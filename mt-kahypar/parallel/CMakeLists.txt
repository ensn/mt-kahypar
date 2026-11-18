set(ParallelSources
        thread_management.cpp
        stl/heap_profiler.cpp)

target_sources(MtKaHyPar-Sources INTERFACE ${ParallelSources})
target_sources(MtKaHyPar-ToolsSources INTERFACE ${ParallelSources})
