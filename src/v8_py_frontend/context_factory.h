#ifndef INCLUDE_MINI_RACER_CONTEXT_FACTORY_H
#define INCLUDE_MINI_RACER_CONTEXT_FACTORY_H

#include <v8-platform.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include "callback.h"
#include "context.h"
#include "gsl_stub.h"
#include "id_maker.h"

namespace MiniRacer {

class ContextFactory {
 public:
  static void Init(const std::string& v8_flags,
                   const std::filesystem::path& icu_path,
                   const std::filesystem::path& snapshot_path);

  static auto Get() -> ContextFactory*;
  auto MakeContext(Callback callback) -> uint64_t;
  auto GetContext(uint64_t context_id) -> std::shared_ptr<Context>;
  void FreeContext(uint64_t context_id);
  auto Count() -> size_t;

 private:
  ContextFactory(const std::string& v8_flags,
                 const std::filesystem::path& icu_path,
                 const std::filesystem::path& snapshot_path);

  static std::once_flag init_flag_;
  static gsl::owner<ContextFactory*> singleton_;
  std::unique_ptr<v8::Platform> current_platform_;
  IdMaker<Context> contexts_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CONTEXT_FACTORY_H
