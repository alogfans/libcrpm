//
// Created by Feng Ren on 2021/1/23.
//

#include "internal/allocator.h"
#include "internal/allocators/lrmalloc_allocator.h"
#include "internal/allocators/trivial_allocator.h"
#include "internal/allocators/hook_lrmalloc_allocator.h"

namespace crpm {
    extern bool process_instrumented;

    Allocator *Allocator::Open(Engine *engine,
                               const MemoryPoolOption &option) {
        if (process_instrumented) {
            // Instrumentation is enabled
            if (option.allocator_name == "default") {
                return HookLRMallocAllocator::Open(engine, option);
            }
        } else {
            // Instrumentation is disabled
            if (option.allocator_name == "noop") {
                return TrivialAllocator::Open(engine, option);
            }
            if (option.allocator_name == "default") {
                return LRMallocAllocator::Open(engine, option);
            }
        }
        fprintf(stderr, "unsupported allocator %s\n", option.allocator_name.c_str());
        return nullptr;
    }
}