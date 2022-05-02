//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_FILESYSTEM_H
#define LIBCRPM_FILESYSTEM_H

#include <cstdint>
#include <string>

namespace crpm {
    class FileSystem {
    public:
        static bool Exist(const char *path);

        static bool Remove(const char *path);

    public:
        FileSystem() : has_init(false) {}

        ~FileSystem() { if (has_init) close(); }

        bool create(const char *path, size_t size, int flags = 0, void *hint_addr = nullptr);

        bool open(const char *path, int flags = 0, void *hint_addr = nullptr);

        void clear_poison(size_t offset, size_t length);

        inline void *rel_to_abs(uintptr_t rel) const { return (char *) addr + rel; }

        inline uintptr_t abs_to_rel(void *abs) const { return (uintptr_t) abs - (uintptr_t) addr; }

        inline size_t get_size() const { return size; }

        inline std::string get_file_path() const { return file_path; }

        void close();

    private:
        bool has_init;
        int fd;
        void *addr;
        size_t size;
        std::string file_path;
    };
}

#endif //LIBCRPM_FILESYSTEM_H
