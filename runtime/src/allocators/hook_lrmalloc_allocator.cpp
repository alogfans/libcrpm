//
// Created by Feng Ren on 2021/1/23.
//

#include "internal/allocators/hook_lrmalloc_allocator.h"
#include "internal/common.h"

namespace crpm {
    HookLRMallocAllocator::HookLRMallocAllocator() : has_init(false) {
        SizeClass::Get();
        caches = new TCache[kMaxThreads];
    }

    HookLRMallocAllocator::~HookLRMallocAllocator() {
        delete[]caches;
    }

    HookLRMallocAllocator *HookLRMallocAllocator::Open(Engine *engine,
                                                       const MemoryPoolOption &option) {
        HookLRMallocAllocator *allocator = new HookLRMallocAllocator();
        allocator->engine = engine;
        allocator->option = option;
        if (engine->exist_snapshot()) {
            allocator->metadata = (Metadata *) engine->get_address();
            allocator->descriptions = allocator->metadata->desc_list;
        } else {
            allocator->setup_metadata();
        }
        allocator->has_init = true;
        return allocator;
    }

    void HookLRMallocAllocator::setup_metadata() {
        uint64_t nr_superblocks = engine->get_capacity() / kSuperBlockSize;
        uint64_t offset = RoundUp(sizeof(Metadata), kPageSize);

        metadata = (Metadata *) engine->get_address();
        new(metadata) Metadata();

        for (size_t idx = 0; idx < kMaxSizeClasses; ++idx) {
            ProcHeap &heap = metadata->heaps[idx];
            heap.partial_list.store(nullptr, std::memory_order_relaxed);
            heap.size_class_index = idx;
        }

        for (size_t i = 0; i < kMaxRoots; i++) {
            metadata->roots[i] = nullptr;
        }

        descriptions = (Descriptor *) engine->get_address(offset);
        offset += RoundUp(nr_superblocks * kDescriptorSize, kPageSize);
        metadata->desc_list = descriptions;
        metadata->bulk_tail = (char *) engine->get_address(offset);
        metadata->first_sb = (char *) engine->get_address(offset);

        void *new_sb = nullptr;
        int res = bulk_allocate(&new_sb, kPageSize, kMinAllocateSuperBlockSize);
        if (res) {
            fprintf(stderr, "allocate superblock failed!\n");
            exit(EXIT_FAILURE);
        }

        // We do not use the first superblock
        new_sb = (char *) ((uint64_t) new_sb + kSuperBlockSize);
        fill_sb_list(new_sb, kMinAllocateSuperBlockSize / kSuperBlockSize - 1);
        metadata->magic = kMetadataHeaderMagic;
    }

    void *HookLRMallocAllocator::pmalloc(size_t size) {
        if (unlikely(size > kMaxSize)) {
            size_t rounded_size = RoundUp(size, kSuperBlockSize);
            char *ptr = (char *) alloc_large_sb(rounded_size);
            if (!ptr)
                return nullptr;

            Descriptor *desc = lookup_desc(ptr);
            desc->heap = &metadata->heaps[0];
            desc->block_size = rounded_size;
            desc->max_count = 1;
            desc->superblock = ptr;

            Anchor anchor;
            anchor.avail = 0;
            anchor.count = 0;
            anchor.state = SB_FULL;
            desc->anchor.store(anchor, std::memory_order_release);

            return (void *) ptr;
        }

        size_t sc_idx = SizeClass::Get()->lookup(size);
        TCacheBin *cache = &caches[tl_thread_info.get_thread_id()].bin[sc_idx];
        if (unlikely(cache->get_block_num() == 0))
            fill_cache(sc_idx, cache);
        return cache->pop_block();
    }

    void HookLRMallocAllocator::pfree(void *ptr) {
        if (ptr == nullptr)
            return;

        Descriptor *desc = lookup_desc(ptr);
        size_t sc_idx = desc->heap->size_class_index;
        if (unlikely(!sc_idx)) {
            char *superblock = desc->superblock;
            retire_large_sb(superblock, desc->block_size);
            return;
        }
        TCacheBin *cache = &caches[tl_thread_info.get_thread_id()].bin[sc_idx];
        SizeClassEntry *entry = SizeClass::Get()->get_entry(sc_idx);
        if (cache->get_block_num() >= entry->cache_block_num)
            flush_cache(sc_idx, cache);
        cache->push_block((char *) ptr);
    }

    void *HookLRMallocAllocator::alloc_large_sb(size_t size) {
        // Reuse free large block
        uint8_t old_stamp, new_stamp;
        Descriptor *desc_start = metadata->avail_sb.load(old_stamp);
        if (desc_start) {
            Descriptor *desc = desc_start;
            bool valid = true;
            for (uint64_t i = 1; i < size / kSuperBlockSize; ++i) {
                if (desc->next_free.load() != desc + 1) {
                    valid = false;
                    break;
                }
                desc++;
            }

            if (valid) {
                new_stamp = old_stamp + 1;
                desc = desc->next_free.load();
                bool success = metadata->avail_sb.compare_exchange_strong(
                        desc_start, desc, old_stamp, new_stamp);
                if (success)
                    return lookup_sb(desc_start);
            }
        }

        void *sb_addr = nullptr;
        while (true) {
            int res = bulk_allocate(&sb_addr, kPageSize, size);
            if (res == -ENOMEM)
                return nullptr;
            if (res == 0)
                return sb_addr;
        }
    }

    void HookLRMallocAllocator::retire_large_sb(void *sb, size_t size) {
        assert(size % kSuperBlockSize == 0);
        fill_sb_list(sb, size / kSuperBlockSize);
    }

    void HookLRMallocAllocator::fill_sb_list(void *sb, size_t count) {
        Descriptor *desc_start = lookup_desc((char *) sb);
        Descriptor *desc = desc_start;
        new(desc) Descriptor();
        for (uint64_t i = 1; i < count; i++) {
            desc->next_free.store(desc + 1);
            desc++;
            new(desc) Descriptor();
        }

        uint8_t old_stamp, new_stamp;
        Descriptor *old_head = metadata->avail_sb.load(old_stamp);
        Descriptor *new_head;
        do {
            desc->next_free.store(old_head);
            new_head = desc_start;
            new_stamp = old_stamp + 1;
        } while (!metadata->avail_sb.compare_exchange_weak(
                old_head, new_head, old_stamp, new_stamp));
    }

    Descriptor *HookLRMallocAllocator::lookup_desc(void *sb) {
        uint64_t sb_index = ((char *) sb - (char *) metadata->first_sb) >> kSuperBlockShift;
        return descriptions + sb_index;
    }

    void *HookLRMallocAllocator::lookup_sb(Descriptor *desc) {
        uint64_t desc_index = desc - descriptions;
        char *ret = metadata->first_sb;
        ret += desc_index << kSuperBlockShift;
        return ret;
    }

    void HookLRMallocAllocator::fill_cache(size_t sc_idx, TCacheBin *cache) {
        size_t block_num = malloc_from_partial(sc_idx, cache);
        if (block_num == 0)
            block_num = malloc_from_new_sb(sc_idx, cache);
        SizeClassEntry *entry = SizeClass::Get()->get_entry(sc_idx);
        (void) entry;
        assert(block_num > 0 && block_num <= entry->cache_block_num);
    }

    void HookLRMallocAllocator::flush_cache(size_t sc_idx, TCacheBin *cache) {
        SizeClassEntry *entry = SizeClass::Get()->get_entry(sc_idx);
        const uint32_t sb_size = entry->sb_size;
        const uint32_t block_size = entry->block_size;
        const uint32_t block_num = entry->block_num;

        while (cache->get_block_num() > 0) {
            char *head = cache->peek_block();
            char *tail = head;
            Descriptor *desc = lookup_desc(head);
            char *superblock = desc->superblock;
            uint32_t block_count = 1;
            while (cache->get_block_num() > block_count) {
                char *ptr = *(pptr<char> *) tail;
                if (ptr < superblock || ptr >= superblock + sb_size)
                    break;
                ++block_count;
                tail = ptr;
            }

            char *tail_next = *(pptr<char> *) tail;
            cache->pop_list(tail_next, block_count);
            uint32_t idx = get_chunk_index(superblock, head, sc_idx);

            Anchor old_anchor = desc->anchor.load();
            Anchor new_anchor;
            do {
                char *next = (char *) (superblock + old_anchor.avail * block_size);
                *(pptr<char> *) tail = next;
                new_anchor = old_anchor;
                new_anchor.avail = idx;
                assert(new_anchor.avail < block_num);
                if (old_anchor.state == SB_FULL)
                    new_anchor.state = SB_PARTIAL;
                assert(old_anchor.count < desc->max_count);
                if (old_anchor.count + block_count == desc->max_count) {
                    new_anchor.count = desc->max_count - 1;
                    new_anchor.state = SB_EMPTY;
                } else {
                    new_anchor.count += block_count;
                    assert(new_anchor.count < block_num);
                }
            } while (!desc->anchor.compare_exchange_weak(old_anchor, new_anchor));

            if (old_anchor.state == SB_FULL) {
                if (new_anchor.state == SB_EMPTY) {
                    retire_small_sb(superblock, kSuperBlockSize);
                } else {
                    heap_push_partial(desc);
                }
            }
        }
    }

    size_t HookLRMallocAllocator::malloc_from_partial(size_t sc_idx, TCacheBin *cache) {
        retry:
        ProcHeap *heap = &metadata->heaps[sc_idx];
        Descriptor *desc = heap_pop_partial(heap);
        if (!desc)
            return 0;

        assert(desc->heap != nullptr);
        Anchor old_anchor = desc->anchor.load();
        Anchor new_anchor;
        uint32_t max_count = desc->max_count;
        uint32_t block_size = desc->block_size;
        char *superblock = desc->superblock;

        do {
            if (old_anchor.state == SB_EMPTY) {
                SizeClassEntry *entry = SizeClass::Get()->get_entry(sc_idx);
                retire_small_sb(superblock, entry->sb_size);
                goto retry;
            }
            assert(old_anchor.state == SB_PARTIAL);
            new_anchor = old_anchor;
            new_anchor.count = 0;
            new_anchor.avail = max_count;
            new_anchor.state = SB_FULL;
        } while (!desc->anchor.compare_exchange_weak(old_anchor, new_anchor));

        uint32_t block_take = old_anchor.count;
        uint32_t avail = old_anchor.avail;

        assert(avail < max_count);
        char *block = superblock + avail * block_size;
        assert(cache->get_block_num() == 0);
        cache->push_list(block, block_take);
        return block_take;
    }

    size_t HookLRMallocAllocator::malloc_from_new_sb(size_t sc_idx, TCacheBin *cache) {
        ProcHeap *heap = &metadata->heaps[sc_idx];
        SizeClassEntry *entry = SizeClass::Get()->get_entry(sc_idx);
        const uint32_t block_size = entry->block_size;
        const uint32_t block_num = entry->block_num;

        char *superblock = (char *) alloc_small_sb(entry->sb_size);
        assert(superblock);
        Descriptor *desc = lookup_desc(superblock);
        new(desc) Descriptor();

        desc->heap = heap;
        desc->block_size = block_size;
        desc->max_count = block_num;
        desc->superblock = superblock;

        pptr<char> *block;
        for (uint32_t idx = 0; idx < block_num - 1; ++idx) {
            block = (pptr<char> *) (superblock + idx * block_size);
            char *next = superblock + (idx + 1) * block_size;
            *block = next;
        }

        cache->push_list(superblock, block_num);

        Anchor anchor;
        anchor.avail = block_num;
        anchor.count = 0;
        anchor.state = SB_FULL;
        desc->anchor.store(anchor);

        return block_num;
    }

    uint32_t HookLRMallocAllocator::get_chunk_index(char *superblock, char *block, size_t sc_idx) {
        SizeClassEntry *sc = SizeClass::Get()->get_entry(sc_idx);
        uint32_t sc_block_size = sc->block_size;
        assert(block >= superblock && block < superblock + sc->sb_size);
        uint32_t diff = uint32_t(block - superblock);
        return diff / sc_block_size;
    }

    void *HookLRMallocAllocator::alloc_small_sb(size_t size) {
        assert(size == kSuperBlockSize);
        Descriptor *old_desc = nullptr;
        uint8_t old_stamp;
        old_desc = metadata->avail_sb.load(old_stamp);
        while (true) {
            if (old_desc) {
                Descriptor *new_desc;
                new_desc = old_desc->next_free.load();
                uint8_t new_stamp = old_stamp + 1;
                if (metadata->avail_sb.compare_exchange_strong(
                        old_desc, new_desc, old_stamp, new_stamp)) {
                    return lookup_sb(old_desc);
                }
            } else {
                void *sb_base;
                int ret = bulk_allocate(&sb_base, kPageSize, kMinAllocateSuperBlockSize);
                assert(ret != -ENOMEM);
                if (ret == 0) {
                    fill_sb_list(sb_base, kMinAllocateSuperBlockSize / kSuperBlockSize);
                    old_desc = lookup_desc(sb_base);
                }
            }
        }
    }

    void HookLRMallocAllocator::retire_small_sb(void *sb, size_t size) {
        assert(size == kSuperBlockSize);
        Descriptor *desc = lookup_desc(sb);
        new(desc) Descriptor(); // inject segment fault
        Descriptor *old_head, *new_head;
        uint8_t old_stamp, new_stamp;
        do {
            old_head = metadata->avail_sb.load(old_stamp);
            desc->next_free = old_head;
            new_head = desc;
            new_stamp = old_stamp + 1;
        } while (!metadata->avail_sb.compare_exchange_weak(old_head, new_head, old_stamp,
                                                           new_stamp));
    }

    void HookLRMallocAllocator::heap_push_partial(Descriptor *desc) {
        ProcHeap *heap = desc->heap;
        Descriptor *old_head, *new_head;
        uint8_t old_stamp, new_stamp;
        do {
            old_head = heap->partial_list.load(old_stamp);
            new_head = desc;
            assert(old_head != new_head);
            new_head->next_partial.store(old_head);
            new_stamp = old_stamp + 1;
        } while (!heap->partial_list.compare_exchange_weak(old_head, new_head, old_stamp,
                                                           new_stamp));
    }

    Descriptor *HookLRMallocAllocator::heap_pop_partial(ProcHeap *heap) {
        Descriptor *old_head, *new_head;
        uint8_t old_stamp, new_stamp;
        do {
            old_head = heap->partial_list.load(old_stamp);
            if (!old_head) {
                return nullptr;
            }
            new_head = old_head->next_partial.load();
            new_stamp = old_stamp + 1;
        } while (!heap->partial_list.compare_exchange_weak(old_head, new_head, old_stamp,
                                                           new_stamp));
        return old_head;
    }

    void HookLRMallocAllocator::set_root(unsigned int index, const void *object) {
        assert(has_init && index < kMaxRoots);
        metadata->roots[index] = (char *) object;
    }

    void *HookLRMallocAllocator::get_root(unsigned int index) const {
        assert(has_init && index < kMaxRoots);
        return metadata->roots[index];
    }

    int HookLRMallocAllocator::bulk_allocate(void **mem_ptr, size_t alignment, size_t size) {
        void *res;
        if (((alignment & (~alignment + 1)) != alignment) || (alignment < sizeof(void *)))
            return -EINVAL;

        char *old_bulk_tail = metadata->bulk_tail.load();
        char *new_bulk_tail = old_bulk_tail;
        size_t aln_adj = (size_t) new_bulk_tail & (alignment - 1);
        if (aln_adj != 0)
            new_bulk_tail += (alignment - aln_adj);

        res = new_bulk_tail;
        new_bulk_tail += size;
        uint64_t address_limit = (uint64_t) engine->get_address() +
                                 engine->get_capacity();

        if ((uint64_t) new_bulk_tail > address_limit)
            return -ENOMEM;

        if (metadata->bulk_tail.compare_exchange_strong(old_bulk_tail, new_bulk_tail)) {
            *mem_ptr = res;
            return 0;
        } else {
            return -EAGAIN;
        }
    }
}
