//
// Created by alogfans on 3/30/21.
//

#ifndef LIBCRPM_CHECKPOINT_H
#define LIBCRPM_CHECKPOINT_H

#include <cstdint>
#include <cstddef>
#include <functional>

#include "internal/pptr.h"
#include "internal/common.h"

#ifdef USE_MPI_EXTENSION
#include <mpi/mpi.h>
#endif

namespace crpm {
    class CheckpointImage {
    public:
        static const uint8_t SS_Initial = 0x0;
        static const uint8_t SS_Main = 0x1;
        static const uint8_t SS_Back = 0x2;
        static const uint8_t SS_Identical = 0x3;

    public:
        static CheckpointImage *Open(void *addr, size_t nr_main_segments,
                                     size_t nr_back_segments, bool initialize);

        static size_t CalculateHeaderSize(size_t nr_main_segments, size_t nr_back_segments);

        static size_t CalculateFileSize(size_t nr_main_segments, size_t nr_back_segments);

        ~CheckpointImage();

        void recovery(uint8_t to_state);

        void set_segment_state_atomic(uint64_t segment_id, uint8_t state);

        uint8_t get_segment_state(uint64_t segment_id);

        void begin_segment_state_update();

        void set_segment_state(uint64_t segment_id, uint8_t state);

        void commit_segment_state_update();

#ifdef USE_MPI_EXTENSION
        void commit_segment_state_update_for_mpi(MPI_Comm comm);
#endif

        uint32_t get_attributes();

        void set_attributes(uint32_t value);

        uint64_t get_back_to_main(uint64_t back_segment_id);

        uint64_t get_main_to_back(uint64_t main_segment_id);

        void bind_back_segment(uint64_t main_segment_id, uint64_t back_segment_id);

        void reset_committed_epoch(uint64_t epoch);

        inline uint64_t get_committed_epoch() {
            return header->committed_epoch;
        }

        inline uint8_t *get_main_segment(uint64_t segment_id) {
            return main_memory + segment_id * kSegmentSize;
        }

        inline uint8_t *get_back_segment(uint64_t segment_id) {
            return back_memory + segment_id * kSegmentSize;
        }

        inline uint8_t *get_main_block(uint64_t block_id) {
            return main_memory + block_id * kBlockSize;
        }

        inline uint8_t *get_back_block(uint64_t block_id) {
            return back_memory + block_id * kBlockSize;
        }

        inline uint64_t get_nr_main_segments() {
            return header->nr_main_segments;
        }

        inline uint64_t get_nr_back_segments() {
            return header->nr_back_segments;
        }

        inline uint8_t *get_start_address() {
            return (uint8_t *) header;
        }

        inline uint8_t *get_end_address() {
            return (uint8_t *) header +
                CalculateFileSize(header->nr_main_segments, header->nr_back_segments);
        }

        inline uint8_t *get_parity_address() {
            return get_back_segment(header->nr_back_segments);
        }

    private:
        CheckpointImage() : has_initialized(false) {}

    private:

        struct Header {
            uint32_t magic;
            uint32_t attributes;
            uint64_t nr_main_segments;
            uint64_t nr_back_segments;
            uint64_t committed_epoch;
            uint64_t media_error;
            // alignas(64) uint8_t segment_state_0[nr_main_segments];
            // alignas(64) uint8_t segment_state_1[nr_main_segments];
            // alignas(64) uint64_t back_to_main[nr_back_segments];
        };

    private:
        bool has_initialized;
        Header *header;
        uint8_t *segment_state[2];
        uint64_t *back_to_main;
        uint64_t *main_to_back; // In DRAM
        uint8_t *header_shadow;
        size_t header_size;

        uint8_t *main_memory;
        uint8_t *back_memory;
        uint8_t *parity_memory;
        int pkey[4];
        bool *segment_state_dirty;
    };
}

#endif //LIBCRPM_CHECKPOINT_H
