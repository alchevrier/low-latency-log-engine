#pragma once
#include <atomic>
#include <llle/concepts.hpp>

namespace llle {
template <typename T>
concept SlotType = FitsCacheLine<T> && IsNotVirtual<T> && IsTriviallyCopyable<T>;


template <SlotType T, std::size_t Capacity> requires IsPowerOfTwo<Capacity>
class SPSCQueue {
public:
    SPSCQueue(): producer_index(0), consumer_index(0) {}

    bool push(const T& item) {
        auto current_producer_index = producer_index.load(std::memory_order_relaxed);
        auto current_consumer_index = consumer_index.load(std::memory_order_acquire);
        if ((current_producer_index - current_consumer_index) >= Capacity) {
            return false; // Queue is full
        }
        buffer_[current_producer_index & (Capacity - 1)] = item;
        producer_index.store(current_producer_index + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        auto current_consumer_index = consumer_index.load(std::memory_order_relaxed);
        auto current_producer_index = producer_index.load(std::memory_order_acquire);
        if (current_consumer_index == current_producer_index) {
            return false; // Queue is empty
        }
        item = buffer_[current_consumer_index & (Capacity - 1)];
        consumer_index.store(current_consumer_index + 1, std::memory_order_release);
        return true;
    }
private:
    alignas(64) T buffer_[Capacity];
    alignas(64) std::atomic<std::size_t> producer_index;
    alignas(64) std::atomic<std::size_t> consumer_index;
};
}