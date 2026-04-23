#pragma once
#include <atomic>
#include <sys/mman.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <expected>
#include <string_view>
#include <system_error>
#include <llle/concepts.hpp>

namespace llle {
    template <std::size_t Size> requires IsHugePageAligned<Size>
    class LogSegment {
        public: 
            LogSegment(const LogSegment&) = delete;
            LogSegment& operator=(const LogSegment&) = delete;

            LogSegment(LogSegment&& other) noexcept
                : fd(other.fd), map(other.map), written_offset(other.written_offset.load(std::memory_order_relaxed)) {
                other.fd = -1;
                other.map = nullptr;
            }

            LogSegment& operator=(LogSegment&& other) noexcept {
                if (this != &other) {
                    if (map) munmap(map, Size);
                    if (fd != -1) close(fd);
                    fd = other.fd;
                    map = other.map;
                    written_offset.store(other.written_offset.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    other.fd = -1;
                    other.map = nullptr;
                }
                return *this;
            }

            ~LogSegment() {
                if (map) munmap(map, Size);
                if (fd != -1) close(fd);
            }

            static std::expected<LogSegment, std::errc> create(std::string_view path) {
                // 1. Try to open/create the file
                int fd = open(path.data(), O_CREAT | O_RDWR, 0644);
                if (fd == -1) return std::unexpected(std::errc(errno));
                
                // 2. Set the file size
                if (ftruncate(fd, Size) == -1) {
                    close(fd);
                    return std::unexpected(std::errc(errno));
                }

                // 3. Map it
                void* ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (ptr == MAP_FAILED) {
                    close(fd);
                    return std::unexpected(std::errc(errno));
                }

                madvise(ptr, Size, MADV_HUGEPAGE); // advisory, ignore failure

                // 4. Construct the object only on success
                return LogSegment(fd, static_cast<char*>(ptr));
            }

            bool append(const void* data, std::size_t len) {
                auto current_position = written_offset.load(std::memory_order_relaxed);
                if (current_position + len > Size) return false;
                std::memcpy(map + current_position, data, len);
                written_offset.store(current_position + len, std::memory_order_release);
                return true;
            }

            std::size_t lastWrittenOffset() const {
                return written_offset.load(std::memory_order_acquire);
            }

        private:
            LogSegment(int fd_, char* map_): fd(fd_), map(map_), written_offset(0) { }


            int fd;
            char* map;
            alignas(64) std::atomic<std::size_t> written_offset;
    };
}