#include <gtest/gtest.h> 
#include <llle/log_segment.hpp>
#include <unistd.h>

TEST(LogSegment, FactorySuccessfulCreation) {
    auto result = llle::LogSegment<2 * 1024 * 1024>::create("/tmp/test_log_segment");
    EXPECT_TRUE(result.has_value());
    unlink("/tmp/test_log_segment");
}

TEST(LogSegment, FactoryFailureOnNonExistingPath) {
    auto result = llle::LogSegment<2 * 1024 * 1024>::create("/nonexistent_dir/test");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::errc::no_such_file_or_directory);
}

TEST(LogSegment, ShouldAppenUntilSizeIsReached) {
    constexpr std::size_t SEG_SIZE = 2 * 1024 * 1024;
    auto result = llle::LogSegment<SEG_SIZE>::create("/tmp/test_log_segment_bounds");
    auto& segment = result.value();

    std::vector<char> full(SEG_SIZE, 0);
    EXPECT_TRUE(segment.append(full.data(), SEG_SIZE)); // fills completely
    EXPECT_FALSE(segment.append(full.data(), 1));
    EXPECT_EQ(segment.lastWrittenOffset(), SEG_SIZE);

    unlink("/tmp/test_log_segment_bounds");
}