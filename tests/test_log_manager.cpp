#include <gtest/gtest.h> 
#include <llle/log_manager.hpp> 
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>

TEST(LogManager, ShouldRecoverFromExistingFile) {    
    // setting up the data inside the file
    mkdir("/tmp/log-directory", 0755);
    int fd = open("/tmp/log-directory/42-0", O_CREAT | O_RDWR, 0644);
    close(fd);

    auto result = llle::LogManager<2 * 1024 * 1024, 4>::create("/tmp/log-directory");
    EXPECT_TRUE(result.has_value());

    unlink("/tmp/log-directory/42-0");
    rmdir("/tmp/log-directory");
}