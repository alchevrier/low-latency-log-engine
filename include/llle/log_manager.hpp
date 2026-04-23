#pragma once
#include <expected>
#include <system_error>
#include <cstdint>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <dirent.h>
#include <llle/concepts.hpp>
#include <llle/log_segment.hpp>

namespace llle {
    template<std::size_t SegmentSize, std::size_t Capacity> requires IsHugePageAligned<SegmentSize> && IsPowerOfTwo<Capacity>
    class LogManager {
        public:
            LogManager(const LogManager&) = delete;
            LogManager& operator=(const LogManager&) = delete;

            LogManager(LogManager&& other) noexcept {
                for (std::size_t i = 0; i < Capacity; ++i) {
                    slots[i] = std::move(other.slots[i]);
                }
            }

            static std::expected<LogManager, std::errc> create(std::string_view log_dir) {
                auto dir = opendir(log_dir.data());
                if (dir == nullptr) return std::unexpected(std::errc(errno));
                dirent* entry;
                LogManager mgr{};
                while ((entry = readdir(dir)) != nullptr) {
                    if (std::string_view(entry->d_name) == "." || std::string_view(entry->d_name) == "..") continue;
                    uint64_t partition_id{};
                    auto [ptr, ec] = std::from_chars(entry->d_name, entry->d_name + std::strlen(entry->d_name), partition_id);
                    if (ec != std::errc{}) {
                        closedir(dir);
                        return std::unexpected(ec);
                    }
                    auto index = partition_id & (Capacity - 1);
                    if (mgr.slots[index].segment.has_value()) {
                        closedir(dir);
                        return std::unexpected(std::errc::file_exists); // collision
                    }
                    std::string full_path = std::string(log_dir) + "/" + entry->d_name;
                    auto seg_result = llle::LogSegment<SegmentSize>::create(full_path);
                    if (!seg_result.has_value()) {
                        closedir(dir);
                        return std::unexpected(seg_result.error());
                    }
                    mgr.slots[index] = {partition_id, std::move(seg_result.value())};
                }
                closedir(dir);
                return mgr;
            }
            
            bool append(uint64_t partition_id, const void* data, std::size_t len) {
                auto& slot = slots[partition_id & (Capacity - 1)];
                if (!slot.segment.has_value() || slot.id != partition_id) return false;
                return slot.segment->append(data, len);
            }

        private:
            LogManager() = default;

            struct Slot {
                uint64_t id;
                std::optional<LogSegment<SegmentSize>> segment;
            };

            Slot slots[Capacity];
    };
}