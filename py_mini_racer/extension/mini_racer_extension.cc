#include <thread>
#include <mutex>
#include <chrono>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <v8.h>
#include <v8-profiler.h>
#include <libplatform/libplatform.h>

#include "serializer.h"

#ifdef V8_OS_WIN
# define LIB_EXPORT __declspec(dllexport)
#else  // V8_OS_WIN
# define LIB_EXPORT __attribute__ ((visibility("default")))
#endif

namespace v8 {

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

struct ContextInfo {
    Isolate* isolate;
    Persistent<Context>* context;
    ArrayBufferAllocator* allocator;
    bool interrupted;
};

struct EvalResult {
    bool parsed;
    bool executed;
    bool terminated;
    bool timed_out;
    Persistent<Value>* value;
    Persistent<Value>* message;
    Persistent<Value>* backtrace;

    ~EvalResult() {
        kill_value(value);
        kill_value(message);
        kill_value(backtrace);
    }

private:
    static void kill_value(Persistent<Value> *val) {
        if (!val) {
            return;
        }
        val->Reset();
        delete val;
    }
};

typedef struct {
    ContextInfo* context_info;
    Local<String>* eval;
    unsigned long timeout;
    EvalResult* result;
    size_t max_memory;
} EvalParams;

enum IsolateFlags {
    MEM_SOFTLIMIT_VALUE,
    MEM_SOFTLIMIT_REACHED,
};

static std::unique_ptr<Platform> current_platform = NULL;
static std::mutex platform_lock;

static void gc_callback(Isolate *isolate, GCType type, GCCallbackFlags flags) {
    if((bool)isolate->GetData(MEM_SOFTLIMIT_REACHED)) return;

    size_t softlimit = *(size_t*) isolate->GetData(MEM_SOFTLIMIT_VALUE);

    HeapStatistics stats;
    isolate->GetHeapStatistics(&stats);
    size_t used = stats.used_heap_size();

    if(used > softlimit) {
        isolate->SetData(MEM_SOFTLIMIT_REACHED, (void*)true);
        isolate->TerminateExecution();
    }
}

static void init_v8() {
    // no need to wait for the lock if already initialized
    if (current_platform != NULL) return;

    platform_lock.lock();

    if (current_platform == NULL) {
        V8::InitializeICU();
        current_platform = platform::NewDefaultPlatform();
        V8::InitializePlatform(current_platform.get());
        V8::Initialize();
    }

    platform_lock.unlock();
}

static void breaker(std::timed_mutex& breaker_mutex, void * d) {
  EvalParams* data = (EvalParams*)d;

  if (!breaker_mutex.try_lock_for(std::chrono::milliseconds(data->timeout))) {
    data->result->timed_out = true;
    data->context_info->isolate->TerminateExecution();
  }
}

static void* nogvl_context_eval(void* arg) {
    EvalParams* eval_params = (EvalParams*)arg;
    EvalResult* result = eval_params->result;
    Isolate* isolate = eval_params->context_info->isolate;
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);

    TryCatch trycatch(isolate);

    Local<Context> context = eval_params->context_info->context->Get(isolate);

    Context::Scope context_scope(context);

    // Memory softlimit
    isolate->SetData(MEM_SOFTLIMIT_VALUE, (void*)false);
    // Memory softlimit hit flag
    isolate->SetData(MEM_SOFTLIMIT_REACHED, (void*)false);

    MaybeLocal<Script> parsed_script = Script::Compile(context, *eval_params->eval);
    result->parsed = !parsed_script.IsEmpty();
    result->executed = false;
    result->terminated = false;
    result->timed_out = false;
    result->value = NULL;

    if (!result->parsed) {
        result->message = new Persistent<Value>();
        result->message->Reset(isolate, trycatch.Exception());
    } else {

        std::timed_mutex breaker_mutex;
        std::thread breaker_thread;

        // timeout limit
        auto timeout = eval_params->timeout;
        if (timeout > 0) {
            breaker_mutex.lock();
            breaker_thread = std::thread(&breaker, std::ref(breaker_mutex), (void *) eval_params);
        }
        // memory limit
        if (eval_params->max_memory > 0) {
            isolate->SetData(MEM_SOFTLIMIT_VALUE, &eval_params->max_memory);
            isolate->AddGCEpilogueCallback(gc_callback);
        }

        MaybeLocal<Value> maybe_value = parsed_script.ToLocalChecked()->Run(context);

        if (timeout > 0) {
            breaker_mutex.unlock();
            breaker_thread.join();
        }

        result->executed = !maybe_value.IsEmpty();
        if (!result->executed) {
            if (trycatch.HasCaught()) {
                if (!trycatch.Exception()->IsNull()) {
                    result->message = new Persistent<Value>();
			Local<Message> message = trycatch.Message();
			char buf[1000];
			int len, line, column;

			if (!message->GetLineNumber(context).To(&line)) {
			  line = 0;
			}

			if (!message->GetStartColumn(context).To(&column)) {
			  column = 0;
			}

			len = snprintf(buf, sizeof(buf), "%s at %s:%i:%i", *String::Utf8Value(isolate, message->Get()),
				       *String::Utf8Value(isolate, message->GetScriptResourceName()->ToString(context).ToLocalChecked()),
				       line,
				       column);

			if ((size_t) len >= sizeof(buf)) {
			    len = sizeof(buf) - 1;
			    buf[len] = '\0';
			}

			Local<String> v8_message = String::NewFromUtf8(isolate, buf, NewStringType::kNormal, len).ToLocalChecked();
			result->message->Reset(isolate, v8_message);
                } else if(trycatch.HasTerminated()) {
                    result->terminated = true;
                    result->message = new Persistent<Value>();
                    Local<String> tmp;
                    if (result->timed_out) {
                        tmp = String::NewFromUtf8(isolate, "JavaScript was terminated by timeout").ToLocalChecked();
                    } else {
                        tmp = String::NewFromUtf8(isolate, "JavaScript was terminated").ToLocalChecked();
                    }
                    result->message->Reset(isolate, tmp);
                }

                if (!trycatch.StackTrace(context).IsEmpty()) {
                    result->backtrace = new Persistent<Value>();
                    result->backtrace->Reset(isolate, trycatch.StackTrace(context).ToLocalChecked());
                }
            }
        } else {
            Persistent<Value>* persistent = new Persistent<Value>();
            persistent->Reset(isolate, maybe_value.ToLocalChecked());
            result->value = persistent;
        }
    }

    return NULL;
}


#define HEAP_NB_ITEMS 5

static BinaryValue *new_bv_str(const char *str) {
    BinaryValue *bv = xalloc(bv);
    bv->type = type_str_utf8;
    if (str) {
        bv->len     = strlen(str);
        bv->str_val = strdup(str);
    } else {
        bv->len     = 0;
        bv->str_val = NULL;
    }
    return bv;
}

template<class T> static BinaryValue *new_bv_int(T val) {
    BinaryValue *bv = xalloc(bv);
    bv->type        = type_integer;
    bv->len         = 0;
    bv->int_val     = uint32_t(val);
    return bv;
}

static BinaryValue *heap_stats(ContextInfo *context_info) {

    Isolate* isolate;
    v8::HeapStatistics stats;

    if (!context_info) {
        return NULL;
    }

    isolate = context_info->isolate;

    BinaryValue **content = xalloc(content, sizeof(BinaryValue *) * 2 * HEAP_NB_ITEMS);
    BinaryValue *hash = xalloc(hash);
    hash->type = type_hash;
    hash->len = HEAP_NB_ITEMS;
    hash->hash_val = content;

    if (!hash || !content) {
        free(hash);
        free(content);
        return NULL;
    }

    uint32_t idx = 0;
    content[idx++ * 2] = new_bv_str("total_physical_size");
    content[idx++ * 2] = new_bv_str("total_heap_size_executable");
    content[idx++ * 2] = new_bv_str("total_heap_size");
    content[idx++ * 2] = new_bv_str("used_heap_size");
    content[idx++ * 2] = new_bv_str("heap_size_limit");

    idx = 0;
    if (!isolate) {
        content[idx++ * 2 + 1] = new_bv_int(0);
        content[idx++ * 2 + 1] = new_bv_int(0);
        content[idx++ * 2 + 1] = new_bv_int(0);
        content[idx++ * 2 + 1] = new_bv_int(0);
        content[idx++ * 2 + 1] = new_bv_int(0);
    } else {
	isolate->GetHeapStatistics(&stats);

        content[idx++ * 2 + 1] = new_bv_int(stats.total_physical_size());
        content[idx++ * 2 + 1] = new_bv_int(stats.total_heap_size_executable());
        content[idx++ * 2 + 1] = new_bv_int(stats.total_heap_size());
        content[idx++ * 2 + 1] = new_bv_int(stats.used_heap_size());
        content[idx++ * 2 + 1] = new_bv_int(stats.heap_size_limit());
    }

    for(idx=0; idx < HEAP_NB_ITEMS; idx++) {
        if(content[idx*2] == NULL || content[idx*2+1] == NULL) {
            goto err;
        }
    }
    return hash;

err:
    for(idx=0; idx < HEAP_NB_ITEMS; idx++) {
        free(content[idx*2]);
        free(content[idx*2+1]);
    }
    free(hash);
    free(content);
    return NULL;
}


ContextInfo *MiniRacer_init_context()
{
    init_v8();

    ContextInfo* context_info = xalloc(context_info);
    context_info->allocator = new ArrayBufferAllocator();
    context_info->interrupted = false;
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = context_info->allocator;

    context_info->isolate = Isolate::New(create_params);

    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);
    HandleScope handle_scope(context_info->isolate);

    Local<Context> context = Context::New(context_info->isolate);

    context_info->context = new Persistent<Context>();
    context_info->context->Reset(context_info->isolate, context);

    return context_info;
}

static BinaryValue* MiniRacer_eval_context_unsafe(
        ContextInfo *context_info,
        char *utf_str, int str_len,
        unsigned long timeout, size_t max_memory)
{
    EvalParams eval_params;
    EvalResult eval_result{};

    BinaryValue *result = NULL;

    if (context_info == NULL) {
        return NULL;
    }

    if (utf_str == NULL) {
        return NULL;
    }

    {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

        Local<String> eval = String::NewFromUtf8(context_info->isolate,
                                                 utf_str,
                                                 NewStringType::kNormal,
                                                 str_len).ToLocalChecked();

        eval_params.context_info = context_info;
        eval_params.eval = &eval;
        eval_params.result = &eval_result;
        eval_params.timeout = 0;
        eval_params.max_memory = 0;
        if (timeout > 0) {
            eval_params.timeout = timeout;
        }
        if (max_memory > 0) {
            eval_params.max_memory = max_memory;
        }

        nogvl_context_eval(&eval_params);
    }

     if (!eval_result.executed) {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

	PickleSerializer serializer = PickleSerializer(context_info->isolate, Local<Context>::New(context_info->isolate, *context_info->context));
        Local<Value> tmp;
        Local<Value> tmp_exc;
        if (eval_result.message) {
            tmp = Local<Value>::New(context_info->isolate,
                                                 *eval_result.message);
	} else {
            tmp = String::NewFromUtf8(context_info->isolate, "Unknown JavaScript error").ToLocalChecked();
	}
	if (eval_result.backtrace) {
            tmp_exc = Local<Value>::New(context_info->isolate, *eval_result.backtrace);
	} else {
            tmp_exc = String::NewFromUtf8(context_info->isolate, "").ToLocalChecked();
	}

	if (!eval_result.parsed) {
		serializer.WriteException("JSParseException", tmp, tmp_exc);
	} else {
            bool mem_softlimit_reached = (bool)context_info->isolate->GetData(MEM_SOFTLIMIT_REACHED);
            if (mem_softlimit_reached) {
		serializer.WriteException("JSOOMException", tmp, tmp_exc);
            } else {
                if (eval_result.timed_out) {
		serializer.WriteException("JSTimeoutException", tmp, tmp_exc);
                } else {
		serializer.WriteException("JSException", tmp, tmp_exc);
                }
            }
        }
	result = new (xalloc(result)) BinaryValue();
	std::pair<uint8_t *, size_t> ret;
	if (serializer.Release().To(&ret)) {
		result->type = type_pickle;
		result->buf = std::get<0>(ret);
		result->len = std::get<1>(ret);
        } else {
		result->type = type_invalid;
	}

    } else {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

        Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.value);
        result = convert_v8_to_binary(context_info->isolate, *context_info->context, tmp);
    }

    return result;
}

class BufferOutputStream: public OutputStream {
public:
    BinaryValue *bv;
    BufferOutputStream() {
        bv = xalloc(bv);
        bv->len = 0;
        bv->type = type_str_utf8;
        bv->str_val = nullptr;
    }
    virtual ~BufferOutputStream() {} // don't destroy the stuff
    virtual void EndOfStream() {}
    virtual int GetChunkSize() { return 1000000; }
    virtual WriteResult WriteAsciiChunk(char* data, int size) {
        size_t oldlen = bv->len;
        bv->len = oldlen + size_t(size);
        bv->str_val = static_cast<char *>(realloc(bv->str_val, bv->len));
        if (!bv->str_val) {
            return kAbort;
        }
        memcpy(bv->str_val + oldlen, data, (size_t) size);
        return kContinue;
    }
};

extern "C" {

LIB_EXPORT BinaryValue * mr_eval_context(ContextInfo *context_info, char *str, int len, unsigned long timeout, size_t max_memory) {
    BinaryValue *res = MiniRacer_eval_context_unsafe(context_info, str, len, timeout, max_memory);
    return res;
}

static void deallocate(void * data) {
    ContextInfo* context_info = (ContextInfo*)data;
    {
        // XXX: what is the point of this?
        Locker lock(context_info->isolate);
    }

    {
        context_info->context->Reset();
        delete context_info->context;
    }

    if (context_info->interrupted) {
        fprintf(stderr, "WARNING: V8 isolate was interrupted by Python, "
                        "it can not be disposed and memory will not be "
                        "reclaimed till the Python process exits.");
    } else {
        context_info->isolate->Dispose();
    }

    delete context_info->allocator;
    free(context_info);
}

LIB_EXPORT ContextInfo * mr_init_context() {
    ContextInfo *res = MiniRacer_init_context();
    return res;
}

LIB_EXPORT void mr_free_value(BinaryValue *val) {
    BinaryValueFree(val);
}

LIB_EXPORT void mr_free_context(ContextInfo *context_info) {
    deallocate(context_info);
}

LIB_EXPORT BinaryValue * mr_heap_stats(ContextInfo *context_info) {
    return heap_stats(context_info);
}

LIB_EXPORT void mr_low_memory_notification(ContextInfo *context_info) {
    context_info->isolate->LowMemoryNotification();
}

// FOR DEBUGGING ONLY
LIB_EXPORT BinaryValue * mr_heap_snapshot(ContextInfo *context_info) {
    Isolate* isolate = context_info->isolate;
    Locker lock(isolate);
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    auto snap = isolate->GetHeapProfiler()->TakeHeapSnapshot();
    BufferOutputStream bos{};
    snap->Serialize(&bos);
    return bos.bv;
}
}
}
