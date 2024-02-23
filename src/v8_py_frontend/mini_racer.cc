#include <libplatform/libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <v8-profiler.h>
#include <v8-version-string.h>
#include <v8.h>

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#ifdef V8_OS_WIN
#define LIB_EXPORT __declspec(dllexport)
#else  // V8_OS_WIN
#define LIB_EXPORT __attribute__((visibility("default")))
#endif

template <class T>
static inline T* xalloc(T*& ptr, size_t x = sizeof(T)) {
  void* tmp = malloc(x);
  if (tmp == NULL) {
    fprintf(stderr, "malloc failed. Aborting");
    abort();
  }
  ptr = static_cast<T*>(tmp);
  return static_cast<T*>(ptr);
}

using namespace v8;

struct ContextInfo {
  Isolate* isolate;
  Persistent<Context>* context;
  v8::ArrayBuffer::Allocator* allocator;
  std::map<void*, std::shared_ptr<BackingStore>> backing_stores;
  bool interrupted;
  size_t soft_memory_limit;
  bool soft_memory_limit_reached;
  size_t hard_memory_limit;
  bool hard_memory_limit_reached;
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
  static void kill_value(Persistent<Value>* val) {
    if (!val) {
      return;
    }
    val->Reset();
    delete val;
  }
};

typedef struct {
  ContextInfo* context_info;
  const char* eval;
  int eval_len;
  unsigned long timeout;
  EvalResult* result;
  size_t max_memory;
} EvalParams;

enum BinaryTypes {
  type_invalid = 0,
  type_null = 1,
  type_bool = 2,
  type_integer = 3,
  type_double = 4,
  type_str_utf8 = 5,
  // type_array     =   6,  // deprecated
  // type_hash      =   7,  // deprecated
  type_date = 8,
  type_symbol = 9,
  type_object = 10,

  type_function = 100,
  type_shared_array_buffer = 101,
  type_array_buffer = 102,

  type_execute_exception = 200,
  type_parse_exception = 201,
  type_oom_exception = 202,
  type_timeout_exception = 203,
};

struct BinaryValue {
  union {
    void* ptr_val;
    char* str_val;
    uint32_t int_val;
    double double_val;
  };
  enum BinaryTypes type = type_invalid;
  size_t len;
};

void BinaryValueFree(ContextInfo* context_info, BinaryValue* v) {
  if (!v) {
    return;
  }
  switch (v->type) {
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_str_utf8:
      free(v->str_val);
      break;
    case type_bool:
    case type_double:
    case type_date:
    case type_null:
    case type_integer:
    case type_function:  // no value implemented
    case type_symbol:
    case type_object:
    case type_invalid:
      // the other types are scalar values
      break;
    case type_shared_array_buffer:
    case type_array_buffer:
      context_info->backing_stores.erase(v);
      break;
  }
  free(v);
}

enum IsolateData {
  CONTEXT_INFO,
};

static std::unique_ptr<Platform> current_platform = NULL;

static void gc_callback(Isolate* isolate, GCType type, GCCallbackFlags flags) {
  ContextInfo* context_info = (ContextInfo*)isolate->GetData(CONTEXT_INFO);

  if (context_info == nullptr) {
    return;
  }

  HeapStatistics stats;
  isolate->GetHeapStatistics(&stats);
  size_t used = stats.used_heap_size();

  context_info->soft_memory_limit_reached =
      (used > context_info->soft_memory_limit);
  isolate->MemoryPressureNotification((context_info->soft_memory_limit_reached)
                                          ? v8::MemoryPressureLevel::kModerate
                                          : v8::MemoryPressureLevel::kNone);
  if (used > context_info->hard_memory_limit) {
    context_info->hard_memory_limit_reached = true;
    isolate->TerminateExecution();
  }
}

static void init_v8(char const* flags,
                    char const* icu_path,
                    char const* snapshot_path) {
  V8::InitializeICU(icu_path);
  V8::InitializeExternalStartupDataFromFile(snapshot_path);

  if (flags != NULL) {
    V8::SetFlagsFromString(flags);
  }
  if (flags != NULL && strstr(flags, "--single-threaded") != NULL) {
    current_platform = platform::NewSingleThreadedDefaultPlatform();
  } else {
    current_platform = platform::NewDefaultPlatform();
  }
  V8::InitializePlatform(current_platform.get());
  V8::Initialize();
}

static void breaker(std::timed_mutex& breaker_mutex, void* d) {
  EvalParams* data = (EvalParams*)d;

  if (!breaker_mutex.try_lock_for(std::chrono::milliseconds(data->timeout))) {
    data->result->timed_out = true;
    data->context_info->isolate->TerminateExecution();
  }
}

static void set_hard_memory_limit(ContextInfo* context_info, size_t limit) {
  context_info->hard_memory_limit = limit;
  context_info->hard_memory_limit_reached = false;
}

static bool maybe_fast_call(const char* eval, int eval_len) {
  // Does the eval string ends with '()'?
  // TODO check if the string is an identifier
  return (eval_len > 2 && eval[eval_len - 2] == '(' &&
          eval[eval_len - 1] == ')');
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

  set_hard_memory_limit(eval_params->context_info, eval_params->max_memory);

  result->parsed = false;
  result->executed = false;
  result->terminated = false;
  result->timed_out = false;
  result->value = NULL;

  std::timed_mutex breaker_mutex;
  std::thread breaker_thread;

  // timeout limit
  auto timeout = eval_params->timeout;
  if (timeout > 0) {
    breaker_mutex.lock();
    breaker_thread =
        std::thread(&breaker, std::ref(breaker_mutex), (void*)eval_params);
  }
  // memory limit
  if (eval_params->max_memory > 0) {
    isolate->AddGCEpilogueCallback(gc_callback);
  }

  MaybeLocal<Value> maybe_value;

  // Is it a single function call?
  if (maybe_fast_call(eval_params->eval, eval_params->eval_len)) {
    Local<String> identifier;
    Local<Value> func;

    // Let's check if the value is a callable identifier
    result->parsed =
        String::NewFromUtf8(isolate, eval_params->eval, NewStringType::kNormal,
                            eval_params->eval_len - 2)
            .ToLocal(&identifier) &&
        context->Global()->Get(context, identifier).ToLocal(&func) &&
        func->IsFunction();

    if (result->parsed) {
      // Call the identifier
      maybe_value = Local<Function>::Cast(func)->Call(
          context, v8::Undefined(isolate), 0, {});
      result->executed = !maybe_value.IsEmpty();
    }
  }

  // Fallback on a slower full eval
  if (!result->executed) {
    Local<String> eval;
    Local<Script> script;

    result->parsed =
        String::NewFromUtf8(isolate, eval_params->eval, NewStringType::kNormal,
                            eval_params->eval_len)
            .ToLocal(&eval) &&
        Script::Compile(context, eval).ToLocal(&script) && !script.IsEmpty();

    if (!result->parsed) {
      result->message = new Persistent<Value>();
      result->message->Reset(isolate, trycatch.Exception());
      return NULL;
    }

    maybe_value = script->Run(context);
    result->executed = !maybe_value.IsEmpty();
  }

  if (result->executed) {
    // Execute all pending tasks
    while (!result->timed_out &&
           !eval_params->context_info->hard_memory_limit_reached) {
      bool wait =
          isolate->HasPendingBackgroundTasks();  // Only wait when needed
                                                 // otherwise it waits forever.

      if (!platform::PumpMessageLoop(
              current_platform.get(), isolate,
              (wait) ? v8::platform::MessageLoopBehavior::kWaitForWork
                     : v8::platform::MessageLoopBehavior::kDoNotWait)) {
        break;
      }
      isolate->PerformMicrotaskCheckpoint();
    }
  }

  if (timeout > 0) {
    breaker_mutex.unlock();
    breaker_thread.join();
  }

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

        len = snprintf(
            buf, sizeof(buf), "%s at %s:%i:%i",
            *String::Utf8Value(isolate, message->Get()),
            *String::Utf8Value(isolate, message->GetScriptResourceName()
                                            ->ToString(context)
                                            .ToLocalChecked()),
            line, column);

        if ((size_t)len >= sizeof(buf)) {
          len = sizeof(buf) - 1;
          buf[len] = '\0';
        }

        Local<String> v8_message =
            String::NewFromUtf8(isolate, buf, NewStringType::kNormal, len)
                .ToLocalChecked();
        result->message->Reset(isolate, v8_message);
      } else if (trycatch.HasTerminated()) {
        result->terminated = true;
        result->message = new Persistent<Value>();
        Local<String> tmp;
        if (result->timed_out) {
          tmp = String::NewFromUtf8(isolate,
                                    "JavaScript was terminated by timeout")
                    .ToLocalChecked();
        } else {
          tmp = String::NewFromUtf8(isolate, "JavaScript was terminated")
                    .ToLocalChecked();
        }
        result->message->Reset(isolate, tmp);
      }

      if (!trycatch.StackTrace(context).IsEmpty()) {
        Local<Value> stacktrace;

        if (trycatch.StackTrace(context).ToLocal(&stacktrace)) {
          Local<Value> tmp;

          if (stacktrace->ToString(context).ToLocal(&tmp)) {
            result->backtrace = new Persistent<Value>();
            result->backtrace->Reset(isolate, tmp);
          }
        }
      }
    }
  } else {
    Local<Value> tmp;

    if (maybe_value.ToLocal(&tmp)) {
      result->value = new Persistent<Value>();
      result->value->Reset(isolate, tmp);
    }
  }

  return NULL;
}

static BinaryValue* convert_v8_to_binary(ContextInfo* context_info,
                                         Local<Context> context,
                                         Local<Value> value) {
  Isolate::Scope isolate_scope(context_info->isolate);
  HandleScope scope(context_info->isolate);

  BinaryValue* res = new (xalloc(res)) BinaryValue();

  if (value->IsNull() || value->IsUndefined()) {
    res->type = type_null;
  } else if (value->IsInt32()) {
    res->type = type_integer;
    auto val = value->Uint32Value(context).ToChecked();
    res->int_val = val;
  }
  // ECMA-262, 4.3.20
  // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
  else if (value->IsNumber()) {
    res->type = type_double;
    double val = value->NumberValue(context).ToChecked();
    res->double_val = val;
  } else if (value->IsBoolean()) {
    res->type = type_bool;
    res->int_val = (value->IsTrue() ? 1 : 0);
  } else if (value->IsFunction()) {
    res->type = type_function;
  } else if (value->IsSymbol()) {
    res->type = type_symbol;
  } else if (value->IsDate()) {
    res->type = type_date;
    Local<Date> date = Local<Date>::Cast(value);

    double timestamp = date->ValueOf();
    res->double_val = timestamp;
  } else if (value->IsString()) {
    Local<String> rstr = value->ToString(context).ToLocalChecked();

    res->type = type_str_utf8;
    res->len = size_t(rstr->Utf8Length(context_info->isolate));  // in bytes
    size_t capacity = res->len + 1;
    res->str_val = xalloc(res->str_val, capacity);
    rstr->WriteUtf8(context_info->isolate, res->str_val);
  } else if (value->IsSharedArrayBuffer() || value->IsArrayBuffer() ||
             value->IsArrayBufferView()) {
    std::shared_ptr<BackingStore> backing_store;
    size_t offset = 0;
    size_t size = 0;

    if (value->IsArrayBufferView()) {
      Local<ArrayBufferView> view = Local<ArrayBufferView>::Cast(value);

      backing_store = view->Buffer()->GetBackingStore();
      offset = view->ByteOffset();
      size = view->ByteLength();
    } else if (value->IsSharedArrayBuffer()) {
      backing_store = Local<SharedArrayBuffer>::Cast(value)->GetBackingStore();
      size = backing_store->ByteLength();
    } else {
      backing_store = Local<ArrayBuffer>::Cast(value)->GetBackingStore();
      size = backing_store->ByteLength();
    }

    context_info->backing_stores[res] = backing_store;
    res->type = value->IsSharedArrayBuffer() ? type_shared_array_buffer
                                             : type_array_buffer;
    res->ptr_val = static_cast<char*>(backing_store->Data()) + offset;
    res->len = size;

  } else if (value->IsObject()) {
    res->type = type_object;
    res->int_val = value->ToObject(context).ToLocalChecked()->GetIdentityHash();
  } else {
    BinaryValueFree(context_info, res);
    res = nullptr;
  }
  return res;
}

static BinaryValue* convert_v8_to_binary(ContextInfo* context_info,
                                         const Persistent<Context>& context,
                                         Local<Value> value) {
  HandleScope scope(context_info->isolate);
  return convert_v8_to_binary(
      context_info, Local<Context>::New(context_info->isolate, context), value);
}

static void deallocate(void* data) {
  ContextInfo* context_info = static_cast<ContextInfo*>(data);

  if (context_info == NULL || context_info->isolate == NULL) {
    return;
  }

  if (context_info->context) {
    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);

    context_info->backing_stores.clear();
    context_info->context->Reset();
    delete context_info->context;
    context_info->context = NULL;
  }

  if (context_info->interrupted) {
    fprintf(stderr,
            "WARNING: V8 isolate was interrupted by Python, "
            "it can not be disposed and memory will not be "
            "reclaimed till the Python process exits.");
  } else {
    context_info->isolate->Dispose();
    context_info->isolate = NULL;
  }

  delete context_info->allocator;
  delete context_info;
}

void MiniRacer_init_v8(char const* v8_flags,
                       char const* icu_path,
                       char const* snapshot_path) {
  init_v8(v8_flags, icu_path, snapshot_path);
}

ContextInfo* MiniRacer_init_context() {
  ContextInfo* context_info = new (xalloc(context_info)) ContextInfo();
  context_info->allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = context_info->allocator;

  context_info->isolate = Isolate::New(create_params);

  Locker lock(context_info->isolate);
  Isolate::Scope isolate_scope(context_info->isolate);
  HandleScope handle_scope(context_info->isolate);

  Local<Context> context = Context::New(context_info->isolate);

  context_info->context = new Persistent<Context>();
  context_info->context->Reset(context_info->isolate, context);
  context_info->isolate->SetData(CONTEXT_INFO, (void*)context_info);

  return context_info;
}

static BinaryValue* MiniRacer_eval_context_unsafe(ContextInfo* context_info,
                                                  const char* eval,
                                                  size_t eval_len,
                                                  unsigned long timeout,
                                                  size_t max_memory) {
  EvalParams eval_params;
  EvalResult eval_result{};

  BinaryValue* result = NULL;

  BinaryValue* bmessage = NULL;
  BinaryValue* bbacktrace = NULL;

  if (context_info == NULL || eval == NULL || static_cast<int>(eval_len) < 0) {
    return NULL;
  }

  {
    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);
    HandleScope handle_scope(context_info->isolate);

    eval_params.context_info = context_info;

    eval_params.eval = eval;
    eval_params.eval_len = static_cast<int>(eval_len);
    eval_params.result = &eval_result;
    eval_params.timeout = timeout;
    eval_params.max_memory = max_memory;

    nogvl_context_eval(&eval_params);

    if (eval_result.message) {
      Local<Value> tmp =
          Local<Value>::New(context_info->isolate, *eval_result.message);

      bmessage =
          convert_v8_to_binary(context_info, *context_info->context, tmp);
    }

    if (eval_result.backtrace) {
      Local<Value> tmp =
          Local<Value>::New(context_info->isolate, *eval_result.backtrace);
      bbacktrace =
          convert_v8_to_binary(context_info, *context_info->context, tmp);
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

    if (context_info->hard_memory_limit_reached) {
      result->type = type_oom_exception;
    } else {
      if (eval_result.timed_out) {
        result->type = type_timeout_exception;
      } else {
        result->type = type_execute_exception;
      }
    }

    if (bmessage && bmessage->type == type_str_utf8 && bbacktrace &&
        bbacktrace->type == type_str_utf8) {
      // +1 for \n, +1 for NUL terminator
      size_t dest_size = bmessage->len + bbacktrace->len + 1 + 1;
      char* dest = xalloc(dest, dest_size);
      memcpy(dest, bmessage->str_val, bmessage->len);
      dest[bmessage->len] = '\n';
      memcpy(dest + bmessage->len + 1, bbacktrace->str_val, bbacktrace->len);
      dest[dest_size - 1] = '\0';

      result->str_val = dest;
      result->len = dest_size - 1;
    } else if (bmessage && bmessage->type == type_str_utf8) {
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

  else if (eval_result.value) {
    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);
    HandleScope handle_scope(context_info->isolate);

    Local<Value> tmp =
        Local<Value>::New(context_info->isolate, *eval_result.value);
    result = convert_v8_to_binary(context_info, *context_info->context, tmp);
  }

  BinaryValueFree(context_info, bmessage);
  BinaryValueFree(context_info, bbacktrace);

  return result;
}

static BinaryValue* heap_stats(ContextInfo* context_info) {
  v8::HeapStatistics stats;

  if (!context_info || !context_info->isolate) {
    return NULL;
  }

  Locker lock(context_info->isolate);
  Isolate::Scope isolate_scope(context_info->isolate);
  HandleScope handle_scope(context_info->isolate);

  TryCatch trycatch(context_info->isolate);
  Local<Context> context = context_info->context->Get(context_info->isolate);
  Context::Scope context_scope(context);

  context_info->isolate->GetHeapStatistics(&stats);

  Local<Object> stats_obj = Object::New(context_info->isolate);

  stats_obj
      ->Set(context,
            String::NewFromUtf8Literal(context_info->isolate,
                                       "total_physical_size"),
            Number::New(context_info->isolate,
                        (double)stats.total_physical_size()))
      .Check();
  stats_obj
      ->Set(context,
            String::NewFromUtf8Literal(context_info->isolate,
                                       "total_heap_size_executable"),
            Number::New(context_info->isolate,
                        (double)stats.total_heap_size_executable()))
      .Check();
  stats_obj
      ->Set(
          context,
          String::NewFromUtf8Literal(context_info->isolate, "total_heap_size"),
          Number::New(context_info->isolate, (double)stats.total_heap_size()))
      .Check();
  stats_obj
      ->Set(context,
            String::NewFromUtf8Literal(context_info->isolate, "used_heap_size"),
            Number::New(context_info->isolate, (double)stats.used_heap_size()))
      .Check();
  stats_obj
      ->Set(
          context,
          String::NewFromUtf8Literal(context_info->isolate, "heap_size_limit"),
          Number::New(context_info->isolate, (double)stats.heap_size_limit()))
      .Check();

  Local<String> output;
  if (!JSON::Stringify(context, stats_obj).ToLocal(&output) ||
      output.IsEmpty()) {
    return NULL;
  }
  return convert_v8_to_binary(context_info, context, output);
}

class BufferOutputStream : public OutputStream {
 public:
  BinaryValue* bv;
  BufferOutputStream() {
    bv = xalloc(bv);
    bv->len = 0;
    bv->type = type_str_utf8;
    bv->str_val = nullptr;
  }
  virtual ~BufferOutputStream() {}  // don't destroy the stuff
  virtual void EndOfStream() {}
  virtual int GetChunkSize() { return 1000000; }
  virtual WriteResult WriteAsciiChunk(char* data, int size) {
    size_t oldlen = bv->len;
    bv->len = oldlen + size_t(size);
    bv->str_val = static_cast<char*>(realloc(bv->str_val, bv->len));
    if (!bv->str_val) {
      return kAbort;
    }
    memcpy(bv->str_val + oldlen, data, (size_t)size);
    return kContinue;
  }
};

extern "C" {

LIB_EXPORT BinaryValue* mr_eval_context(ContextInfo* context_info,
                                        char* str,
                                        int len,
                                        unsigned long timeout,
                                        size_t max_memory) {
  BinaryValue* res = MiniRacer_eval_context_unsafe(context_info, str, len,
                                                   timeout, max_memory);
  return res;
}

LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path) {
  MiniRacer_init_v8(v8_flags, icu_path, snapshot_path);
}

LIB_EXPORT ContextInfo* mr_init_context() {
  return MiniRacer_init_context();
}

LIB_EXPORT void mr_free_value(ContextInfo* context_info, BinaryValue* val) {
  BinaryValueFree(context_info, val);
}

LIB_EXPORT void mr_free_context(ContextInfo* context_info) {
  deallocate(context_info);
}

LIB_EXPORT BinaryValue* mr_heap_stats(ContextInfo* context_info) {
  return heap_stats(context_info);
}

LIB_EXPORT void mr_set_hard_memory_limit(ContextInfo* context_info,
                                         size_t limit) {
  set_hard_memory_limit(context_info, limit);
}

LIB_EXPORT void mr_set_soft_memory_limit(ContextInfo* context_info,
                                         size_t limit) {
  context_info->soft_memory_limit = limit;
  context_info->soft_memory_limit_reached = false;
}

LIB_EXPORT bool mr_soft_memory_limit_reached(ContextInfo* context_info) {
  return context_info->soft_memory_limit_reached;
}

LIB_EXPORT void mr_low_memory_notification(ContextInfo* context_info) {
  context_info->isolate->LowMemoryNotification();
}

LIB_EXPORT char const* mr_v8_version() {
  return V8_VERSION_STRING;
}

// FOR DEBUGGING ONLY
LIB_EXPORT BinaryValue* mr_heap_snapshot(ContextInfo* context_info) {
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
