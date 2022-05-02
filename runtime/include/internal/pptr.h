//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_PPTR_H
#define LIBCRPM_PPTR_H

#include <atomic>

namespace crpm {
    /*
     * Dig 16 least significant bits inside pptr and atomic_pptr to create unique
     * bits pattern. The least bit is sign bit.
     * Note: here we assume addresses on x86-64 don't use most significant 16 bits
     * and thus we are safe to shift an offset left by 16 bits.
     */

    // const uint64_t kPPtrPatternPositive = 0x52b0;
    // const uint64_t kPPtrPatternNegative = 0x52b1;
    const uint64_t kPPtrPatternPositive = 0xb000;
    const uint64_t kPPtrPatternNegative = 0xb100;
    const int kPPtrReservedShift = 16;
    // const int kPPtrReservedMask = (1 << kPPtrReservedShift) - 2;
    const int kPPtrReservedMask = 0xfe00;

    inline bool is_null_pptr(uint64_t off) {
        // return off == kPPtrPatternPositive;
        return (off & ~0xfful) == kPPtrPatternPositive;
    }

    inline bool is_valid_pptr(uint64_t off) {
        return (off & kPPtrReservedMask) == kPPtrPatternPositive;
    }

    /*
     * class pptr<T>
     *
     * Description:
     *  Position independent pointer class for type T, which can be applied via
     *  replacing all T* by pptr<T>.
     *  However, for atomic plain pointers, please replace atomic<T*> by
     *  atomic_pptr<T> as a whole.
     *
     *  The current implementation is off-holder from paper:
     *      Efficient Support of Position Independence on Non-Volatile Memory
     *      Guoyang Chen et al., MICRO'2017
     *
     *  It stores the offset from the instance itself to the object it points to.
     *  The offset can be negative.
     *
     *  Two kinds of constructors and casting to transient pointer are provided,
     *  as well as dereference, arrow access, assignment, and comparison.
     *
     *  TODO: implement pptr as RIV which allows cross-region references, while still
     *  keeping the same interface.
     */
    template<class T>
    class pptr;

    /*
     * class atomic_pptr<T>
     *
     * Description:
     * This is the atomic version of pptr<T>, whose constructor takes a pointer or
     * pptr<T>.
     *
     * The field *off* stores the offset from the instance of atomic_pptr to
     * the object it points to.
     *
     * It defines load, store, compare_exchange_weak, and compare_exchange_strong
     * with the same specification of atomic, and returns and/or takes desired and
     * expected value in type of T*.
     */
    template<class T>
    class atomic_pptr;

    template<class T>
    class atomic_stamped_pptr;

    /*
     * functions to_pptr_off<T> and from_pptr_off<T>
     *
     * Description:
     * These are functions for conversions between pptr<T>::off and T*
     */
    template<class T>
    inline uint64_t to_pptr_off(const T *v, const pptr<T> *p) {
        uint64_t off;
        if (v == nullptr) {
            off = kPPtrPatternPositive;
        } else {
            if (v > reinterpret_cast<const T *>(p)) {
                off = ((uint64_t) v) - ((uint64_t) p);
                off = off << kPPtrReservedShift;
                off = off | kPPtrPatternPositive;
            } else {
                off = ((uint64_t) p) - ((uint64_t) v);
                off = off << kPPtrReservedShift;
                off = off | kPPtrPatternNegative;
            }
        }
        return off;
    }

    template<class T>
    inline T *from_pptr_off(uint64_t off, const pptr<T> *p) {
        if (!is_valid_pptr(off) || is_null_pptr(off)) {
            return nullptr;
        } else {
            // if (off & 1) {   // sign bit is true (negative)
            if (off & 0x0100) { // sign bit is true (negative)
                return (T *) (((int64_t) p) - (off >> kPPtrReservedShift));
            } else {
                return (T *) (((int64_t) p) + (off >> kPPtrReservedShift));
            }
        }
    }

    template<class T>
    inline uint64_t to_pptr_off(const T *v, const atomic_pptr<T> *p) {
        return to_pptr_off(v, reinterpret_cast<const pptr<T> *>(p));
    }

    template<class T>
    inline T *from_pptr_off(uint64_t off, const atomic_pptr<T> *p) {
        return from_pptr_off(off, reinterpret_cast<const pptr<T> *>(p));
    }

    template<class T>
    inline uint64_t to_pptr_off(const T *v, const atomic_stamped_pptr<T> *p) {
        return to_pptr_off(v, reinterpret_cast<const pptr<T> *>(p));
    }

    template<class T>
    inline T *from_pptr_off(uint64_t off, const atomic_stamped_pptr<T> *p) {
        return from_pptr_off(off, reinterpret_cast<const pptr<T> *>(p));
    }

    template<class T>
    class pptr {
    public:
        volatile uint64_t off;

        pptr(T *v = nullptr) noexcept {         // default constructor
            off = to_pptr_off(v, this);
        };

        pptr(const pptr<T> &p) noexcept {       // copy constructor
            T *v = static_cast<T *>(p);
            off = to_pptr_off(v, this);
        }

        template<class F>
        inline operator F *() const {           // cast to transient pointer
            return static_cast<F *>(from_pptr_off(off, this));
        }

        inline T &operator*() {                 // dereference
            return *static_cast<T *>(*this);
        }

        inline T *operator->() {                // arrow
            return static_cast<T *>(*this);
        }

        template<class F>
        inline pptr &operator=(const F *v) {    // assignment
            off = to_pptr_off(v, this);
            return *this;
        }

        inline pptr &operator=(const pptr &p) { // assignment
            T *v = static_cast<T *>(p);
            off = to_pptr_off(v, this);
            return *this;
        }

        inline T &operator[](unsigned int index) {
            return *(static_cast<T *>(*this) + index);
        }

        inline T &operator[](unsigned int index) const {
            return *(static_cast<T *>(*this) + index);
        }

        bool is_null() const {
            return off == kPPtrPatternPositive;
        }

        bool is_valid() const {
            return (off & kPPtrReservedMask) == kPPtrPatternPositive;
        }
    };

    template<class T>
    inline bool operator==(const pptr<T> &lhs, const std::nullptr_t &rhs) {
        return lhs.is_null();
    }

    template<class T>
    inline bool operator==(const pptr<T> &lhs, const pptr<T> &rhs) {
        return (T *) lhs == (T *) rhs;
    }

    template<class T>
    inline bool operator!=(const pptr<T> &lhs, const std::nullptr_t &rhs) {
        return !lhs.is_null();
    }

    template<class T>
    inline bool operator!=(const pptr<T> &lhs, const pptr<T> &rhs) {
        return !((T *) lhs == (T *) rhs);
    }

    template<class T>
    class atomic_pptr {
    public:
        std::atomic<uint64_t> off;

        atomic_pptr(T *v = nullptr) noexcept {              // default constructor
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        atomic_pptr(const pptr<T> &p) noexcept {            // copy constructor
            T *v = static_cast<T *>(p);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        atomic_pptr(const atomic_pptr<T> &p) noexcept {            // copy constructor
            T *v = p.load(std::memory_order_release);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        inline atomic_pptr &operator=(const atomic_pptr &p) { // assignment
            T *v = p.load(std::memory_order_release);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
            return *this;
        }

        template<class F>
        inline atomic_pptr &operator=(const F *v) {         // assignment
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
            return *this;
        }

        T *load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
            uint64_t cur_off = off.load(order);
            return from_pptr_off(cur_off, this);
        }

        void store(T *desired,
                   std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t new_off = to_pptr_off(desired, this);
            off.store(new_off, order);
        }

        bool compare_exchange_weak(T *&expected, T *desired,
                                   std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t old_off = to_pptr_off(expected, this);
            uint64_t new_off = to_pptr_off(desired, this);
            bool ret = off.compare_exchange_weak(old_off, new_off, order);
            if (!ret) {
                if (is_null_pptr(old_off)) {
                    expected = nullptr;
                } else {
                    expected = from_pptr_off(old_off, this);
                }
            }
            return ret;
        }

        bool compare_exchange_strong(T *&expected, T *desired,
                                     std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t old_off = to_pptr_off(expected, this);
            uint64_t new_off = to_pptr_off(desired, this);
            bool ret = off.compare_exchange_strong(old_off, new_off, order);
            if (!ret) {
                if (is_null_pptr(old_off)) {
                    expected = nullptr;
                } else {
                    expected = from_pptr_off(old_off, this);
                }
            }
            return ret;
        }
    };

    template<class T>
    class atomic_stamped_pptr {
    public:
        std::atomic<uint64_t> off;

        atomic_stamped_pptr(T *v = nullptr) noexcept {                      // default constructor
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        atomic_stamped_pptr(const pptr<T> &p) noexcept {                    // copy constructor
            T *v = static_cast<T *>(p);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        atomic_stamped_pptr(const atomic_pptr<T> &p) noexcept {             // copy constructor
            T *v = p.load(std::memory_order_release);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
        }

        inline atomic_stamped_pptr &operator=(const atomic_stamped_pptr &p) { // assignment
            T *v = p.load(std::memory_order_release);
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
            return *this;
        }

        template<class F>
        inline atomic_stamped_pptr &operator=(const F *v) {         // assignment
            uint64_t tmp_off = to_pptr_off(v, this);
            off.store(tmp_off, std::memory_order_relaxed);
            return *this;
        }

        T *load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
            uint64_t cur_off = off.load(order);
            return from_pptr_off(cur_off, this);
        }

        T *
        load(uint8_t &stamp, std::memory_order order = std::memory_order_seq_cst) const noexcept {
            uint64_t cur_off = off.load(order);
            stamp = cur_off & UINT8_MAX;
            return from_pptr_off(cur_off, this);
        }

        void store(T *desired,
                   std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t new_off = to_pptr_off(desired, this);
            off.store(new_off, order);
        }

        bool compare_exchange_weak(T *&expected, T *desired,
                                   uint8_t &expected_stamp,
                                   uint8_t desired_stamp,
                                   std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t old_off = to_pptr_off(expected, this) | expected_stamp;
            uint64_t new_off = to_pptr_off(desired, this) | desired_stamp;
            bool ret = off.compare_exchange_weak(old_off, new_off, order);
            if (!ret) {
                if (is_null_pptr(old_off)) {
                    expected = nullptr;
                } else {
                    expected = from_pptr_off(old_off, this);
                    expected_stamp = old_off & UINT8_MAX;
                }
            }
            return ret;
        }

        bool compare_exchange_strong(T *&expected, T *desired,
                                     uint8_t &expected_stamp,
                                     uint8_t desired_stamp,
                                     std::memory_order order = std::memory_order_seq_cst) noexcept {
            uint64_t old_off = to_pptr_off(expected, this) | expected_stamp;
            uint64_t new_off = to_pptr_off(desired, this) | desired_stamp;
            bool ret = off.compare_exchange_strong(old_off, new_off, order);
            if (!ret) {
                if (is_null_pptr(old_off)) {
                    expected = nullptr;
                } else {
                    expected = from_pptr_off(old_off, this);
                    expected_stamp = old_off & UINT8_MAX;
                }
            }
            return ret;
        }
    };
}

#endif //LIBCRPM_PPTR_H
