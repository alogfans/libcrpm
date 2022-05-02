//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_HOOK_LRMALLOC_ALLOCATOR_H
#define LIBCRPM_HOOK_LRMALLOC_ALLOCATOR_H

#include "lrmalloc_allocator.h"

namespace crpm {
    class HookLRMallocAllocator : public Allocator {
    public:
        HookLRMallocAllocator();

        virtual ~HookLRMallocAllocator();

        static HookLRMallocAllocator *Open(Engine *engine,
                                           const MemoryPoolOption &option);

        virtual void *pmalloc(size_t size);

        virtual void pfree(void *pointer);

        virtual void set_root(unsigned int index, const void *object);

        virtual void *get_root(unsigned int index) const;

    private:
        void setup_metadata();

        void *alloc_large_sb(size_t size);

        void retire_large_sb(void *sb, size_t size);

        void fill_sb_list(void *sb, size_t count);

        Descriptor *lookup_desc(void *sb);

        void *lookup_sb(Descriptor *desc);

        void fill_cache(size_t sc_idx, TCacheBin *cache);

        void flush_cache(size_t sc_idx, TCacheBin *cache);

        size_t malloc_from_partial(size_t sc_idx, TCacheBin *cache);

        size_t malloc_from_new_sb(size_t sc_idx, TCacheBin *cache);

        uint32_t get_chunk_index(char *superblock, char *block, size_t sc_idx);

        void *alloc_small_sb(size_t size);

        void retire_small_sb(void *sb, size_t size);

        void heap_push_partial(Descriptor *desc);

        Descriptor *heap_pop_partial(ProcHeap *heap);

        int bulk_allocate(void **mem_ptr, size_t alignment, size_t size);

    private:
        bool has_init;
        Metadata *metadata;
        Descriptor *descriptions;
        TCache *caches;
        Engine *engine;
        MemoryPoolOption option;
    };
}

#endif //LIBCRPM_HOOK_LRMALLOC_ALLOCATOR_H
