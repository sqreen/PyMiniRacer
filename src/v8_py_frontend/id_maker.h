#ifndef INCLUDE_MINI_RACER_ID_MAKER_H
#define INCLUDE_MINI_RACER_ID_MAKER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MiniRacer {

/** Assigns uint64_t IDs to C++ objects.
 *
 * Assigning arbitrary numeric IDs to C++ objects is a common pattern in
 * PyMiniRacer, because it provides a safe way to share references to objects
 * with both Python and JavaScript. Neither Python nor JavaScript provides
 * strong object lifecycle management guarantees (and V8 in particular seems
 * hostile to the idea), so it's hard to trust the Python or JavaScript
 * runtimes to tell us when they're actually done using a reference.
 *
 * Instead we create a numeric ID and hand that to Python and JavaScript. This
 * way we can validate the ID when it's passed back to C++, more gracefully
 * handling use-after-free errors, and providing a backing stop for garbage
 * collection even if Python or JavaScript never sends a finalization signal.
 */
template <typename T>
class IdMaker {
 public:
  auto MakeId(std::shared_ptr<T> object) -> uint64_t;
  auto GetObject(uint64_t object_id) -> std::shared_ptr<T>;
  void EraseId(uint64_t object_id);
  auto CountIds() -> size_t;
  auto GetObjects() -> std::vector<std::shared_ptr<T>>;

 private:
  std::mutex mutex_;
  uint64_t next_object_id_{1};
  std::unordered_map<uint64_t, std::shared_ptr<T>> objects_;
};

/** Registers an ID for the given object, and then unregisters that ID upon
 * destruction. */
template <typename T>
class IdHolder {
 public:
  IdHolder(std::shared_ptr<T> object, std::shared_ptr<IdMaker<T>> id_maker);
  ~IdHolder();

  IdHolder(const IdHolder&) = delete;
  auto operator=(const IdHolder&) -> IdHolder& = delete;
  IdHolder(IdHolder&& other) noexcept;
  auto operator=(IdHolder&& other) -> IdHolder& = delete;

  auto GetId() -> uint64_t;
  auto GetObject() -> std::shared_ptr<T>;

 private:
  std::shared_ptr<IdMaker<T>> id_maker_;
  uint64_t object_id_;
};

template <typename T>
inline auto IdMaker<T>::MakeId(std::shared_ptr<T> object) -> uint64_t {
  const std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t object_id = next_object_id_++;
  objects_[object_id] = std::move(object);
  return object_id;
}

template <typename T>
inline auto IdMaker<T>::GetObject(uint64_t object_id) -> std::shared_ptr<T> {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto iter = objects_.find(object_id);
  if (iter == objects_.end()) {
    return {};
  }
  return iter->second;
}

template <typename T>
inline void IdMaker<T>::EraseId(uint64_t object_id) {
  std::shared_ptr<T> object;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto iter = objects_.find(object_id);
    if (iter == objects_.end()) {
      return;
    }
    object = std::move(iter->second);
    objects_.erase(iter);
  }
  // We actually destruct the object here, outside of the mutex, so that other
  // threads can continue to make, get, and erase object IDs.
  // (Of course, other shared_ptr references to this object may also exist, and
  // if so this object will be destructed at another time entirely.)
}

template <typename T>
inline auto IdMaker<T>::CountIds() -> size_t {
  const std::lock_guard<std::mutex> lock(mutex_);
  return objects_.size();
}

template <typename T>
inline auto IdMaker<T>::GetObjects() -> std::vector<std::shared_ptr<T>> {
  std::vector<std::shared_ptr<T>> ret;
  const std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& pair : objects_) {
    ret.push_back(pair.second);
  }
  return ret;
}

template <typename T>
inline IdHolder<T>::IdHolder(std::shared_ptr<T> object,
                             std::shared_ptr<IdMaker<T>> id_maker)
    : id_maker_(std::move(id_maker)), object_id_(id_maker_->MakeId(object)) {}

template <typename T>
inline IdHolder<T>::~IdHolder() {
  if (object_id_ == 0) {
    return;
  }

  id_maker_->EraseId(object_id_);
}

template <typename T>
inline IdHolder<T>::IdHolder(IdHolder<T>&& other) noexcept
    : id_maker_(std::move(other.id_maker_)),
      object_id_(std::exchange(other.object_id_, 0)) {}

template <typename T>
inline auto IdHolder<T>::GetId() -> uint64_t {
  return object_id_;
}

template <typename T>
inline auto IdHolder<T>::GetObject() -> std::shared_ptr<T> {
  return id_maker_->GetObject(object_id_);
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ID_MAKER_H
