//
// Created by Feng Ren on 2021/1/28.
//

#include <thread>
#include <sys/mman.h>
#include "internal/common.h"
#include "internal/metadata.h"

namespace crpm {
    MetadataV1 *MetadataV1::Open(void *addr, size_t capacity, bool create) {
        Header *header = (Header *) addr;
        if (!header) {
            fprintf(stderr, "addr is nullptr\n");
            return nullptr;
        }

        if (create) {
            if (capacity == 0) {
                fprintf(stderr, "capacity is invalid\n");
                return nullptr;
            }

            memset(header, 0, sizeof(Header));
            header->magic = kMetadataV1Magic;
            header->capacity = capacity;
            header->consistent_data = kBackData;
            uint8_t *pos = (uint8_t *) addr + kHugePageSize;
            header->main_data = pos;
            pos += capacity;
            header->back_data = pos;
            FlushRegion(header, sizeof(Header));
            StoreFence();
            uint32_t checksum = CalculateCRC32(header, sizeof(Header), 0);
            NTStore32(&header->checksum, checksum);
            StoreFence();
        } else {
            if (header->magic != kMetadataV1Magic) {
                fprintf(stderr, "magic number mismatch\n");
                return nullptr;
            }
        }
        return new MetadataV1(header);
    }

    size_t MetadataV1::GetLayoutSize(size_t capacity) {
        return kHugePageSize + 2 * capacity;
    }

    void MetadataV1::recover_data() {
        if (header->consistent_data == kMainData) {
            NonTemporalCopyWithWriteElimination(header->back_data, header->main_data,
                                                header->capacity);
            set_consistent_data(kBackData);
        } else {
            NonTemporalCopyWithWriteElimination(header->main_data, header->back_data,
                                                header->capacity);
        }
    }

    const MetadataV1::Header &MetadataV1::get_header() const {
        assert(header);
        return *header;
    }

    void MetadataV1::set_consistent_data(uint32_t consistent_data) {
        assert(header);
        NTStore32(&header->consistent_data, consistent_data);
        StoreFence();
    }

    void MetadataV1::set_attributes(uint32_t attributes) {
        assert(header);
        NTStore32(&header->attributes, attributes);
        StoreFence();
    }
}