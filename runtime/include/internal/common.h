//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_COMMON_H
#define LIBCRPM_COMMON_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <immintrin.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

// #define SFENCE_STAT

#ifndef likely
#define likely(cond)    __glibc_likely(cond)
#endif

#ifndef unlikely
#define unlikely(cond)  __glibc_unlikely(cond)
#endif

#ifndef BLOCK_SHIFT
#define BLOCK_SHIFT 8
#endif // BLOCK_SHIFT

#ifndef REGION_SHIFT
#define REGION_SHIFT 12
#endif // REGION_SHIFT

#ifndef SEGMENT_SHIFT
#define SEGMENT_SHIFT 21
#endif // SEGMENT_SHIFT

#define USE_CLWB
#define USE_AVX512
// #define USE_IDENTICAL_DATA
// #define USE_ENHANCED_ADR
// #define USE_PARITY_CHECK

namespace crpm {
    const static size_t kCacheLineShift = 6;
    const static size_t kCacheLineSize = 1ull << kCacheLineShift;
    const static size_t kCacheLineMask = kCacheLineSize - 1;

    const static size_t kPageShift = 12;
    const static size_t kPageSize = 1ull << kPageShift;
    const static size_t kPageMask = kPageSize - 1;

    const static size_t kHugePageShift = 21;
    const static size_t kHugePageSize = 1ull << kHugePageShift;
    const static size_t kHugePageMask = kHugePageSize - 1;

    // For default engine only
    const static size_t kBlockShift = BLOCK_SHIFT;
    const static size_t kBlockSize = 1ull << kBlockShift;
    const static size_t kBlockMask = kBlockSize - 1;

    const static size_t kSegmentShift = SEGMENT_SHIFT;  // 2MiB
    const static size_t kSegmentSize = 1ull << kSegmentShift;
    const static size_t kSegmentMask = kSegmentSize - 1;

    const static size_t kParitySize = 16ull << 10;

    const static size_t kMaxFlushBlocks = (32ull << 20ull) >> kBlockShift;
    const static size_t kBlocksPerSegment = kSegmentSize / kBlockSize;

    // For mprotect()-based engine only.
    const static size_t kRegionShift = REGION_SHIFT;    // 4KiB
    const static size_t kRegionSize = 1ull << kRegionShift;
    const static size_t kRegionMask = kRegionSize - 1;
    const static size_t kMaxFlushRegions = (32ull << 20ull) >> kRegionShift;

    // Misc
    const static size_t kMinContainerSize = 16ull << 20ull;
    const static size_t kMinAllocateSuperBlockSize = 2ull << 20ull;
    const static uint32_t kMetadataHeaderMagic = 0xc3c3c3c3;
    const static uint32_t kMetadataV1Magic = 0x6f6f0101;
    const static uint32_t kMetadataV2Magic = 0x6f6f0202;
    const static size_t kDescriptorSize = kCacheLineSize;
    const static size_t kMaxRoots = 1024;
    const static size_t kMaxThreads = 256;
    const static uint64_t kPTESoftDirtyBit = 1ull << 55ull;
    const static uint64_t kAddressListCapacity = 256;
    const static uint64_t kSegmentLocks = 1024;
    const static double kShadowMemoryCapacityFactor = 0.20;
    const static uint32_t kAttributeHasSnapshot = 0x10;
    const static uint64_t kNullSegmentIndex = UINT64_MAX;

    extern uint64_t sfence_cnt;

    static inline void Flush(const void *addr) {
#ifndef USE_ENHANCED_ADR
#ifdef USE_CLWB
        asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *) (addr)));
#else
        asm volatile("clflushopt %0" : "+m" (*(volatile char *) (addr)));
#endif //USE_CLWB
#endif //USE_ENHANCED_ADR
    }

    static inline void StoreFence() {
        asm volatile("sfence" : : : "memory");
#ifdef SFENCE_STAT
        sfence_cnt++;
#endif
    }

    static inline void FlushRegion(const void *addr, size_t len) {
        for (uintptr_t ptr = (uintptr_t) addr & ~kCacheLineMask;
             ptr < (uintptr_t) addr + len; ptr += kCacheLineSize) {
            Flush((void *) ptr);
        }
    }

    static inline void PrefetchT0(const void *addr, size_t len) {
        for (uintptr_t ptr = (uintptr_t) addr & ~kCacheLineMask;
             ptr < (uintptr_t) addr + len; ptr += kCacheLineSize) {
            _mm_prefetch((void *) ptr, _MM_HINT_T0);
        }
    }

    static inline void PrefetchT2(const void *addr, size_t len) {
        for (uintptr_t ptr = (uintptr_t) addr & ~kCacheLineMask;
             ptr < (uintptr_t) addr + len; ptr += kCacheLineSize) {
            _mm_prefetch((void *) ptr, _MM_HINT_T2);
        }
    }

    static inline void NTStore(const void *addr, uint64_t value) {
        _mm_stream_si64((long long int *) addr, (long long int) value);
    }

    static inline void NTStore32(const void *addr, uint32_t value) {
        _mm_stream_si32((int *) addr, (int) value);
    }

    static inline void NonTemporalCopy64(void *dst, void *src, const size_t len) {
        assert(!(((uint64_t) dst) & kCacheLineMask));
        assert(!(((uint64_t) src) & kCacheLineMask));
        assert(!(len & kCacheLineMask));
        uintptr_t dst_addr = (uintptr_t) dst;
        uintptr_t src_addr = (uintptr_t) src;
#ifdef USE_AVX512
#pragma unroll
        for (int i = 0; i < len; i += 64) {
            __m512i reg = _mm512_stream_load_si512((__m512i *) src_addr);
            _mm512_stream_si512((__m512i *) dst_addr, reg);
            src_addr += 64;
            dst_addr += 64;
        }
#else
#pragma unroll
        for (int i = 0; i < len; i += 32) {
            __m256i reg = _mm256_stream_load_si256((__m256i *) src_addr);
            _mm256_stream_si256((__m256i *) dst_addr, reg);
            src_addr += 32;
            dst_addr += 32;
        }
#endif
    }

    static inline void NonTemporalCopyWithWriteElimination(void *dst, void *src, const size_t len) {
        assert(!(((uint64_t) dst) & kCacheLineMask));
        assert(!(((uint64_t) src) & kCacheLineMask));
        assert(!(len & kCacheLineMask));
        uintptr_t dst_addr = (uintptr_t) dst;
        uintptr_t src_addr = (uintptr_t) src;
#ifdef USE_AVX512
#pragma unroll
        for (int i = 0; i < len; i += 64) {
            __m512i reg = _mm512_stream_load_si512((__m512i *) src_addr);
            __m512i cmp_reg = _mm512_stream_load_si512((__m512i *) dst_addr);
            if (_mm512_cmpeq_epi64_mask(reg, cmp_reg) != UINT8_MAX) {
                _mm512_stream_si512((__m512i *) dst_addr, reg);
            }
            src_addr += 64;
            dst_addr += 64;
        }
#else
#pragma unroll
        for (int i = 0; i < len; i += 32) {
            __m256i reg = _mm256_stream_load_si256((__m256i *) src_addr);
            __m256i cmp_reg = _mm256_stream_load_si256((__m256i *) dst_addr);
            if (_mm256_cmpeq_epi64_mask(reg, cmp_reg) != 15) {
                _mm256_stream_si256((__m256i *) dst_addr, reg);
            }
            src_addr += 32;
            dst_addr += 32;
        }
#endif
    }

    static inline void NonTemporalCopy256(void *dst, void *src, const size_t len) {
        assert(!(((uint64_t) dst) & kCacheLineMask));
        assert(!(((uint64_t) src) & kCacheLineMask));
        assert(len % 256 == 0);

#ifdef USE_AVX512
        uintptr_t dst_addr = (uintptr_t) dst;
        uintptr_t src_addr = (uintptr_t) src;
#pragma unroll
        for (int i = 0; i < len; i += 256) {
            __m512i regs[4];
            regs[0] = _mm512_stream_load_si512((__m512i *) src_addr);
            regs[1] = _mm512_stream_load_si512((__m512i *) (src_addr + 64));
            regs[2] = _mm512_stream_load_si512((__m512i *) (src_addr + 128));
            regs[3] = _mm512_stream_load_si512((__m512i *) (src_addr + 192));
            _mm512_stream_si512((__m512i *) dst_addr, regs[0]);
            _mm512_stream_si512((__m512i *) (dst_addr + 64), regs[1]);
            _mm512_stream_si512((__m512i *) (dst_addr + 128), regs[2]);
            _mm512_stream_si512((__m512i *) (dst_addr + 192), regs[3]);
            src_addr += 256;
            dst_addr += 256;
        }
#else
        NonTemporalCopy64(dst, src, len);
#endif
    }

    class ThreadInfo {
    public:
        ThreadInfo() noexcept;

        ~ThreadInfo();

        inline unsigned int get_thread_id() const { return id; }

    private:
        unsigned int id;
    };

    extern thread_local ThreadInfo tl_thread_info;

    class AtomicBitSet {
    public:
        const static uint64_t kBitShift = 6;
        const static uint64_t kBitWidth = 64;
        const static uint64_t kBitMask = 63;

        AtomicBitSet() : is_allocated(false) {}

        ~AtomicBitSet() {
            if (is_allocated) {
                free(buf);
                is_allocated = false;
            }
        }

        void allocate(uint64_t nr_bits_);

        inline void set(uint64_t idx, std::memory_order m = std::memory_order_relaxed) {
            assert(idx < nr_bits && is_allocated && buf);
            uint64_t idx_off = idx >> kBitShift;
            uint64_t idx_bit = 1ull << (idx & kBitMask);
            buf[idx_off].fetch_or(idx_bit, m);
        }

        inline bool test(uint64_t idx, std::memory_order m = std::memory_order_relaxed) {
            uint64_t idx_off = idx >> kBitShift;
            uint64_t idx_bit = 1ull << (idx & kBitMask);
            return buf[idx_off].load(m) & idx_bit;
        }

        inline void clear(uint64_t idx) {
            uint64_t idx_off = idx >> kBitShift;
            uint64_t idx_bit = 1ull << (idx & kBitMask);
            buf[idx_off].fetch_and(~idx_bit, std::memory_order_relaxed);
        }

        inline uint64_t test_all(uint64_t idx) {
            uint64_t idx_off = idx >> kBitShift;
            return buf[idx_off].load(std::memory_order_relaxed);
        }

        inline void clear_all(uint64_t idx) {
            uint64_t idx_off = idx >> kBitShift;
            buf[idx_off].store(0, std::memory_order_relaxed);
        }

        inline void clear_region(uint64_t start_idx, uint64_t end_idx) {
            uint64_t start_idx_off = start_idx >> kBitShift;
            uint64_t end_idx_off = end_idx >> kBitShift;
            for (uint64_t idx_off = start_idx_off; idx_off < end_idx_off; idx_off++) {
                buf[idx_off].store(0, std::memory_order_relaxed);
            }
        }

        inline void prefetch() {
            uint64_t nr_bytes = nr_bits / kBitWidth * sizeof(uint64_t);
            if (nr_bits % kBitWidth) {
                nr_bytes += sizeof(uint64_t);
            }
            PrefetchT0(this, sizeof(*this));
            PrefetchT0(buf, nr_bytes);
        }

    private:
        bool is_allocated;
        uint64_t nr_bits;
        std::atomic<uint64_t> *buf;
    };

    struct Barrier {
        std::atomic<int> counter;
        std::atomic<int> flag;
        uint64_t padding[6];
        int local_sense[kMaxThreads];

        Barrier() : counter(0), flag(0) {
            memset(local_sense, 0, sizeof(int) * kMaxThreads);
        }

        void barrier(int N, int thread_id) {
            int &my_local_sense = local_sense[thread_id];
            my_local_sense = 1 - my_local_sense;
            int arrived = counter.fetch_add(1, std::memory_order_relaxed) + 1;
            if (arrived == N) {
                counter.store(0, std::memory_order_relaxed);
                flag.store(my_local_sense, std::memory_order_release);
            } else {
                std::atomic_thread_fence(std::memory_order_acquire);
                while (flag.load(std::memory_order_relaxed) != my_local_sense) {
                    __builtin_ia32_pause();
                }
            }
        }

        void latch_add(int thread_id) {
            int &my_local_sense = local_sense[thread_id];
            flag.store(1 - my_local_sense, std::memory_order_release);
        }

        void latch_wait(int thread_id) {
            int &my_local_sense = local_sense[thread_id];
            my_local_sense = 1 - my_local_sense;
            std::atomic_thread_fence(std::memory_order_acquire);
            while (flag.load(std::memory_order_relaxed) != my_local_sense) {
                __builtin_ia32_pause();
            }
        }
    };

    class GlobalFlush {
    private:
        int fd;
    public:
        static GlobalFlush &Get() {
            static GlobalFlush gf;
            return gf;
        }

        GlobalFlush() {
            fd = open("/dev/global_flush", O_RDWR);
            if (fd < 0) {
                fprintf(stderr, "Failed to open /dev/global_flush. Please install kernel module in /flush directory!\n");
                exit(EXIT_FAILURE);
            }
        }

        ~GlobalFlush() {
            close(fd);
        }

        void flush() const {
            ssize_t ret = write(fd, "", 0);
            if (ret < 0) {
                perror("failed to flush\n");
                exit(EXIT_FAILURE);
            }
        }
    };

    static inline uint64_t ReadTSC() {
        uint64_t rax, rdx, aux;
        asm volatile ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
        return (rdx << 32ull) + rax;
    }

    static inline uint64_t RoundUp(uint64_t a, uint64_t b) {
        uint64_t mod = a % b;
        if (mod) {
            a = a - mod + b;
        }
        return a;
    }

    static inline bool WriteBackAndInvalidate() {
#ifndef USE_ENHANCED_ADR
        GlobalFlush::Get().flush();
#endif // USE_ENHANCED_ADR
        return true;
    }

    static inline void AcquireLock(std::atomic_flag &lock) {
        std::atomic_thread_fence(std::memory_order_acquire);
        while (lock.test_and_set(std::memory_order_relaxed)) {}
    }

    static inline bool TryAcquireLock(std::atomic_flag &lock) {
        std::atomic_thread_fence(std::memory_order_acquire);
        return !lock.test_and_set(std::memory_order_relaxed);
    }

    static inline void ReleaseLock(std::atomic_flag &lock) {
        lock.clear(std::memory_order_release);
    }

    void GetStackAddressSpace(uint64_t &addr_begin, uint64_t &addr_end);

    void BindSingleSocket(int socket = 0);

    uint32_t CalculateCRC32(const void *buf, int len, unsigned int init);
}

#endif //LIBCRPM_COMMON_H
