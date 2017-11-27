cmake_minimum_required(VERSION 3.1)
project(skal VERSION 0.1.1 LANGUAGES CXX)
include(GNUInstallDirs)
enable_testing()

find_package(Boost 1.62 REQUIRED COMPONENTS date_time filesystem system)
find_package(Protobuf REQUIRED)

add_subdirectory(googletest)
config_compiler_and_linker()

protobuf_generate_cpp(SKAL_MSG_PROTO_SRC SKAL_MSG_PROTO_HDR src/skal/msg.proto)

add_library(skal STATIC
    include/skal/alarm.hpp
    include/skal/blob.hpp
    include/skal/cfg.hpp
    include/skal/domain.hpp
    include/skal/error.hpp
    include/skal/executor.hpp
    include/skal/log.hpp
    include/skal/msg.hpp
    include/skal/queue.hpp
    include/skal/safe-mutex.hpp
    include/skal/scheduler.hpp
    include/skal/semaphore.hpp
    #include/skal/skal.hpp
    include/skal/util.hpp
    include/skal/worker.hpp

    src/skal/alarm.cpp
    src/skal/blob.cpp
    src/skal/domain.cpp
    src/skal/error.cpp
    src/skal/executor.cpp
    src/skal/log.cpp
    src/skal/msg.cpp
    src/skal/queue.cpp
    src/skal/scheduler.cpp
    #src/skal/skal.cpp
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

target_include_directories(skal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_include_directories(skal
    PRIVATE ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(skal
    Boost::date_time Boost::filesystem Boost::system
    ${Protobuf_LIBRARIES} rt)

install(TARGETS skal EXPORT skal-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Make skal usable for `find_package()`
install(EXPORT skal-targets DESTINATION share/skal/cmake NAMESPACE skal::)

# Make skal importable from the build directory
export(TARGETS skal FILE skal-targets.cmake)

add_executable(skal-unit-tests
    unit-test/test-util.cpp
    unit-test/test-blob.cpp
    unit-test/test-msg.cpp
    unit-test/test-queue.cpp
    unit-test/test-worker.cpp
    unit-test/test-executor.cpp)

set_property(TARGET skal-unit-tests PROPERTY CXX_STANDARD 14)
set_property(TARGET skal-unit-tests PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET skal-unit-tests PROPERTY CXX_EXTENSIONS OFF)

# Turn on maximum warnings
if (CMAKE_COMPILER_IS_GNUCXX)
target_compile_options(skal-unit-tests PUBLIC -Wall -Wextra -Werror
    -Wno-unused-parameter -Wno-nonnull-compare)
endif()

target_include_directories(skal-unit-tests PRIVATE include)

target_link_libraries(skal-unit-tests skal gtest gmock_main)

add_test(NAME skal-unit-tests COMMAND skal-unit-tests)
