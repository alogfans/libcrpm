//
// Created by Feng Ren on 2021/1/28.
//

#ifndef LIBCRPM_METADATA_H
#define LIBCRPM_METADATA_H

#include <cstdint>
#include <cstddef>
#include <functional>

#include "internal/pptr.h"
#include "internal/common.h"

namespace crpm {
    class MetadataV1 {
    public:
        static const uint32_t kMainData = 0x1;
        static const uint32_t kBackData = 0x2;
        static const uint32_t kAttributeHasSnapshot = 0x10;

        struct Header {
            uint32_t magic;
            uint32_t checksum;
            uint32_t attributes;
            uint32_t consistent_data;
            uint64_t capacity;
            pptr<uint8_t> main_data;
            pptr<uint8_t> back_data;
            uint64_t padding[3];
        };

    public:
        static MetadataV1 *Open(void *addr, size_t capacity, bool create);

        static size_t GetLayoutSize(size_t capacity);

        MetadataV1(Header *header_) : header(header_) {}

        ~MetadataV1() = default;

        void recover_data();

        const Header &get_header() const;

        void set_consistent_data(uint32_t consistent_data);

        void set_attributes(uint32_t attributes);

    private:
        Header *header;
    };
}

#endif //LIBCRPM_LAYOUT_H
