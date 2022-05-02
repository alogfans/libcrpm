//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_LRMALLOC_ALLOCATOR_H
#define LIBCRPM_LRMALLOC_ALLOCATOR_H

#include "internal/pptr.h"
#include "internal/common.h"
#include "internal/allocator.h"
#include "lrmalloc_util.h"
#include "internal/engine.h"

namespace crpm {
    struct Anchor;
    struct Descriptor;
    struct ProcHeap;

    enum SuperBlockState {
        SB_INVALID = 0, SB_EMPTY, SB_PARTIAL, SB_FULL
    };

    struct Anchor {
        uint64_t avail: 31, count: 31, state: 2;

        Anchor(uint64_t a = 0) noexcept { (*(uint64_t *) this) = a; }

        Anchor(unsigned a, unsigned c, unsigned s) noexcept:
                avail(a), count(c), state(s) {};
    };

    static_assert(sizeof(Anchor) == sizeof(uint64_t), "invalid anchor size");

    struct Descriptor {
        atomic_pptr<Descriptor> next_free;
        atomic_pptr<Descriptor> next_partial;
        std::atomic<Anchor> anchor;
        pptr<char> superblock;
        pptr<ProcHeap> heap;
        uint32_t block_size;
        uint32_t max_count;

        Descriptor() : block_size(0), max_count(0) {}
    } __attribute__ ((aligned(kCacheLineSize)));

    static_assert(sizeof(Descriptor) == kCacheLineSize, "invalid descriptor size");

    struct ProcHeap {
    public:
        // atomic_pptr<Descriptor> partial_list;
        atomic_stamped_pptr<Descriptor> partial_list;
        size_t size_class_index;
    } __attribute__((aligned(kCacheLineSize)));

    struct Metadata {
        uint32_t magic;
        uint32_t checksum;
        // atomic_pptr<Descriptor> avail_sb;
        atomic_stamped_pptr<Descriptor> avail_sb;
        std::atomic<bool> dirty_flag;
        pptr<Descriptor> desc_list;
        pptr<char> first_sb;
        atomic_pptr<char> bulk_tail;
        uint64_t reserved[2];
        ProcHeap heaps[kMaxSizeClasses];
        pptr<char> roots[kMaxRoots];

        Metadata() : magic(0), dirty_flag(false) {}
    } __attribute__((aligned(kCacheLineSize)));

    class LRMallocAllocator : public Allocator {
    public:
        LRMallocAllocator();

        virtual ~LRMallocAllocator();

        static LRMallocAllocator *Open(Engine *engine,
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

#endif //LIBCRPM_LRMALLOC_ALLOCATOR_H
