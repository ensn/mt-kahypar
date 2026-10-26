/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2025 Nikolai Maas <nikolai.maas@kit.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>

#ifdef NDEBUG
#include <tbb/tbb_allocator.h>
#include <tbb/scalable_allocator.h>
#endif

#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
#include <cstdio>
#endif

namespace mt_kahypar {
namespace parallel {

#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
inline void record_heap_profile_event(const char* event,
                                     const void* pointer,
                                     const size_t size,
                                     const char* allocator) {
  std::fprintf(stderr, "%s,%p,%zu,%s\n", event, pointer, size, allocator);
}
#endif

#ifndef NDEBUG
// note: tbb allocators will still be used for TBB data structures such as
// tbb::concurrent_vector, tbb::concurrent_queue or tbb::enumerable_thread_specific

template <typename T>
using scalable_allocator_base = std::allocator<T>;

#else
template <typename T>
using scalable_allocator_base = tbb::tbb_allocator<T>;
#endif

template <typename T>
class scalable_allocator {
  template <typename>
  friend class scalable_allocator;

  using base_allocator = scalable_allocator_base<T>;

 public:
  using value_type = T;
  using is_always_equal = std::true_type;
  template <typename U>
  struct rebind {
    using other = scalable_allocator<U>;
  };

  scalable_allocator() noexcept = default;

  template <typename U>
  scalable_allocator(const scalable_allocator<U>&) noexcept { }

  [[nodiscard]] T* allocate(const size_t n) {
    T* ptr = _allocator.allocate(n);
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
    record_heap_profile_event("alloc", ptr, n * sizeof(T), "scalable_allocator");
#endif
    return ptr;
  }

  void deallocate(T* ptr, const size_t n) noexcept {
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
    record_heap_profile_event("free", ptr, n * sizeof(T), "scalable_allocator");
#endif
    _allocator.deallocate(ptr, n);
  }

  template <typename U>
  bool operator==(const scalable_allocator<U>&) const noexcept {
    return true;
  }

 private:
  base_allocator _allocator;
};

inline void* mtk_scalable_malloc(const size_t size) {
#ifndef NDEBUG
  void* ptr = std::malloc(size);
#else
  void* ptr = scalable_malloc(size);
#endif
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
  if (ptr != nullptr) {
    record_heap_profile_event("alloc", ptr, size, "mtk_scalable_malloc");
  }
#endif
  return ptr;
}

inline void* mtk_scalable_calloc(const size_t nobj, const size_t size) {
#ifndef NDEBUG
  void* ptr = std::calloc(nobj, size);
#else
  void* ptr = scalable_calloc(nobj, size);
#endif
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
  if (ptr != nullptr) {
    record_heap_profile_event("alloc", ptr, nobj * size, "mtk_scalable_calloc");
  }
#endif
  return ptr;
}

inline void mtk_scalable_free(void* ptr) {
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
  if (ptr != nullptr) {
    record_heap_profile_event("free", ptr, 0, "mtk_scalable_free");
  }
#endif
#ifndef NDEBUG
  std::free(ptr);
#else
  scalable_free(ptr);
#endif
}

}  // namespace parallel
}  // namespace mt_kahypar
