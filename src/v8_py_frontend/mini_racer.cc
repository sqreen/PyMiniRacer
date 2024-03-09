#include <libplatform/libplatform.h>
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

using namespace v8;

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
  type_terminated_exception = 204,
};

struct BinaryValue {
  union {
    void* ptr_val;
    char* bytes;
    uint64_t int_val;
    double double_val;
  };
  BinaryTypes type = type_invalid;
  size_t len;

  BinaryValue() {}

  BinaryValue(const std::string& str, BinaryTypes type)
      : type(type), len(str.size()) {
    bytes = new char[len + 1];
    std::copy(str.begin(), str.end(), bytes);
    bytes[len] = '\0';
  }
};

class MiniRacerContext;

class BinaryValueDeleter {
 public:
  BinaryValueDeleter() : context(0) {}
  BinaryValueDeleter(MiniRacerContext* context) : context(context) {}
  void operator()(BinaryValue* bv) const;

 private:
  MiniRacerContext* context;
};

typedef std::unique_ptr<BinaryValue, BinaryValueDeleter> BinaryValuePtr;

class MiniRacerContext {
 public:
  MiniRacerContext();
  ~MiniRacerContext();

  Isolate* isolate;
  Persistent<Context>* persistentContext;
  v8::ArrayBuffer::Allocator* allocator;
  std::map<void*, std::shared_ptr<BackingStore>> backing_stores;
  size_t soft_memory_limit;
  bool soft_memory_limit_reached;
  size_t hard_memory_limit;
  bool hard_memory_limit_reached;

  template <typename... Args>
  BinaryValuePtr makeBinaryValue(Args&&... args);

  void BinaryValueFree(BinaryValue* v);

  std::optional<std::string> valueToUtf8String(Local<Value> value);

  void gc_callback(Isolate* isolate);
  void set_hard_memory_limit(size_t limit);
  void set_soft_memory_limit(size_t limit);
  BinaryValuePtr convert_v8_to_binary(Local<Context> context,
                                      Local<Value> value);
  BinaryValuePtr heap_snapshot();
  BinaryValuePtr heap_stats();
  BinaryValuePtr eval(const std::string& code, unsigned long timeout);
  BinaryValuePtr summarizeTryCatch(Local<Context>& context,
                                   TryCatch& trycatch,
                                   BinaryTypes resultType);
};

inline void BinaryValueDeleter::operator()(BinaryValue* bv) const {
  context->BinaryValueFree(bv);
}

template <typename... Args>
inline BinaryValuePtr MiniRacerContext::makeBinaryValue(Args&&... args) {
  return BinaryValuePtr(new BinaryValue(std::forward<Args>(args)...),
                        BinaryValueDeleter(this));
}

void MiniRacerContext::BinaryValueFree(BinaryValue* v) {
  if (!v) {
    return;
  }
  switch (v->type) {
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_terminated_exception:
    case type_str_utf8:
      delete[] v->bytes;
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
      backing_stores.erase(v);
      break;
  }
  delete v;
}

static std::unique_ptr<Platform> current_platform = nullptr;

static void static_gc_callback(Isolate* isolate,
                               GCType type,
                               GCCallbackFlags flags,
                               void* data) {
  ((MiniRacerContext*)data)->gc_callback(isolate);
}

void MiniRacerContext::gc_callback(Isolate* isolate) {
  HeapStatistics stats;
  isolate->GetHeapStatistics(&stats);
  size_t used = stats.used_heap_size();

  soft_memory_limit_reached = (used > soft_memory_limit);
  isolate->MemoryPressureNotification((soft_memory_limit_reached)
                                          ? v8::MemoryPressureLevel::kModerate
                                          : v8::MemoryPressureLevel::kNone);
  if (used > hard_memory_limit) {
    hard_memory_limit_reached = true;
    isolate->TerminateExecution();
  }
}

void MiniRacerContext::set_hard_memory_limit(size_t limit) {
  hard_memory_limit = limit;
  hard_memory_limit_reached = false;

  if (limit > 0) {
    isolate->AddGCEpilogueCallback(static_gc_callback, this);
  }
}

void MiniRacerContext::set_soft_memory_limit(size_t limit) {
  soft_memory_limit = limit;
  soft_memory_limit_reached = false;
}

static bool maybe_fast_call(const std::string& code) {
  // Does the code string end with '()'?
  // TODO check if the string is an identifier
  return (code.size() > 2 && code[code.size() - 2] == '(' &&
          code[code.size() - 1] == ')');
}

BinaryValuePtr MiniRacerContext::summarizeTryCatch(Local<Context>& context,
                                                   TryCatch& trycatch,
                                                   BinaryTypes resultType) {
  if (!trycatch.StackTrace(context).IsEmpty()) {
    Local<Value> stacktrace;

    if (trycatch.StackTrace(context).ToLocal(&stacktrace)) {
      std::optional<std::string> backtrace = valueToUtf8String(stacktrace);
      if (backtrace.has_value()) {
        // Generally the backtrace from V8 starts with the exception message, so
        // we can skip the exception message (below) when we have the backtrace.
        return makeBinaryValue(backtrace.value(), resultType);
      }
    }
  }

  // Fall back to the backtrace-less exception message:
  if (!trycatch.Exception()->IsNull()) {
    std::optional<std::string> message =
        valueToUtf8String(trycatch.Exception());
    if (message.has_value()) {
      return makeBinaryValue(message.value(), resultType);
    }
  }

  // Send no message at all; the recipient can fill in generic messages based on
  // the type code.
  return makeBinaryValue("", resultType);
}

/** Spawns a separate thread which calls isolate->TerminateExecution() after a
 * timeout, if not first disengaged. */
class BreakerThread {
 public:
  BreakerThread(Isolate* isolate, unsigned long timeout)
      : isolate(isolate), timeout(timeout), timed_out_(false) {
    if (timeout > 0) {
      engaged = true;
      mutex.lock();
      thread_ = std::thread(&BreakerThread::threadMain, this);
    } else {
      engaged = false;
    }
  }

  ~BreakerThread() { disengage(); }

  bool timed_out() { return timed_out_; }

  void disengage() {
    if (engaged) {
      mutex.unlock();
      thread_.join();
      engaged = false;
    }
  }

 private:
  void threadMain() {
    if (!mutex.try_lock_for(std::chrono::milliseconds(timeout))) {
      timed_out_ = true;
      isolate->TerminateExecution();
    }
  }

  Isolate* isolate;
  unsigned long timeout;
  bool engaged;
  bool timed_out_;
  std::thread thread_;
  std::timed_mutex mutex;
};

BinaryValuePtr MiniRacerContext::eval(const std::string& code,
                                      unsigned long timeout) {
  Locker lock(isolate);
  Isolate::Scope isolate_scope(isolate);

  TryCatch trycatch(isolate);

  // Later in this function, we pump the V8 message loop.
  // Per comment in v8/samples/shell.cc, it is important not to pump the message
  // loop when there are v8::Local handles on the stack, as this may trigger a
  // stackless GC when the new conservative stack scanning flag is enabled. So
  // we don't use any Local handles here; only in sub-scopes of this method.

  // Spawn a thread to inforce the timeout limit:
  BreakerThread breaker_thread(isolate, timeout);

  bool parsed = false;
  bool executed = false;
  BinaryValuePtr ret;

  // Is it a single function call?
  if (maybe_fast_call(code)) {
    HandleScope handle_scope(isolate);
    Local<Context> context = persistentContext->Get(isolate);
    Context::Scope context_scope(context);
    Local<String> identifier;
    Local<Value> func;

    // Let's check if the value is a callable identifier
    parsed = String::NewFromUtf8(isolate, code.data(), NewStringType::kNormal,
                                 static_cast<int>(code.size() - 2))
                 .ToLocal(&identifier) &&
             context->Global()->Get(context, identifier).ToLocal(&func) &&
             func->IsFunction();

    if (parsed) {
      // Call the identifier
      MaybeLocal<Value> maybe_value = Local<Function>::Cast(func)->Call(
          context, v8::Undefined(isolate), 0, {});
      if (!maybe_value.IsEmpty()) {
        executed = true;
        ret = convert_v8_to_binary(context, maybe_value.ToLocalChecked());
      }
    }
  }

  // Fallback on a slower full eval
  if (!executed) {
    HandleScope handle_scope(isolate);
    Local<Context> context = persistentContext->Get(isolate);
    Context::Scope context_scope(context);
    Local<String> asString;
    Local<Script> script;

    parsed = String::NewFromUtf8(isolate, code.data(), NewStringType::kNormal,
                                 static_cast<int>(code.size()))
                 .ToLocal(&asString) &&
             Script::Compile(context, asString).ToLocal(&script) &&
             !script.IsEmpty();

    if (!parsed) {
      return summarizeTryCatch(context, trycatch, type_parse_exception);
    }

    MaybeLocal<Value> maybe_value = script->Run(context);
    if (!maybe_value.IsEmpty()) {
      executed = true;
      ret = convert_v8_to_binary(context, maybe_value.ToLocalChecked());
    }
  }

  if (executed) {
    // Execute all pending tasks

    while (!breaker_thread.timed_out() && !hard_memory_limit_reached) {
      bool wait =
          isolate->HasPendingBackgroundTasks();  // Only wait when needed
                                                 // otherwise it waits forever.

      // Run message loop items (like timers)
      if (!platform::PumpMessageLoop(
              current_platform.get(), isolate,
              (wait) ? v8::platform::MessageLoopBehavior::kWaitForWork
                     : v8::platform::MessageLoopBehavior::kDoNotWait)) {
        break;
      }

      // Run microtask items (like promise callbacks)
      isolate->PerformMicrotaskCheckpoint();
    }
  }

  breaker_thread.disengage();

  if (!executed) {
    // Still didn't execute. Find an error:
    HandleScope handle_scope(isolate);
    Local<Context> context = persistentContext->Get(isolate);
    Context::Scope context_scope(context);

    BinaryTypes resultType;

    if (hard_memory_limit_reached) {
      resultType = type_oom_exception;
    } else if (breaker_thread.timed_out()) {
      resultType = type_timeout_exception;
    } else if (trycatch.HasTerminated()) {
      resultType = type_terminated_exception;
    } else {
      resultType = type_execute_exception;
    }

    return summarizeTryCatch(context, trycatch, resultType);
  }

  return ret;
}

std::optional<std::string> MiniRacerContext::valueToUtf8String(
    Local<Value> value) {
  String::Utf8Value utf8(isolate, value);

  if (utf8.length()) {
    return std::make_optional(std::string(*utf8, utf8.length()));
  }

  return std::nullopt;
}

BinaryValuePtr MiniRacerContext::convert_v8_to_binary(Local<Context> context,
                                                      Local<Value> value) {
  Isolate::Scope isolate_scope(isolate);
  HandleScope scope(isolate);

  BinaryValuePtr res = makeBinaryValue();

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
    res->len = size_t(rstr->Utf8Length(isolate));  // in bytes
    size_t capacity = res->len + 1;
    res->bytes = new char[capacity];
    rstr->WriteUtf8(isolate, res->bytes);
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

    backing_stores[res.get()] = backing_store;
    res->type = value->IsSharedArrayBuffer() ? type_shared_array_buffer
                                             : type_array_buffer;
    res->ptr_val = static_cast<char*>(backing_store->Data()) + offset;
    res->len = size;

  } else if (value->IsObject()) {
    res->type = type_object;
    res->int_val = value->ToObject(context).ToLocalChecked()->GetIdentityHash();
  } else {
    return BinaryValuePtr();
  }
  return res;
}

MiniRacerContext::~MiniRacerContext() {
  if (persistentContext) {
    Locker lock(isolate);
    Isolate::Scope isolate_scope(isolate);

    backing_stores.clear();
    persistentContext->Reset();
    delete persistentContext;
  }

  isolate->Dispose();

  delete allocator;
}

void MiniRacer_init_v8(char const* v8_flags,
                       char const* icu_path,
                       char const* snapshot_path) {
  V8::InitializeICU(icu_path);
  V8::InitializeExternalStartupDataFromFile(snapshot_path);

  if (v8_flags != nullptr) {
    V8::SetFlagsFromString(v8_flags);
  }
  if (v8_flags != nullptr && strstr(v8_flags, "--single-threaded") != nullptr) {
    current_platform = platform::NewSingleThreadedDefaultPlatform();
  } else {
    current_platform = platform::NewDefaultPlatform();
  }
  V8::InitializePlatform(current_platform.get());
  V8::Initialize();
}

MiniRacerContext::MiniRacerContext()
    : allocator(ArrayBuffer::Allocator::NewDefaultAllocator()),
      soft_memory_limit(0),
      soft_memory_limit_reached(false),
      hard_memory_limit(0),
      hard_memory_limit_reached(false) {
  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator;

  isolate = Isolate::New(create_params);

  Locker lock(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);

  persistentContext = new Persistent<Context>(isolate, Context::New(isolate));
}

BinaryValuePtr MiniRacerContext::heap_stats() {
  v8::HeapStatistics stats;

  if (!isolate) {
    return BinaryValuePtr();
  }

  Locker lock(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);

  TryCatch trycatch(isolate);
  Local<Context> context = persistentContext->Get(isolate);
  Context::Scope context_scope(context);

  isolate->GetHeapStatistics(&stats);

  Local<Object> stats_obj = Object::New(isolate);

  stats_obj
      ->Set(context, String::NewFromUtf8Literal(isolate, "total_physical_size"),
            Number::New(isolate, (double)stats.total_physical_size()))
      .Check();
  stats_obj
      ->Set(context,
            String::NewFromUtf8Literal(isolate, "total_heap_size_executable"),
            Number::New(isolate, (double)stats.total_heap_size_executable()))
      .Check();
  stats_obj
      ->Set(context, String::NewFromUtf8Literal(isolate, "total_heap_size"),
            Number::New(isolate, (double)stats.total_heap_size()))
      .Check();
  stats_obj
      ->Set(context, String::NewFromUtf8Literal(isolate, "used_heap_size"),
            Number::New(isolate, (double)stats.used_heap_size()))
      .Check();
  stats_obj
      ->Set(context, String::NewFromUtf8Literal(isolate, "heap_size_limit"),
            Number::New(isolate, (double)stats.heap_size_limit()))
      .Check();

  Local<String> output;
  if (!JSON::Stringify(context, stats_obj).ToLocal(&output) ||
      output.IsEmpty()) {
    return BinaryValuePtr();
  }
  return convert_v8_to_binary(context, output);
}

// From v8/src/d8/d8-console.cc:
class StringOutputStream : public v8::OutputStream {
 public:
  WriteResult WriteAsciiChunk(char* data, int size) override {
    os_.write(data, size);
    return kContinue;
  }

  void EndOfStream() override {}

  std::string result() { return os_.str(); }

 private:
  std::ostringstream os_;
};

BinaryValuePtr MiniRacerContext::heap_snapshot() {
  Locker lock(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);
  auto snap = isolate->GetHeapProfiler()->TakeHeapSnapshot();
  StringOutputStream sos;
  snap->Serialize(&sos);
  return makeBinaryValue(sos.result(), type_str_utf8);
}

extern "C" {

LIB_EXPORT BinaryValue* mr_eval_context(MiniRacerContext* mini_racer_context,
                                        char* str,
                                        int len,
                                        unsigned long timeout) {
  return mini_racer_context->eval(std::string(str, len), timeout).release();
}

LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path) {
  MiniRacer_init_v8(v8_flags, icu_path, snapshot_path);
}

LIB_EXPORT MiniRacerContext* mr_init_context() {
  return new MiniRacerContext();
}

LIB_EXPORT void mr_free_value(MiniRacerContext* mini_racer_context,
                              BinaryValue* val) {
  mini_racer_context->BinaryValueFree(val);
}

LIB_EXPORT void mr_free_context(MiniRacerContext* mini_racer_context) {
  delete mini_racer_context;
}

LIB_EXPORT BinaryValue* mr_heap_stats(MiniRacerContext* mini_racer_context) {
  return mini_racer_context->heap_stats().release();
}

LIB_EXPORT void mr_set_hard_memory_limit(MiniRacerContext* mini_racer_context,
                                         size_t limit) {
  mini_racer_context->set_hard_memory_limit(limit);
}

LIB_EXPORT void mr_set_soft_memory_limit(MiniRacerContext* mini_racer_context,
                                         size_t limit) {
  mini_racer_context->set_soft_memory_limit(limit);
}

LIB_EXPORT bool mr_soft_memory_limit_reached(
    MiniRacerContext* mini_racer_context) {
  return mini_racer_context->soft_memory_limit_reached;
}

LIB_EXPORT void mr_low_memory_notification(
    MiniRacerContext* mini_racer_context) {
  mini_racer_context->isolate->LowMemoryNotification();
}

LIB_EXPORT char const* mr_v8_version() {
  return V8_VERSION_STRING;
}

// FOR DEBUGGING ONLY
LIB_EXPORT BinaryValue* mr_heap_snapshot(MiniRacerContext* mini_racer_context) {
  return mini_racer_context->heap_snapshot().release();
}
}
