#include "heap_reporter.h"
#include <v8-exception.h>
#include <v8-json.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-primitive.h>
#include <v8-profiler.h>
#include <v8-statistics.h>
#include <sstream>
#include <string>
#include "binary_value.h"

namespace MiniRacer {

HeapReporter::HeapReporter(v8::Isolate* isolate_,
                           BinaryValueFactory* bv_factory)
    : isolate_(isolate_), bv_factory_(bv_factory) {}

auto HeapReporter::HeapStats() -> BinaryValue::Ptr {
  const v8::Locker lock(isolate_);
  const v8::Isolate::Scope isolate_scope(isolate_);
  const v8::HandleScope handle_scope(isolate_);

  const v8::TryCatch trycatch(isolate_);
  const v8::Local<v8::Context> context = v8::Context::New(isolate_);
  const v8::Context::Scope context_scope(context);

  v8::HeapStatistics stats;
  isolate_->GetHeapStatistics(&stats);

  const v8::Local<v8::Object> stats_obj = v8::Object::New(isolate_);

  stats_obj
      ->Set(context,
            v8::String::NewFromUtf8Literal(isolate_, "total_physical_size"),
            v8::Number::New(isolate_,
                            static_cast<double>(stats.total_physical_size())))
      .Check();
  stats_obj
      ->Set(context,
            v8::String::NewFromUtf8Literal(isolate_,
                                           "total_heap_size_executable"),
            v8::Number::New(isolate_, static_cast<double>(
                                          stats.total_heap_size_executable())))
      .Check();
  stats_obj
      ->Set(context,
            v8::String::NewFromUtf8Literal(isolate_, "total_heap_size"),
            v8::Number::New(isolate_,
                            static_cast<double>(stats.total_heap_size())))
      .Check();
  stats_obj
      ->Set(context, v8::String::NewFromUtf8Literal(isolate_, "used_heap_size"),
            v8::Number::New(isolate_,
                            static_cast<double>(stats.used_heap_size())))
      .Check();
  stats_obj
      ->Set(context,
            v8::String::NewFromUtf8Literal(isolate_, "heap_size_limit"),
            v8::Number::New(isolate_,
                            static_cast<double>(stats.heap_size_limit())))
      .Check();

  v8::Local<v8::String> output;
  if (!v8::JSON::Stringify(context, stats_obj).ToLocal(&output) ||
      output.IsEmpty()) {
    return bv_factory_->New("error stringifying heap output", type_str_utf8);
  }
  return bv_factory_->ConvertFromV8(context, output);
}

namespace {
// From v8/src/d8/d8-console.cc:
class StringOutputStream : public v8::OutputStream {
 public:
  auto WriteAsciiChunk(char* data, int size) -> WriteResult override {
    os_.write(data, size);
    return kContinue;
  }

  void EndOfStream() override {}

  auto result() -> std::string { return os_.str(); }

 private:
  std::ostringstream os_;
};
}  // end anonymous namespace

auto HeapReporter::HeapSnapshot() -> BinaryValue::Ptr {
  const v8::Locker lock(isolate_);
  const v8::Isolate::Scope isolate_scope(isolate_);
  const v8::HandleScope handle_scope(isolate_);
  const auto* snap = isolate_->GetHeapProfiler()->TakeHeapSnapshot();
  StringOutputStream sos;
  snap->Serialize(&sos);
  return bv_factory_->New(sos.result(), type_str_utf8);
}

}  // end namespace MiniRacer
