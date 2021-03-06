cmake_minimum_required(VERSION 3.1)
project(skal LANGUAGES C CXX)
enable_testing()
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

find_package(Boost 1.62 REQUIRED
    COMPONENTS filesystem system thread regex)
find_package(Protobuf REQUIRED)

add_subdirectory(googletest)
config_compiler_and_linker()

protobuf_generate_cpp(SKAL_MSG_PROTO_SRC SKAL_MSG_PROTO_HDR src/skal/msg.proto)

add_library(skal STATIC
    include/skal/alarm.hpp
    include/skal/blob.hpp
    include/skal/cfg.hpp
    include/skal/error.hpp
    include/skal/global.hpp
    include/skal/log.hpp
    include/skal/msg.hpp
    include/skal/net.hpp
    include/skal/queue.hpp
    include/skal/safe-mutex.hpp
    include/skal/semaphore.hpp
    include/skal/skal.hpp
    include/skal/util.hpp
    include/skal/worker.hpp

    src/skal/alarm.cpp
    src/skal/blob.cpp
    src/skal/error.cpp
    src/skal/global.cpp
    src/skal/log.cpp
    src/skal/msg.cpp
    src/skal/net.cpp
    src/skal/queue.cpp
    src/skal/skal.cpp
    src/skal/util.cpp
    src/skal/worker.cpp

    ${SKAL_MSG_PROTO_SRC}
    ${SKAL_MSG_PROTO_HDR})

set_property(TARGET skal PROPERTY CXX_STANDARD 14)
set_property(TARGET skal PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET skal PROPERTY CXX_EXTENSIONS OFF)

# Turn on maximum warnings
if (CMAKE_COMPILER_IS_GNUCXX)
target_compile_options(skal PUBLIC -Wall -Wextra -Werror
    -Wno-unused-parameter -Wno-nonnull-compare)
endif()

target_include_directories(skal
    PUBLIC include
    PRIVATE ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(skal
    Boost::filesystem Boost::system
    ${Protobuf_LIBRARIES} rt)

# Make skal importable from the build directory
export(TARGETS skal FILE skal-targets.cmake)

add_executable(skal-unit-tests
    unit-test/test-util.cpp
    unit-test/test-blob.cpp
    unit-test/test-msg.cpp
    unit-test/test-queue.cpp
    unit-test/test-worker.cpp)

set_property(TARGET skal-unit-tests PROPERTY CXX_STANDARD 14)
set_property(TARGET skal-unit-tests PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET skal-unit-tests PROPERTY CXX_EXTENSIONS OFF)

# Turn on maximum warnings
if (CMAKE_COMPILER_IS_GNUCXX)
target_compile_options(skal-unit-tests PUBLIC -Wall -Wextra -Werror
    -Wno-unused-parameter -Wno-nonnull-compare)
endif()

target_link_libraries(skal-unit-tests skal gtest gmock_main)

add_test(NAME skal-unit-tests COMMAND skal-unit-tests)
