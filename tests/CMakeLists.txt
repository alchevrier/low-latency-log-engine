add_executable(tests
    test_concepts.cpp
    test_spsc_queue.cpp
    test_log_segment.cpp
    test_log_manager.cpp
)
target_link_libraries(tests PRIVATE llle GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tests)