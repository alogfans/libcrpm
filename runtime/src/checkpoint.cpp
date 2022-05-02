//
// Created by alogfans on 3/30/21.
//

#include <sys/mman.h>
#include <thread>
#include "internal/checkpoint.h"

namespace crpm {
    thread_local bool segment_state_update = false;

    size_t CheckpointImage::CalculateHeaderSize(size_t nr_main_segments, size_t nr_back_segments) {
        size_t header_size =
                RoundUp(sizeof(Header), kCacheLineSize) +
                RoundUp(sizeof(uint8_t) * nr_main_segments, kCacheLineSize) * 2 +
                RoundUp(sizeof(uint64_t) * nr_back_segments, kCacheLineSize);
        return RoundUp(header_size, kHugePageSize) * 2;
    }

    size_t CheckpointImage::CalculateFileSize(size_t nr_main_segments, size_t nr_back_segments) {
        return CalculateHeaderSize(nr_main_segments, nr_back_segments) +
               (nr_main_segments + nr_back_segments) * kSegmentSize +
               (nr_main_segments + nr_back_segments) * kParitySize;
    }

    CheckpointImage *CheckpointImage::Open(void *addr, size_t nr_main_segments,
                                           size_t nr_back_segments, bool initialize) {
        Header *header = (Header *) addr;
        if (!header) {
            fprintf(stderr, "addr is nullptr\n");
            return nullptr;
        }

        CheckpointImage *obj = new CheckpointImage();

        if (initialize) {
            uint64_t offset = RoundUp(sizeof(Header), kCacheLineSize);

            memset(header, 0, sizeof(Header));
            header->magic = kMetadataV2Magic;
            header->nr_main_segments = nr_main_segments;
            header->nr_back_segments = nr_back_segments;

            obj->header = header;
            obj->segment_state[0] = (uint8_t *) addr + offset;
            offset += RoundUp(sizeof(uint8_t) * nr_main_segments, kCacheLineSize);
            obj->segment_state[1] = (uint8_t *) addr + offset;
            offset += RoundUp(sizeof(uint8_t) * nr_main_segments, kCacheLineSize);
            obj->back_to_main = (uint64_t *) ((uintptr_t) addr + offset);
            offset += RoundUp(sizeof(uint64_t) * nr_back_segments, kCacheLineSize);
            offset = RoundUp(offset, kHugePageSize);
            obj->header_size = offset;
            obj->header_shadow = (uint8_t *) addr + offset;
            offset *= 2;
            obj->main_memory = (uint8_t *) addr + offset;
            offset += nr_main_segments * kSegmentSize;
            obj->back_memory = (uint8_t *) addr + offset;
            offset += nr_back_segments * kSegmentSize;
            obj->parity_memory = (uint8_t *) addr + offset;

            memset(obj->segment_state[0], SS_Initial, nr_main_segments);
            memset(obj->segment_state[1], SS_Initial, nr_main_segments);
            // memset(obj->back_to_main, UINT8_MAX, nr_back_segments * sizeof(uint64_t));
            for (int i = 0; i < nr_back_segments; ++i) {
                obj->back_to_main[i] = i;
            }
            FlushRegion(header, CalculateHeaderSize(nr_main_segments, nr_back_segments));
            StoreFence();
        } else {
            if (header->magic != kMetadataV2Magic) {
                fprintf(stderr, "magic number mismatch\n");
                return nullptr;
            }

            nr_main_segments = header->nr_main_segments;
            nr_back_segments = header->nr_back_segments;
            uint64_t offset = RoundUp(sizeof(Header), kCacheLineSize);
            obj->header = header;
            obj->segment_state[0] = (uint8_t *) addr + offset;
            offset += RoundUp(sizeof(uint8_t) * nr_main_segments, kCacheLineSize);
            obj->segment_state[1] = (uint8_t *) addr + offset;
            offset += RoundUp(sizeof(uint8_t) * nr_main_segments, kCacheLineSize);
            obj->back_to_main = (uint64_t *) ((uintptr_t) addr + offset);
            offset += RoundUp(sizeof(uint64_t) * nr_back_segments, kCacheLineSize);
            offset = RoundUp(offset, kHugePageSize);
            obj->header_size = offset;
            obj->header_shadow = (uint8_t *) addr + offset;
            offset *= 2;
            obj->main_memory = (uint8_t *) addr + offset;
            offset += nr_main_segments * kSegmentSize;
            obj->back_memory = (uint8_t *) addr + offset;
            offset += nr_back_segments * kSegmentSize;
            obj->parity_memory = (uint8_t *) addr + offset;
        }

        obj->segment_state_dirty = (bool *) malloc(nr_main_segments);
        obj->main_to_back = (uint64_t *) malloc(nr_main_segments * sizeof(uint64_t));
        for (uint64_t i = 0; i < header->nr_main_segments; ++i) {
            obj->segment_state_dirty[i] = false;
            obj->main_to_back[i] = kNullSegmentIndex;
        }

        for (uint64_t i = 0; i < header->nr_back_segments; ++i) {
            if (obj->back_to_main[i] != kNullSegmentIndex) {
                obj->main_to_back[obj->back_to_main[i]] = i;
            }
        }

        obj->has_initialized = true;
        return obj;
    }

    CheckpointImage::~CheckpointImage() {
        if (has_initialized) {
            free(segment_state_dirty);
            free(main_to_back);
            has_initialized = false;
        }
    }

    void CheckpointImage::recovery(uint8_t to_state) {
#ifdef USE_MULTI_THREADS_RECOVERY
        const static size_t kThreads = 8;
        std::thread threads[kThreads];
        for (int tid = 0; tid < kThreads; tid++) {
            threads[tid] = std::thread([=]{
                uint8_t bi_epoch = header->committed_epoch & 1;
                uint8_t *state = segment_state[bi_epoch];
                for (uint64_t back_id = tid; back_id < header->nr_back_segments; back_id += kThreads) {
                    uint64_t main_id = get_back_to_main(back_id);
                    if (main_id == kNullSegmentIndex || state[main_id] == SS_Initial) {
                        continue;
                    }
                    uint8_t *main_segment = get_main_segment(main_id);
                    uint8_t *back_segment = get_back_segment(back_id);
                    if (state[main_id] == SS_Main) {
                        NonTemporalCopyWithWriteElimination(back_segment, main_segment, kSegmentSize);
                    } else if (state[main_id] == SS_Back) {
                        NonTemporalCopyWithWriteElimination(main_segment, back_segment, kSegmentSize);
                    }
                }
            });
        }
        begin_segment_state_update();
        uint8_t bi_epoch = header->committed_epoch & 1;
        uint8_t *state = segment_state[bi_epoch];
        for (uint64_t back_id = 0; back_id < header->nr_back_segments; ++back_id) {
            uint64_t main_id = get_back_to_main(back_id);
            if (main_id == kNullSegmentIndex || state[main_id] == SS_Initial) {
                continue;
            }
            if (to_state != state[main_id]) {
                set_segment_state(main_id, to_state);
            }
        }
        for (int i = 0; i < kThreads; i++) {
            threads[i].join();
        }
        commit_segment_state_update();
#else
        uint64_t traffic = 0;
        begin_segment_state_update();
        uint8_t bi_epoch = header->committed_epoch & 1;
        uint8_t *state = segment_state[bi_epoch];
        for (uint64_t back_id = 0; back_id < header->nr_back_segments; ++back_id) {
            uint64_t main_id = get_back_to_main(back_id);
            if (main_id == kNullSegmentIndex || state[main_id] == SS_Initial) {
                continue;
            }
            uint8_t *main_segment = get_main_segment(main_id);
            uint8_t *back_segment = get_back_segment(back_id);
            if (state[main_id] == SS_Main) {
                NonTemporalCopyWithWriteElimination(back_segment, main_segment, kSegmentSize);
                traffic += kSegmentSize;
            } else if (state[main_id] == SS_Back) {
                NonTemporalCopyWithWriteElimination(main_segment, back_segment, kSegmentSize);
                traffic += kSegmentSize;
            }
            if (to_state != state[main_id]) {
                set_segment_state(main_id, to_state);
            }
        }
        commit_segment_state_update();
#endif
    }

    void CheckpointImage::set_segment_state_atomic(uint64_t segment_id, uint8_t state) {
        uint32_t bi_epoch = header->committed_epoch & 1;
        uint8_t *slot = &segment_state[bi_epoch][segment_id];
        *slot = state;
        Flush(slot);
        StoreFence();
        slot = &segment_state[1 - bi_epoch][segment_id];
        *slot = state;
        Flush(slot);
    }

    uint8_t CheckpointImage::get_segment_state(uint64_t segment_id) {
        uint32_t bi_epoch = header->committed_epoch & 1;
        uint8_t *slot = &segment_state[bi_epoch][segment_id];
        return *slot;
    }

    void CheckpointImage::begin_segment_state_update() {
        segment_state_update = true;
    }

    void CheckpointImage::set_segment_state(uint64_t segment_id, uint8_t state) {
        if (!segment_state_update) {
            fprintf(stderr, "illegal instruction\n");
            exit(EXIT_FAILURE);
        }
        const static size_t kRecordsPerCacheLine = kCacheLineSize / sizeof(uint8_t);
        uint32_t bi_epoch = header->committed_epoch & 1;
        uint8_t *slot = &segment_state[1 - bi_epoch][segment_id];
        if (*slot != state) {
            *slot = state;
            segment_state_dirty[segment_id / kRecordsPerCacheLine] = true;
        }
    }

    void CheckpointImage::commit_segment_state_update() {
        uint32_t next_epoch = header->committed_epoch + 1;
        uint8_t bi_epoch = next_epoch & 1;
        const static size_t kRecordsPerCacheLine = kCacheLineSize / sizeof(uint8_t);
        for (uint64_t i = 0; i < header->nr_main_segments; i += kRecordsPerCacheLine) {
            if (segment_state_dirty[i / kRecordsPerCacheLine]) {
                Flush(&segment_state[bi_epoch][i]);
            }
        }
        StoreFence();
        NTStore(&header->committed_epoch, next_epoch);
        StoreFence();
        for (uint64_t i = 0; i < header->nr_main_segments; i += kRecordsPerCacheLine) {
            if (segment_state_dirty[i / kRecordsPerCacheLine]) {
                NonTemporalCopy64(&segment_state[1 - bi_epoch][i],
                                  &segment_state[bi_epoch][i],
                                  kCacheLineSize);
                segment_state_dirty[i / kRecordsPerCacheLine] = false;
            }
        }
        segment_state_update = false;
    }

#ifdef USE_MPI_EXTENSION
    void CheckpointImage::commit_segment_state_update_for_mpi(MPI_Comm comm) {
        uint32_t next_epoch = header->committed_epoch + 1;
        uint8_t bi_epoch = next_epoch & 1;
        const static size_t kRecordsPerCacheLine = kCacheLineSize / sizeof(uint8_t);
        for (uint64_t i = 0; i < header->nr_main_segments; i += kRecordsPerCacheLine) {
            if (segment_state_dirty[i / kRecordsPerCacheLine]) {
                Flush(&segment_state[bi_epoch][i]);
            }
        }
        StoreFence();
        NTStore(&header->committed_epoch, next_epoch);
        StoreFence();
        MPI_Barrier(comm); // Inter-process synchronization
        for (uint64_t i = 0; i < header->nr_main_segments; i += kRecordsPerCacheLine) {
            if (segment_state_dirty[i / kRecordsPerCacheLine]) {
                NonTemporalCopy64(&segment_state[1 - bi_epoch][i],
                                  &segment_state[bi_epoch][i],
                                  kCacheLineSize);
                segment_state_dirty[i / kRecordsPerCacheLine] = false;
            }
        }
        segment_state_update = false;
    }
#endif //USE_MPI_EXTENSION

    uint32_t CheckpointImage::get_attributes() {
        return header->attributes;
    }

    void CheckpointImage::set_attributes(uint32_t value) {
        NTStore32(&header->attributes, value);
        StoreFence();
    }

    void CheckpointImage::reset_committed_epoch(uint64_t epoch) {
        NTStore(&header->committed_epoch, epoch);
        StoreFence();
    }

    uint64_t CheckpointImage::get_back_to_main(uint64_t back_segment_id) {
        return back_to_main[back_segment_id];
    }

    uint64_t CheckpointImage::get_main_to_back(uint64_t main_segment_id) {
        return main_to_back[main_segment_id];
    }

    void CheckpointImage::bind_back_segment(uint64_t main_segment_id, uint64_t back_segment_id) {
        uint64_t old_main_segment_id = back_to_main[back_segment_id];
        if (old_main_segment_id != kNullSegmentIndex) {
            main_to_back[old_main_segment_id] = kNullSegmentIndex;
        }
        NTStore(&back_to_main[back_segment_id], main_segment_id);
        main_to_back[main_segment_id] = back_segment_id;
        StoreFence();
    }
}