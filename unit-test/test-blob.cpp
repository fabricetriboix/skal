/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "skal-blob.hpp"
#include <cstring>
#include <thread>
#include <chrono>
#include <gtest/gtest.h>

TEST(Blob, MallocBlobConcurrency)
{
    int64_t size_B = 1000;
    std::unique_ptr<skal::blob_proxy_t> proxy;
    EXPECT_NO_THROW(proxy = skal::find_allocator("malloc").create("", size_B));

    std::string id = proxy->id();
    bool thread_ok = true;
    std::thread thread(
            [size_B, &thread_ok, id] ()
            {
                try {
                    std::unique_ptr<skal::blob_proxy_t> proxy2;
                    proxy2 = skal::find_allocator("malloc").open(id);
                    if (proxy2->id() != id) {
                        std::cerr << "THR: invalid proxy id: got '"
                            << proxy2->id() << "' expected '" << id << "'"
                            << std::endl;
                        throw 1;
                    }
                    if (proxy2->size_B() != size_B) {
                        std::cerr << "THR: invalid proxy size: got "
                            << proxy2->size_B() << " expected " << size_B
                            << std::endl;
                        throw 2;
                    }

                    // Let the main thread do its stuff
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                    // The main thread will keep the blob mapped for 10ms
                    auto start = std::chrono::high_resolution_clock::now();
                    skal::blob_proxy_t::scoped_map_t m(*proxy2);
                    auto end = std::chrono::high_resolution_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (diff < std::chrono::milliseconds(7))
                    {
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
        skal::blob_proxy_t::scoped_map_t m(*proxy);
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
        skal::blob_proxy_t::scoped_map_t m(*proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    thread.join();
}
