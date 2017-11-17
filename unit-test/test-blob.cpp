/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/blob.hpp>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <gtest/gtest.h>

TEST(Blob, MallocBlobConcurrency)
{
    int64_t size_B = 1000;
    skal::blob_proxy_t proxy = skal::create_blob("malloc", "", size_B);
    std::string id = proxy.id();
    bool thread_ok = true;
    std::thread thread(
            [size_B, &thread_ok, id] ()
            {
                try {
                    skal::blob_proxy_t proxy2 = skal::open_blob("malloc", id);
                    if (proxy2.id() != id) {
                        std::cerr << "THR: invalid proxy id: got '"
                            << proxy2.id() << "' expected '" << id << "'"
                            << std::endl;
                        throw 1;
                    }
                    if (proxy2.size_B() != size_B) {
                        std::cerr << "THR: invalid proxy size: got "
                            << proxy2.size_B() << " expected " << size_B
                            << std::endl;
                        throw 2;
                    }

                    // Let the main thread do its stuff
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                    // The main thread will keep the blob mapped for 10ms
                    auto start = std::chrono::high_resolution_clock::now();
                    skal::blob_proxy_t::scoped_map_t m(proxy2);
                    auto end = std::chrono::high_resolution_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (diff < std::chrono::milliseconds(7)) {
                        std::cerr << "THR: blocked for less than 10ms: "
                            << diff.count() << std::endl;
                        throw 3;
                    }
                    char* ptr = static_cast<char*>(m.mem());
                    if (std::strcmp(ptr, "Hello, World!") != 0) {
                        std::cerr << "THR: blob has bad content" << std::endl;
                        throw 4;
                    }
                    std::strcpy(ptr, "How are you?");
                } catch (...) {
                    thread_ok = false;
                }
            });

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");

        // Keep the blob mapped for 10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_TRUE(thread_ok);
    }

    // Let the thread do its stuff
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(thread_ok);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    thread.join();
}

TEST(Blob, CopyProxy)
{
    int64_t size_B = 1000;
    skal::blob_proxy_t proxy = skal::create_blob("malloc", "", size_B);
    bool thread_ok = true;
    std::thread thread(
            [&thread_ok, &proxy] ()
            {
                try {
                    skal::blob_proxy_t proxy2 = proxy;

                    // Let the main thread do its stuff
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                    // The main thread will keep the blob mapped for 10ms
                    auto start = std::chrono::high_resolution_clock::now();
                    skal::blob_proxy_t::scoped_map_t m(proxy2);
                    auto end = std::chrono::high_resolution_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (diff < std::chrono::milliseconds(7)) {
                        std::cerr << "THR: blocked for less than 10ms: "
                            << diff.count() << std::endl;
                        throw 3;
                    }
                    char* ptr = static_cast<char*>(m.mem());
                    if (std::strcmp(ptr, "Hello, World!") != 0) {
                        std::cerr << "THR: blob has bad content" << std::endl;
                        throw 4;
                    }
                    std::strcpy(ptr, "How are you?");
                } catch (...) {
                    thread_ok = false;
                }
            });

    // Let the thread make a copy of the proxy
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");

        // Keep the blob mapped for 10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_TRUE(thread_ok); // FIXME: there is sometimes a failure here
    }

    // Let the thread do its stuff
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(thread_ok);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    thread.join();
}

#ifndef _WIN32
TEST(Blob, ShmBlobConcurrency)
{
    int64_t size_B = 1000;
    skal::blob_proxy_t proxy = skal::create_blob("shm", "test-blob", size_B);
    std::string id = proxy.id();
    pid_t pid = fork();
    ASSERT_NE(-1, pid);
    if (pid == 0) { // I'm the child
        // Let the main process do its stuff
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        try {
            skal::blob_proxy_t proxy2 = skal::open_blob("shm", id);
            if (proxy2.id() != id) {
                std::cerr << "CHILD: invalid proxy id: got '" << proxy2.id()
                    << "' expected '" << id << "'" << std::endl;
                throw 1;
            }
            if (proxy2.size_B() != size_B) {
                std::cerr << "CHILD: invalid proxy size: got "
                    << proxy2.size_B() << " expected " << size_B << std::endl;
                throw 2;
            }

            {
                // The main process will keep the blob mapped for 10ms
                auto start = std::chrono::high_resolution_clock::now();
                skal::blob_proxy_t::scoped_map_t m(proxy2);
                auto end = std::chrono::high_resolution_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (diff < std::chrono::milliseconds(7)) {
                    std::cerr << "CHILD: blocked for less than 10ms: "
                        << diff.count() << std::endl;
                    throw 3;
                }
                char* ptr = static_cast<char*>(m.mem());
                if (std::strcmp(ptr, "Hello, World!") != 0) {
                    std::cerr << "CHILD: blob has bad content" << std::endl;
                    throw 4;
                }
                std::strcpy(ptr, "How are you?");
            }
        } catch (...) {
            std::abort();
        }
        std::exit(0);
    } // child process

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");

        // Keep the blob mapped for 10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Let the child process do its stuff
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    int wstatus;
    pid_t ret = ::waitpid(pid, &wstatus, WNOHANG);
    ASSERT_EQ(ret, pid);
    int exit_status = WEXITSTATUS(wstatus);
    ASSERT_EQ(0, exit_status) << "Child returned in error";
}
#endif
