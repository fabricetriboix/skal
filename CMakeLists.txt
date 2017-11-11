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

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(SKAL_MSG_PROTO_SRC SKAL_MSG_PROTO_HDR
    src/skal/msg.proto)

set(SKAL_SRC
    include/skal/alarm.hpp
    include/skal/blob.hpp
    include/skal/cfg.hpp
    include/skal/error.hpp
    include/skal/msg.hpp
    #include/skal/policy.hpp
    #include/skal/skal.hpp
    include/skal/worker.hpp

    include/skal/detail/domain.hpp
    #include/skal/detail/executor.hpp
    include/skal/detail/log.hpp
    include/skal/detail/queue.hpp
    include/skal/detail/safe-mutex.hpp
    include/skal/detail/semaphore.hpp
    include/skal/detail/util.hpp

    src/skal/alarm.cpp
    src/skal/blob.cpp
    src/skal/domain.cpp
    src/skal/error.cpp
    #src/skal/executor.cpp
    src/skal/log.cpp
    src/skal/msg.cpp
    #src/skal/policy.cpp
    src/skal/queue.cpp
    #src/skal/skal.cpp
    src/skal/util.cpp
    src/skal/worker.cpp

    ${SKAL_MSG_PROTO_SRC}
    ${SKAL_MSG_PROTO_HDR}
)

add_library(skal STATIC ${SKAL_SRC})
target_include_directories(skal PUBLIC include)
target_link_libraries(skal Boost::date_time Boost::filesystem Boost::system
    ${Protobuf_LIBRARIES} rt)

set(SKAL_TEST_SRC
    unit-test/test-util.cpp
    unit-test/test-blob.cpp
    unit-test/test-msg.cpp
    unit-test/test-queue.cpp
    )
add_executable(skal-unit-tests ${SKAL_TEST_SRC})
target_link_libraries(skal-unit-tests skal gtest gmock_main)
add_test(NAME skal-unit-tests COMMAND skal-unit-tests)
