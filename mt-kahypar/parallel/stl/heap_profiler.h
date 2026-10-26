/*******************************************************************************
 * Heap profiler to measure heap memory usage.
 *
 * @file:   heap_profiler.h
 * @author: Daniel Salwasser
 * @date:   21.10.2023
 ******************************************************************************/
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <mutex>
#include <new>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef KAHYPAR_ENABLE_HEAP_PROFILING
#define START_HEAP_PROFILER(...)                                                                   \
  mt_kahypar::parallel::heap_profiler::HeapProfiler::global().start_profile(__VA_ARGS__)

#define STOP_HEAP_PROFILER() mt_kahypar::parallel::heap_profiler::HeapProfiler::global().stop_profile()

#define SCOPED_HEAP_PROFILER_IMPL(line, ...)                                                       \
  const auto mtkahypar_scoped_heap_profiler_##line =                                               \
      mt_kahypar::parallel::heap_profiler::ScopedHeapProfiler(__VA_ARGS__)
#define SCOPED_HEAP_PROFILER(...) SCOPED_HEAP_PROFILER_IMPL(__LINE__, __VA_ARGS__);

#define ENABLE_HEAP_PROFILER() mt_kahypar::parallel::heap_profiler::HeapProfiler::global().enable()
#define DISABLE_HEAP_PROFILER() mt_kahypar::parallel::heap_profiler::HeapProfiler::global().disable()

#define PRINT_HEAP_PROFILE(out)                                                                    \
  mt_kahypar::parallel::heap_profiler::HeapProfiler::global().print_heap_profile(out)

/*!
 * Whether heap profiling is enabled.
 */
constexpr bool kHeapProfiling = true;

#else

#define START_HEAP_PROFILER(...)
#define STOP_HEAP_PROFILER()
#define SCOPED_HEAP_PROFILER(...)
#define ENABLE_HEAP_PROFILER()
#define DISABLE_HEAP_PROFILER()
#define PRINT_HEAP_PROFILE(...)

/*!
 * Whether heap profiling is enabled.
 */
constexpr bool kHeapProfiling = false;

#endif

#ifdef KAHYPAR_ENABLE_PAGE_PROFILING
constexpr bool kPageProfiling = true;
#else
constexpr bool kPageProfiling = false;
#endif

namespace mt_kahypar::parallel::heap_profiler {

/*!
 * A minimal allocator that uses the C allocator directly instead of Mt-KaHyPar's allocation
 * wrappers.
 *
 * Keeping its bookkeeping allocations outside the profiled wrappers avoids recursive profiling.
 */
template <typename T> struct NoProfileAllocator {
  using value_type = T;

  NoProfileAllocator() noexcept = default;
  template <typename U> NoProfileAllocator(const NoProfileAllocator<U> &) noexcept {}

  template <typename U> bool operator==(const NoProfileAllocator<U> &) const noexcept {
    return true;
  }
  template <typename U> bool operator!=(const NoProfileAllocator<U> &) const noexcept {
    return false;
  }

  T *allocate(const size_t n) const {
    if (n == 0) {
      return nullptr;
    }

    if (n > static_cast<size_t>(-1) / sizeof(T)) {
      throw std::bad_array_new_length();
    }

    void *const ptr = std::malloc(n * sizeof(T));
    if (!ptr) {
      throw std::bad_alloc();
    }

    return static_cast<T *>(ptr);
  }

  void deallocate(T *const ptr, size_t) const noexcept {
    std::free(ptr);
  }

  template <typename... Args> T *create(Args &&...args) const {
    T *t = allocate(1);
    new (t) T(std::forward<Args>(args)...);
    return t;
  }

  void destruct(T *const t) const {
    t->~T();
    deallocate(t, 1);
  }
};

/*!
 * A hierarchical heap profiler to measure dynamic memory allocation of the program.
 *
 * Mt-KaHyPar's scalable allocation wrappers report allocation and deallocation events here.
 */
class HeapProfiler {
private:
  static constexpr std::string_view kMaxAllocTitle = "Peak Memory (MiB)";
  static constexpr std::string_view kAllocTitle = "Total Alloc (MiB)";
  static constexpr std::string_view kFreeTitle = "Total Free (MiB)";
  static constexpr std::string_view kAllocsTitle = "Allocs";
  static constexpr std::string_view kFreesTitle = "Frees";

  static constexpr std::string_view kBranch = "|- ";
  static constexpr std::string_view kTailBranch = "`- ";
  static constexpr std::string_view kTailEdge = "    ";
  static constexpr std::string_view kNameDel = ": ";
  static constexpr char kHeadingPadding = '-';
  static constexpr char kPadding = '.';

  static constexpr std::size_t kBranchLength = 3;
  static constexpr std::size_t kPercentageLength = 9;
  static constexpr std::size_t kDataStructSizeThreshold = 1024;

  [[nodiscard]] static std::string to_megabytes(const std::size_t bytes) {
    std::stringstream stream;
    stream << std::fixed << std::setprecision(2) << (bytes / static_cast<float>(1024 * 1024));
    return stream.str();
  }

public:
  struct HeapProfileTreeNode {
    std::string name;
    std::string description;
    std::string annotation;

    HeapProfileTreeNode *parent;
    std::vector<HeapProfileTreeNode *, NoProfileAllocator<HeapProfileTreeNode *>> children;

    bool is_peak_memory_node;
    std::size_t peak_memory;
    std::size_t total_alloc;
    std::size_t total_free;
    std::size_t num_allocs;
    std::size_t num_frees;

    HeapProfileTreeNode(std::string_view name, std::string description, HeapProfileTreeNode *parent)
        : name(name),
          description(description),
          parent(parent),
          is_peak_memory_node(false),
          peak_memory(0),
          total_alloc(0),
          total_free(0),
          num_allocs(0),
          num_frees(0) {}

    template <typename NodeAllocator> void free(NodeAllocator node_allocator) {
      for (HeapProfileTreeNode *child : children) {
        child->free(node_allocator);
        node_allocator.destruct(child);
      }
    }
  };

  struct HeapProfileTree {
    HeapProfileTreeNode root;
    HeapProfileTreeNode *currentNode;
    std::string annotation;

    HeapProfileTree(std::string_view name) : root(name, "", nullptr), currentNode(&root) {}
  };

private:
  struct HeapProfileTreeStats {
    std::size_t len;
    std::size_t peak_memory;
    std::size_t total_alloc;
    std::size_t total_free;
    std::size_t num_allocs;
    std::size_t num_frees;

    HeapProfileTreeStats(const HeapProfileTreeNode &node) {
      std::size_t name_length = node.name.length();
      if (!node.description.empty()) {
        name_length += node.description.length() + 2;
      }

      len = name_length;
      peak_memory = node.peak_memory;
      total_alloc = node.total_alloc;
      total_free = node.total_free;
      num_allocs = node.num_allocs;
      num_frees = node.num_frees;

      for (auto const &child : node.children) {
        HeapProfileTreeStats child_stats(*child);

        len = std::max(len, child_stats.len + kBranchLength);
        peak_memory = std::max(peak_memory, child_stats.peak_memory);
        total_alloc = std::max(total_alloc, child_stats.total_alloc);
        total_free = std::max(total_free, child_stats.total_free);
        num_allocs = std::max(num_allocs, child_stats.num_allocs);
        num_frees = std::max(num_frees, child_stats.num_frees);
      }
    }
  };

public:
  /**
   * Returns the global heap profiler.
   *
   * @return The global heap profiler.
   */
  static HeapProfiler &global();

  /*!
   * Constructs a new heap profiler.
   *
   * @param name The name of the heap profiler and the name of the root profile.
   */
  explicit HeapProfiler(std::string_view name);

  /*!
   * Destroys the heap profiler.
   */
  ~HeapProfiler();

  /*!
   * Starts profiling the heap.
   */
  void enable();

  /*!
   * Stops profiling the heap.
   */
  void disable();

  /**
   * Starts a new profile, adds it as a child profile to the current profile, and sets it to the
   * current profile.
   *
   * @param name The name of the profile to start.
   * @param desc The description of the profile to start.
   */
  void start_profile(std::string_view name, std::string desc = {});

  /*!
   * Stops the current profile and sets the new current profile to the parent profile.
   */
  void stop_profile();

  /*!
   * Records a memory allocation.
   *
   * @param ptr The pointer to the beginning of the allocated memory.
   * @param size The number allocated bytes.
   */
  void record_alloc(const void *ptr, std::size_t size);

  /*!
   * Records a memory deallocation.
   *
   * @param ptr The pointer to the beginning of the allocated memory
   */
  void record_free(const void *ptr);

  /*!
   * Adjusts the summary settings for experiments.
   */
  void set_experiment_summary_options();

  /*!
   * Sets the maximum depth shown in the summary.
   *
   * @param max_depth The maximum depth shown in the summary.
   */
  void set_max_depth(std::size_t max_depth);

  /*!
   * Sets the option whether to highlight the peak memory node in the summary.
   *
   * @param print Whether to highlight the peak memory node in the summary.
   */
  void set_highlight_peak_memory_node(bool highlight);

  /*!
   * Prints information about the heap profile to the output stream.
   *
   * @param out The output stream to write to.
   */
  void print_heap_profile(std::ostream &out);

  /*!
   * Returns the peak memory in bytes of the current heap profile.
   *
   * @return The peak memory in bytes of the current heap profile.
   */
  [[nodiscard]] std::size_t peak_memory();

  /*!
   * Returns the total amount of allocated memory in bytes of the current heap profile.
   *
   * @return The total amount of allocated memory in bytes of the current heap profile.
   */
  [[nodiscard]] std::size_t total_alloc();

  /*!
   * Returns the total amount of freed memory in bytes of the current heap profile.
   *
   * @return The total amount of freed memory in bytes of the current heap profile.
   */
  [[nodiscard]] std::size_t total_free();

  /*!
   * Returns the number of alloc operations of the current heap profile.
   *
   * @return The number of alloc operations of the current heap profile.
   */
  [[nodiscard]] std::size_t num_allocs();

  /*!
   * Returns the number of free operations of the current heap profile.
   *
   * @return The number of free operations of the current heap profile.
   */
  [[nodiscard]] std::size_t num_frees();

  /*!
   * Returns the tree that stores the data of this heap profiler.
   *
   * @return The tree that stores the data of this heap profiler.
   */
  [[nodiscard]] HeapProfileTree &tree_root();

private:
  struct AllocationInfo {
    std::size_t size;
    HeapProfileTreeNode *allocation_node;
  };

  std::atomic_bool _enabled = false;
  std::mutex _mutex;

  std::size_t _sum_suspicious_allocs = 0;
  std::size_t _num_suspicious_allocs = 0;
  std::size_t _num_suspicious_frees = 0;

  NoProfileAllocator<HeapProfileTreeNode> _node_allocator;
  HeapProfileTree _tree;
  HeapProfileTreeNode *_peak_memory_node;
  std::unordered_map<
      const void *,
      AllocationInfo,
      std::hash<const void *>,
      std::equal_to<const void *>,
      NoProfileAllocator<std::pair<const void *const, AllocationInfo>>>
      _address_map;

  std::size_t _max_depth = std::numeric_limits<std::size_t>::max();
  bool _highlight_peak_memory_node = true;
  bool _print_data_structs = false;
  std::size_t _min_data_struct_size = 0;

  static void print_heap_tree_node(
      std::ostream &out,
      const HeapProfileTreeNode &node,
      const HeapProfileTreeStats stats,
      std::size_t max_depth,
      bool print_data_structs,
      bool highlight_peak_memory_node,
      std::size_t min_data_struct_size,
      std::size_t depth = 0,
      bool last = false
  );
  static void print_indentation(std::ostream &out, std::size_t depth, bool last);
  static void print_percentage(std::ostream &out, const float percentage);
  static void print_statistics(
      std::ostream &out, const HeapProfileTreeNode &node, const HeapProfileTreeStats stats
  );
};

/*!
 * A helper class for scoped heap profiling. The profile starts with the construction of the object
 * and ends with the destruction of the object.
 */
class ScopedHeapProfiler {
public:
  /*!
   * Constructs a new scoped heap profiler and thereby starting a new heap profile.
   *
   * @param name The name of the started profile.
   * @param description The description of the started profile.
   */
  ScopedHeapProfiler(std::string_view name, std::string description = {}) {
    HeapProfiler::global().start_profile(name, description);
  }

  /*!
   * Deconstructs the scoped heap profiler and thereby stopping the heap profile.
   */
  inline ~ScopedHeapProfiler() {
    HeapProfiler::global().stop_profile();
  }
};

} // namespace mt_kahypar::parallel::heap_profiler