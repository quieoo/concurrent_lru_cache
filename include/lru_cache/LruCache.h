/*
 * Copyright © 2020 Andrew Penkrat <contact.aldrog@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LRU_CACHE_LRUCACHE_H
#define LRU_CACHE_LRUCACHE_H

#include "Log.h"

#include <atomic>
#include <forward_list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <functional>

namespace lru_cache {

namespace detail {

template <typename Key, typename Value> struct Defaults {
  static constexpr auto initialize = [](const Key &) { return Value{}; };
};

} // namespace detail

template <typename Key, typename Value,
          typename InitFunctor =
              decltype(detail::Defaults<Key, Value>::initialize)>
class LruCache {
  using HandleBase = std::shared_ptr<Value>;
  using Value_Function=std::function<void(Key, std::shared_ptr<Value>)>;

public:
  

  LruCache(size_t max_unused, const InitFunctor &init_function = detail::Defaults<Key, Value>::initialize) : max_unused{max_unused}, init_function{init_function} {}
  
  // LruCache(size_t max_unused,
  //                   const InitFunctor &init_function =
  //                       detail::Defaults<Key, Value>::initialize)
  //     : max_unused{max_unused}, init_function{init_function} {}

  LruCache(size_t max_unused, Value_Function ef, const InitFunctor &init_function = detail::Defaults<Key, Value>::initialize)
      : max_unused{max_unused}, evict_function(ef), init_function{init_function} {}

  LruCache& operator=(const LruCache &other){
    max_unused=other.max_unused;
    // init_function=other.init_function;
    evict_function=other.evict_function;
    return *this;
  }

  class Handle : public HandleBase {
  public:
    ~Handle() {
      if (this->use_count() == 2) // Current handle and cached value
        parent.unuse(key);
    }

  private:
    friend LruCache;
    Handle(LruCache &parent, const Key &key, const HandleBase &value)
        : std::shared_ptr<Value>(value), parent{parent}, key{key} {
      if (this->use_count() == 2) // Current handle and cached value
        parent.use(key);
    }

    LruCache &parent;
    Key key;
  };

  /**
   * Inserts a new element into the container if a matching key does not exist.
   *
   * @param key The key of the element to insert.
   * @param args The arguments to forward to the constructor of the value type.
   *
   * @return A pair consisting of a handle to the inserted element and a bool
   * indicating whether the insertion took place.
   *
   * @throws None
   */
  template <typename... Args>
  std::pair<Handle, bool> emplace(const Key &key, Args &&... args) {
    HandleBase result;
    bool inserted;
    {
      std::scoped_lock lock{map_mutex};
      auto [it, ins] = map.emplace(
          key, std::make_shared<Value>(std::forward<Args>(args)...));
      result = it->second;
      inserted = ins;
    }
    // No need to explicitly unuse key as it's implicitly used by result
    return {Handle{*this, key, result}, inserted};
  }

  /**
   * Retrieves the value associated with the given key.
   *
   * @param key the key to look up
   *
   * @return a handle to the value associated with the key
   *
   * @throws N/A
   */
  Handle at(const Key &key) {
    std::shared_lock lock{map_mutex};
    return Handle{*this, key, map.at(key)};
  }

  /**
   * Check if the map contains the specified key.
   *
   * @param key the key to check for
   *
   * @return true if the map contains the key, false otherwise
   *
   * @throws None
   */
  bool contains(const Key &key) const {
    std::shared_lock lock{map_mutex};
    return map.count(key);
  }

  Handle operator[](const Key &key) {
    HandleBase result;
    {
      std::scoped_lock lock{map_mutex};
      if (!map.count(key)) {
        result = std::make_shared<Value>(init_function(key));
        map.emplace(key, result);
      }
    }
    if (result) {
      return Handle{*this, key, result};
    }
    std::shared_lock lock{map_mutex};
    return Handle{*this, key, map.at(key)};
  }

  void erase(const Key &key) {
    std::scoped_lock lock{map_mutex};
    // Allowing to erase used elements may lead to hard-to-handle situations
    // e.g. element erased -> new element created with the same key
    // -> old handle destructed and tries to mark it's key as unused
    if (map.at(key).use_count() > 1)
      throw std::logic_error{"Erasing used elements is not supported"};
    log << "Erasing " << key << ".\n";
    use(key);
    map.erase(key);
  }

  // Erases all unused elements
  void clear() {
    std::scoped_lock lock{list_mutex, map_mutex};
    while (!unused.empty()) {
      const Key &key = unused.front();
      log << "Erasing " << key << ".\n";
      map.erase(key);
      unused.erase_after(unused.before_begin());
    }
    unused_size = 0;
    unused_back = unused.before_begin();
    log << unused_size << " unused elements.\n";
  }

private:
  friend Handle;

  /**
   * Use the provided key, remove it from the unused list, and update the log.
   *
   * @param key the key to be used
   *
   * @return void
   *
   * @throws N/A
   */
  void use(const Key &key) {
    {
      std::scoped_lock lock{list_mutex};
      for (auto it = unused.begin(), prev = unused.before_begin();
           it != unused.end(); ++it, ++prev) {
        if (*it == key) {
          if (++it == unused.end())
            unused_back = prev;
          unused.erase_after(prev);
          break;
        }
      }
    }
    unused_size--;
    log << "Using " << key << ". " << unused_size << " unused elements.\n";
  }

  /**
   * A function to mark a key as unused and perform cleanup if necessary.
   *
   * @param key the key to be marked as unused
   *
   * @return void
   *
   * @throws None
   */
  void unuse(const Key &key) {
    {
      std::scoped_lock lock{list_mutex};
      unused_back = unused.insert_after(unused_back, key);
    }
    log << "Unusing " << key << ". " << unused_size + 1
        << " unused elements.\n";
    if (++unused_size > max_unused)
      cleanup();
  }

  /**
   * Cleans up the resources by erasing an unused element and its corresponding
   * entry in the map.
   */
  void cleanup() {
    auto key = [this]() {
      std::scoped_lock lock{list_mutex};
      Key lru = unused.front();
      unused.pop_front();
      return lru;
    }();
    if(evict_function){
      evict_function(key, map[key]);
    }
    log << "Erasing " << key << ". " << unused_size - 1
        << " unused elements.\n";
    unused_size--;
    std::scoped_lock lock{map_mutex};
    map.erase(key);
  }

  std::unordered_map<Key, HandleBase> map;
  mutable std::shared_mutex map_mutex;
  std::forward_list<Key> unused;
  typename std::forward_list<Key>::const_iterator unused_back =
      unused.before_begin();
  std::atomic<size_t> unused_size{0};
  std::mutex list_mutex;
  size_t max_unused = 10;

  InitFunctor init_function;
  Value_Function evict_function;

};

} // namespace lru_cache

#endif // LRU_CACHE_LRUCACHE_H
