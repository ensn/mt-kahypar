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

#include <cstring>
#include <limits>

#ifndef NDEBUG
#include <memory>
#include <stdlib.h>
#else
#include <tbb/tbb_allocator.h>
#include <tbb/scalable_allocator.h>
#endif

#include "mt-kahypar/parallel/stl/heap_profiler.h"

namespace mt_kahypar {
namespace parallel {

inline void mtk_record_allocation(void* ptr, size_t size) {
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
  if (ptr) {
    heap_profiler::HeapProfiler::global().record_alloc(ptr, size);
  }
#else
  (void) ptr;
  (void) size;
#endif
}

inline void mtk_record_deallocation(void* ptr) {
#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
  if (ptr) {
    heap_profiler::HeapProfiler::global().record_free(ptr);
  }
#else
  (void) ptr;
#endif
}

#ifndef NDEBUG
template<typename T>
using scalable_allocator_base = std::allocator<T>;
#else
template<typename T>
using scalable_allocator_base = tbb::tbb_allocator<T>;
#endif

template<typename T>
class scalable_allocator : public scalable_allocator_base<T> {
 public:
  using value_type = T;

  template<typename U>
  struct rebind {
    using other = scalable_allocator<U>;
  };

  scalable_allocator() noexcept = default;
  template<typename U>
  scalable_allocator(const scalable_allocator<U>&) noexcept { }

  T* allocate(size_t n) {
    scalable_allocator_base<T> base;
    T* ptr = base.allocate(n);
    mtk_record_allocation(ptr, n * sizeof(T));
    return ptr;
  }

  void deallocate(T* ptr, size_t n) noexcept {
    mtk_record_deallocation(ptr);
    scalable_allocator_base<T> base;
    base.deallocate(ptr, n);
  }

  template<typename U>
  bool operator==(const scalable_allocator<U>&) const noexcept {
    return true;
  }

  template<typename U>
  bool operator!=(const scalable_allocator<U>&) const noexcept {
    return false;
  }
};

// note: TBB allocators will still be used for TBB data structures such as
// tbb::concurrent_vector, tbb::concurrent_queue or tbb::enumerable_thread_specific
#ifndef NDEBUG
inline void* mtk_scalable_malloc(size_t size) {
  void* ptr = malloc(size);
  mtk_record_allocation(ptr, size);
  return ptr;
}

inline void* mtk_scalable_calloc(size_t nobj, size_t size) {
  void* ptr = calloc(nobj, size);
  const size_t allocated_size =
      nobj != 0 && size > std::numeric_limits<size_t>::max() / nobj ? 0 : nobj * size;
  mtk_record_allocation(ptr, allocated_size);
  return ptr;
}

inline void mtk_scalable_free(void* ptr) {
  mtk_record_deallocation(ptr);
  free(ptr);
}
#else
inline void* mtk_scalable_malloc(size_t size) {
  void* ptr = scalable_malloc(size);
  mtk_record_allocation(ptr, size);
  return ptr;
}

inline void* mtk_scalable_calloc(size_t nobj, size_t size) {
  void* ptr = scalable_calloc(nobj, size);
  const size_t allocated_size =
      nobj != 0 && size > std::numeric_limits<size_t>::max() / nobj ? 0 : nobj * size;
  mtk_record_allocation(ptr, allocated_size);
  return ptr;
}

inline void mtk_scalable_free(void* ptr) {
  mtk_record_deallocation(ptr);
  scalable_free(ptr);
}
#endif

}  // namespace parallel
}  // namespace mt_kahypar
