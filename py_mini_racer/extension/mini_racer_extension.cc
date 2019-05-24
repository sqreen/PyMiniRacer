
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <v8.h>
#include <v8-profiler.h>
#include <libplatform/libplatform.h>

template<class T> static inline T* xalloc(T*& ptr, size_t x = sizeof(T))
{
    void *tmp = malloc(x);
    if (tmp == NULL) {
        fprintf(stderr, "malloc failed. Aborting");
        abort();
    }
    ptr = static_cast<T*>(tmp);
    return static_cast<T*>(ptr);
}

enum BinaryTypes {
    type_invalid   =   0,
    type_null      =   1,
    type_bool      =   2,
    type_integer   =   3,
    type_double    =   4,
    type_str_utf8  =   5,
    type_array     =   6,
    type_hash      =   7,
    type_date      =   8,
    type_symbol    =   9,

    type_function  = 100,

    type_execute_exception = 200,
    type_parse_exception   = 201,
    type_oom_exception = 202,
    type_timeout_exception = 203,
};

/* This is a generic store for arbitrary JSON like values.
 * Non scalar values are:
 *  - Strings: pointer to a string
 *  - Arrays: contiguous map of pointers to BinaryValue
 *  - Hash: contiguous map of pair of pointers to BinaryTypes (first is key,
 *          second is value)
 */
struct BinaryValue {
    union {
        BinaryValue **array_val;
        BinaryValue **hash_val;
        char *str_val;
        uint32_t int_val;
        double double_val;
    };
    enum BinaryTypes type = type_invalid;
    size_t len;
};

void BinaryValueFree(BinaryValue *v) {
    if (!v) {
        return;
    }
    switch(v->type) {
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_str_utf8:
        free(v->str_val);
        break;
    case type_array:
        for (size_t i = 0; i < v->len; i++) {
            BinaryValue *w = v->array_val[i];
            BinaryValueFree(w);
        }
        free(v->array_val);
        break;
    case type_hash:
        for(size_t i = 0; i < v->len; i++) {
            BinaryValue *k = v->hash_val[i*2];
            BinaryValue *w = v->hash_val[i*2+1];
            BinaryValueFree(k);
            BinaryValueFree(w);
        }
        free(v->hash_val);
        break;
    case type_bool:
    case type_double:
    case type_date:
    case type_null:
    case type_integer:
    case type_function: // no value implemented
    case type_symbol:
    case type_invalid:
        // the other types are scalar values
        break;
    }
    free(v);
}


using namespace v8;

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
    useconds_t timeout;
    EvalResult* result;
    size_t max_memory;
} EvalParams;

enum IsolateFlags {
    MEM_SOFTLIMIT_VALUE,
    MEM_SOFTLIMIT_REACHED,
};

static Platform* current_platform = NULL;

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
    if (current_platform == NULL) {
        V8::InitializeICU();
        current_platform = platform::CreateDefaultPlatform();
        V8::InitializePlatform(current_platform);
        V8::Initialize();
    }
}

static void* breaker(void *d) {
  EvalParams* data = (EvalParams*)d;
  usleep(data->timeout*1000);
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  data->result->timed_out = true;
  data->context_info->isolate->TerminateExecution();
  return NULL;
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
        result->message->Reset(isolate, trycatch.Exception()->ToString());
    } else {

        pthread_t breaker_thread = 0;

        // timeout limit
        auto timeout = eval_params->timeout;
        if (timeout > 0) {
            pthread_create(&breaker_thread, NULL, breaker, (void*)eval_params);
        }
        // memory limit
        if (eval_params->max_memory > 0) {
            isolate->SetData(MEM_SOFTLIMIT_VALUE, &eval_params->max_memory);
            isolate->AddGCEpilogueCallback(gc_callback);
        }

        MaybeLocal<Value> maybe_value = parsed_script.ToLocalChecked()->Run(context);

        if (timeout > 0) {
            pthread_cancel(breaker_thread);
            pthread_join(breaker_thread, NULL);
        }

        result->executed = !maybe_value.IsEmpty();
        if (!result->executed) {
            if (trycatch.HasCaught()) {
                if (!trycatch.Exception()->IsNull()) {
                    result->message = new Persistent<Value>();
                    result->message->Reset(isolate, trycatch.Exception()->ToString());
                } else if(trycatch.HasTerminated()) {
                    result->terminated = true;
                    result->message = new Persistent<Value>();
                    Local<String> tmp;
                    if (result->timed_out) {
                        tmp = String::NewFromUtf8(isolate, "JavaScript was terminated by timeout");
                    } else {
                        tmp = String::NewFromUtf8(isolate, "JavaScript was terminated");
                    }
                    result->message->Reset(isolate, tmp);
                }

                if (!trycatch.StackTrace().IsEmpty()) {
                    result->backtrace = new Persistent<Value>();
                    result->backtrace->Reset(isolate, trycatch.StackTrace()->ToString());
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

#define HEAP_NB_ITEMS 5

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


static BinaryValue *convert_v8_to_binary(ContextInfo *context_info,
                                         Handle<Value> &value)
{
    Local<Context> context = context_info->context->Get(context_info->isolate);

    Context::Scope context_scope(context);

    Isolate *isolate = context_info->isolate;
    HandleScope scope(isolate);

    BinaryValue *res = new (xalloc(res)) BinaryValue();

    if (value->IsNull() || value->IsUndefined()) {
        res->type = type_null;
    }

    else if (value->IsInt32()) {
        res->type = type_integer;
        auto val = value->Uint32Value();
        res->int_val = val;
    }

    // ECMA-262, 4.3.20
    // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
    else if (value->IsNumber()) {
        res->type = type_double;
        double val = value->NumberValue();
        res->double_val = val;
    }

    else if (value->IsBoolean()) {
        res->type = type_bool;
        res->int_val = (value->IsTrue() ? 1 : 0);
    }

    else if (value->IsArray()) {
        Local<Array> arr = Local<Array>::Cast(value);
        size_t len = arr->Length();
        BinaryValue **ary = xalloc(ary, sizeof(*ary) * len);

        res->type = type_array;
        res->array_val = ary;

        for(uint32_t i = 0; i < arr->Length(); i++) {
            Local<Value> element = arr->Get(i);
            BinaryValue *bin_value = convert_v8_to_binary(context_info, element);
            if (bin_value == NULL) {
                goto err;
            }
            ary[i] = bin_value;
            res->len++;
        }
    }

    else if (value->IsFunction()){
        res->type = type_function;
    }

    else if (value->IsSymbol()){
        res->type = type_symbol;
    }

    else if (value->IsDate()) {
        res->type = type_date;
        Local<Date> date = Local<Date>::Cast(value);

        double timestamp = date->ValueOf();
        res->double_val = timestamp;
    }

    else if (value->IsObject()) {
        res->type = type_hash;

        TryCatch trycatch(isolate);

        Local<Object> object = value->ToObject();
        MaybeLocal<Array> maybe_props = object->GetOwnPropertyNames(context);
        if (!maybe_props.IsEmpty()) {
            Local<Array> props = maybe_props.ToLocalChecked();
            uint32_t hash_len = props->Length();

            if (hash_len > 0) {
                res->hash_val = xalloc(res->hash_val,
                                       sizeof(*res->hash_val) * hash_len * 2);
            }

            for (uint32_t i = 0; i < hash_len; i++) {
                Local<Value> pkey = props->Get(i);
                Local<Value> pvalue = object->Get(pkey);
                // this may have failed due to Get raising

                if (trycatch.HasCaught()) {
                    // TODO: factor out code converting exception in
                    //       nogvl_context_eval() and use it here/?
                    goto err;
                }

                BinaryValue *bin_key = convert_v8_to_binary(context_info, pkey);
                BinaryValue *bin_value = convert_v8_to_binary(context_info, pvalue);

                if (!bin_key || !bin_value) {
                    BinaryValueFree(bin_key);
                    BinaryValueFree(bin_value);
                    goto err;
                }

                res->hash_val[i * 2]     = bin_key;
                res->hash_val[i * 2 + 1] = bin_value;
                res->len++;
            }
        } // else empty hash
    }

    else {
        Local<String> rstr = value->ToString();

        res->type = type_str_utf8;
        res->len = size_t(rstr->Utf8Length()); // in bytes
        size_t capacity = res->len + 1;
        res->str_val = xalloc(res->str_val, capacity);
        rstr->WriteUtf8(res->str_val);
    }
    return res;

err:
    BinaryValueFree(res);
    return NULL;
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

    BinaryValue *bmessage = NULL;
    BinaryValue *bbacktrace = NULL;

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
            eval_params.timeout = (useconds_t)timeout;
        }
        if (max_memory > 0) {
            eval_params.max_memory = max_memory;
        }

        nogvl_context_eval(&eval_params);

        if (eval_result.message) {
            Local<Value> tmp = Local<Value>::New(context_info->isolate,
                                                 *eval_result.message);
            bmessage = convert_v8_to_binary(context_info, tmp);
        }

        if (eval_result.backtrace) {
            Local<Value> tmp = Local<Value>::New(context_info->isolate,
                                                 *eval_result.backtrace);
            bbacktrace = convert_v8_to_binary(context_info, tmp);
        }
    }

    // bmessage and bbacktrace are now potentially allocated
    // they are always freed at the end of the function

    // NOTE: this is very important, we can not do an raise from within
    // a v8 scope, if we do the scope is never cleaned up properly and we leak
    if (!eval_result.parsed) {
        result = xalloc(result);
        result->type = type_parse_exception;

        if (bmessage && bmessage->type == type_str_utf8) {
            // canibalize bmessage
            result->str_val = bmessage->str_val;
            result->len = bmessage->len;
            free(bmessage);
            bmessage = NULL;
        } else {
            result->str_val = strdup("Unknown JavaScript error during parse");
            result->len = result->str_val ? strlen(result->str_val) : 0;
        }
    }

    else if (!eval_result.executed) {
        result = xalloc(result);
        result->str_val = nullptr;

        bool mem_softlimit_reached = (bool)context_info->isolate->GetData(MEM_SOFTLIMIT_REACHED);
        if (mem_softlimit_reached) {
            result->type = type_oom_exception;
        } else {
            if (eval_result.timed_out) {
                result->type = type_timeout_exception;
            } else {
                result->type = type_execute_exception;
            }
        }

        if (bmessage && bmessage->type == type_str_utf8 &&
                bbacktrace && bbacktrace->type == type_str_utf8) {
            // +1 for \n, +1 for NUL terminator
            size_t dest_size = bmessage->len + bbacktrace->len + 1 + 1;
            char *dest = xalloc(dest, dest_size);
            memcpy(dest, bmessage->str_val, bmessage->len);
            dest[bmessage->len] = '\n';
            memcpy(dest + bmessage->len + 1, bbacktrace->str_val, bbacktrace->len);
            dest[dest_size - 1] = '\0';

            result->str_val = dest;
            result->len = dest_size - 1;
        } else if(bmessage && bmessage->type == type_str_utf8) {
            // canibalize bmessage
            result->str_val = bmessage->str_val;
            result->len = bmessage->len;
            free(bmessage);
            bmessage = NULL;
        } else {
            result->str_val = strdup("Unknown JavaScript error during execution");
            result->len = result->str_val ? strlen(result->str_val) : 0;
        }
    }

    else {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

        Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.value);
        result = convert_v8_to_binary(context_info, tmp);
    }

    BinaryValueFree(bmessage);
    BinaryValueFree(bbacktrace);

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

BinaryValue* mr_eval_context(ContextInfo *context_info, char *str, int len, unsigned long timeout, size_t max_memory) {
    BinaryValue *res = MiniRacer_eval_context_unsafe(context_info, str, len, timeout, max_memory);
    return res;
}

ContextInfo *mr_init_context() {
    ContextInfo *res = MiniRacer_init_context();
    return res;
}

void mr_free_value(BinaryValue *val) {
    BinaryValueFree(val);
}

void mr_free_context(ContextInfo *context_info) {
    deallocate(context_info);
}

BinaryValue *mr_heap_stats(ContextInfo *context_info) {
    return heap_stats(context_info);
}

void mr_low_memory_notification(ContextInfo *context_info) {
    context_info->isolate->LowMemoryNotification();
}

// FOR DEBUGGING ONLY
BinaryValue* mr_heap_snapshot(ContextInfo *context_info) {
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
