#include <gtest/gtest.h> 
#include <llle/spsc_queue.hpp>

struct alignas(64) TestSlot {
    int qty;
    int price;
    char direction;
};

TEST(SPSCQueue, PushAndPopSingleThread) {
    llle::SPSCQueue<TestSlot, 8> queue;

    TestSlot slot1{100, 5, 'B'};
    TestSlot slot2(50, 2, 'S');

    EXPECT_FALSE(queue.pop(slot1)); // Queue should be empty
    EXPECT_TRUE(queue.push(slot1));

    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(queue.push(slot1)); // Fill the queue
    }
    EXPECT_FALSE(queue.push(slot1)); // Queue should be full

    TestSlot readSlot{};

    for (auto i = 0; i < 8; i++) {
        EXPECT_TRUE(queue.pop(readSlot));
        EXPECT_TRUE(readSlot.qty == 100);
        EXPECT_TRUE(readSlot.price == 5);
        EXPECT_TRUE(readSlot.direction == 'B');
    }
    EXPECT_FALSE(queue.pop(readSlot)); // We have matched the producerIndex and are at the end of the buffer

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(queue.push(slot1)); // Wrapping around and producerIndex should increment back from position 0
    }

    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(queue.pop(readSlot));
        EXPECT_TRUE(readSlot.qty == 100);
        EXPECT_TRUE(readSlot.price == 5);
        EXPECT_TRUE(readSlot.direction == 'B');
    }
    EXPECT_FALSE(queue.pop(readSlot)); // We have matched the producerIndex no further items to consume
}