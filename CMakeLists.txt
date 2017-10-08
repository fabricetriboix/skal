cmake_minimum_required(VERSION 3.1)
project(skal)
enable_testing()

# The following are no good, they add "-std=gnu++14" on the command line
# instead of "-std=c++14"
#set(CMAKE_CXX_STANDARD 14)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror -std=c++14
    -Wno-unused-parameter -Wno-nonnull-compare)

add_subdirectory(googletest)
config_compiler_and_linker()

find_package(Boost 1.62 REQUIRED COMPONENTS date_time filesystem system)

set(LIBSKAL_SRC
    lib/skal/include/skal-cfg.hpp
    lib/skal/src/skal-log.cpp
    lib/skal/include/skal-error.hpp
    lib/skal/src/skal-error.cpp
    lib/skal/include/skal-blob.hpp
    lib/skal/src/skal-blob.cpp
    lib/skal/include/skal-alarm.hpp
    lib/skal/src/skal-alarm.cpp
    lib/skal/include/detail/safe-mutex.hpp
    lib/skal/include/detail/skal-log.hpp
    lib/skal/include/detail/skal-thread-specific.hpp)

add_library(skal STATIC ${LIBSKAL_SRC})
target_include_directories(skal PUBLIC lib/skal/include)
target_link_libraries(skal Boost::date_time Boost::filesystem Boost::system rt)

set(LIBSKAL_TEST_SRC
    unit-test/test-blob.cpp)
add_executable(skal-unit-tests ${LIBSKAL_TEST_SRC})
target_link_libraries(skal-unit-tests skal gtest gmock_main)
add_test(NAME skal-unit-tests COMMAND skal-unit-tests)
