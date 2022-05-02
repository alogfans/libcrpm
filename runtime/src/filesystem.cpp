//
// Created by Feng Ren on 2021/1/23.
//

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "internal/filesystem.h"
#include "internal/common.h"

namespace crpm {
    bool FileSystem::Exist(const char *path) {
        int rc = access(path, R_OK | W_OK);
        return (rc == 0);
    }

    bool FileSystem::Remove(const char *path) {
        int rc = remove(path);
        return (rc == 0);
    }

    bool FileSystem::create(const char *path, size_t size_, int flags, void *hint_addr) {
        size = size_;
        int tmp_fd, rc;
        void *map_addr;

        if (has_init)
            return false;

        tmp_fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (tmp_fd < 0) {
            perror("open");
            return false;
        }

        off_t off_tail = lseek(tmp_fd, size - 1, SEEK_SET);
        if (off_tail < 0) {
            perror("lseek");
            ::close(tmp_fd);
            return false;
        }

        size_t bytes_written = write(tmp_fd, "", 1);
        if (bytes_written <= 0) {
            perror("write");
            ::close(tmp_fd);
            return false;
        }

        rc = fsync(tmp_fd);
        if (rc) {
            perror("fsync");
            ::close(tmp_fd);
            return false;
        }

        map_addr = mmap(hint_addr, size, PROT_READ | PROT_WRITE,
                        MAP_SHARED_VALIDATE | MAP_SYNC | flags,
                        tmp_fd, 0);

        if (map_addr == MAP_FAILED) {
            perror("mmap");
            ::close(tmp_fd);
            return false;
        }

        if ((flags & MAP_FIXED) && (map_addr != hint_addr)) {
            perror("mmap");
            ::close(tmp_fd);
            return false;
        }

        fd = tmp_fd;
        addr = map_addr;
        file_path = path;
        has_init = true;
        return true;
    }

    bool FileSystem::open(const char *path, int flags, void *hint_addr) {
        int tmp_fd, rc;
        size_t tmp_size;
        void *map_addr;

        if (has_init)
            return false;

        tmp_fd = ::open(path, O_RDWR, S_IRUSR | S_IWUSR);
        if (tmp_fd < 0) {
            perror("open");
            return false;
        }

        off_t off_tail = lseek(tmp_fd, 0, SEEK_END);
        if (off_tail < 0) {
            perror("lseek");
            ::close(tmp_fd);
            return false;
        }

        tmp_size = off_tail;
        map_addr = mmap(hint_addr, tmp_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED_VALIDATE | MAP_SYNC | flags,
                        tmp_fd, 0);

        if (map_addr == MAP_FAILED) {
            perror("mmap");
            ::close(tmp_fd);
            return false;
        }

        if ((flags & MAP_FIXED) && (map_addr != hint_addr)) {
            perror("mmap");
            ::close(tmp_fd);
            return false;
        }

        fd = tmp_fd;
        addr = map_addr;
        size = tmp_size;
        file_path = path;
        has_init = true;
        return true;
    }

    void FileSystem::close() {
        if (has_init) {
            munmap(addr, size);
            ::close(fd);
            has_init = false;
        }
    }

    void FileSystem::clear_poison(size_t offset, size_t length) {
        fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, length);
        fallocate(fd, FALLOC_FL_KEEP_SIZE, offset, length);
    }
}