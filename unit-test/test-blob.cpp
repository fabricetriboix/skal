/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/blob.hpp>
#include <skal/semaphore.hpp>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <gtest/gtest.h>

TEST(Blob, MallocBlobConcurrency)
{
    int64_t size_B = 1000;
    skal::blob_proxy_t proxy = skal::create_blob("malloc", "", size_B);
    std::string id = proxy.id();
    bool thread_ok = true;
    ft::semaphore_t sem1;
    ft::semaphore_t sem2;
    std::thread thread(
            [size_B, &thread_ok, &sem1, &sem2, id] ()
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
                    bool taken = sem2.take(1s);
                    if (!taken) {
                        std::cerr
                            << "THR: main thread failed to signal me"
                            << std::endl;
                        throw 3;
                    }

                    // The main thread will keep the blob mapped for 10ms
                    auto start = std::chrono::steady_clock::now();
                    skal::blob_proxy_t::scoped_map_t m(proxy2);
                    auto end = std::chrono::steady_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (diff < 2ms) {
                        std::cerr << "THR: didn't block while mapping proxy: "
                            << diff.count() << "ms" << std::endl;
                        throw 4;
                    }
                    char* ptr = static_cast<char*>(m.mem());
                    if (std::strcmp(ptr, "Hello, World!") != 0) {
                        std::cerr << "THR: blob has bad content" << std::endl;
                        throw 5;
                    }
                    std::strcpy(ptr, "How are you?");
                    sem1.post();
                } catch (...) {
                    thread_ok = false;
                }
            });

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");
        sem2.post();

        // Keep the blob mapped for 10ms
        std::this_thread::sleep_for(10ms);
        ASSERT_TRUE(thread_ok);
    }

    // Let the thread do its stuff
    bool taken = sem1.take(1s);
    ASSERT_TRUE(taken) << "Child thread failed to signal main thread";
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
    ft::semaphore_t sem1;
    ft::semaphore_t sem2;
    std::thread thread(
            [&thread_ok, &sem1, &sem2, &proxy] ()
            {
                try {
                    skal::blob_proxy_t proxy2 = proxy;
                    sem1.post();

                    // Let the main thread do its stuff
                    bool taken = sem2.take(1s);
                    if (!taken) {
                        std::cerr
                            << "THR: main thread failed to signal me"
                            << std::endl;
                        throw 1;
                    }

                    // The main thread will keep the blob mapped for 10ms
                    auto start = std::chrono::steady_clock::now();
                    skal::blob_proxy_t::scoped_map_t m(proxy2);
                    auto end = std::chrono::steady_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (diff < 2ms) {
                        std::cerr << "THR: didn't block while mapping proxy: "
                            << diff.count() << "ms" << std::endl;
                        throw 3;
                    }
                    char* ptr = static_cast<char*>(m.mem());
                    if (std::strcmp(ptr, "Hello, World!") != 0) {
                        std::cerr << "THR: blob has bad content" << std::endl;
                        throw 4;
                    }
                    std::strcpy(ptr, "How are you?");
                    sem1.post();
                } catch (...) {
                    thread_ok = false;
                }
            });

    // Let the thread make a copy of the proxy
    bool taken = sem1.take(1s);
    ASSERT_TRUE(taken);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");
        sem2.post();

        // Keep the blob mapped for 10ms
        std::this_thread::sleep_for(10ms);
        ASSERT_TRUE(thread_ok);
    }

    // Let the thread do its stuff
    taken = sem1.take(1s);
    ASSERT_TRUE(taken);
    ASSERT_TRUE(thread_ok);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    thread.join();
}

#ifndef _WIN32
using namespace boost::interprocess;

struct semaphores_t {
    interprocess_semaphore sem1;
    interprocess_semaphore sem2;

    semaphores_t(): sem1(0), sem2(0) { }
};

TEST(Blob, ShmBlobConcurrency)
{
    // Remove shared memory on construction and destruction
    constexpr const char* shm_name = "skal-test-blob";
    struct shm_remove_t {
        shm_remove_t() { shared_memory_object::remove(shm_name); }
        ~shm_remove_t() { shared_memory_object::remove(shm_name); }
    } shm_remover;

    shared_memory_object shm(create_only, shm_name, read_write);
    shm.truncate(sizeof(semaphores_t));
    {
        mapped_region region(shm, read_write);
        void* addr = region.get_address();
        (void)new (addr) semaphores_t();
    }

    int64_t size_B = 1000;
    skal::blob_proxy_t proxy = skal::create_blob("shm", "test-blob", size_B);
    std::string id = proxy.id();
    pid_t pid = fork();
    ASSERT_NE(-1, pid);
    if (pid == 0) { // I'm the child
        // Open the interprocess semaphores
        shared_memory_object myshm(open_only, shm_name, read_write);
        mapped_region region(myshm, read_write);
        void* addr = region.get_address();
        semaphores_t* semaphores = static_cast<semaphores_t*>(addr);

        // Let the main process do its stuff
        auto deadline = boost::posix_time::microsec_clock::universal_time();
        deadline += boost::posix_time::seconds(2);
        bool taken = semaphores->sem1.timed_wait(deadline);
        if (!taken) {
            std::cerr << "CHILD: Main process didn't notify me" << std::endl;
            throw 1;
        }

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
                // The main process will keep the blob mapped for 20ms
                auto start = std::chrono::steady_clock::now();
                skal::blob_proxy_t::scoped_map_t m(proxy2);
                auto end = std::chrono::steady_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (diff < 2ms) {
                    std::cerr << "CHILD: blocked for less than 10ms: "
                        << diff.count() << std::endl;
                    std::cerr << "CHILD: didn't block while mapping proxy: "
                        << diff.count() << "ms" << std::endl;
                    throw 3;
                }
                char* ptr = static_cast<char*>(m.mem());
                if (std::strcmp(ptr, "Hello, World!") != 0) {
                    std::cerr << "CHILD: blob has bad content" << std::endl;
                    throw 4;
                }
                std::strcpy(ptr, "How are you?");
                semaphores->sem2.post();
            }
        } catch (...) {
            std::abort();
        }
        std::exit(0);
    } // child process

    // Open the interprocess semaphores
    mapped_region region(shm, read_write);
    void* addr = region.get_address();
    semaphores_t* semaphores = static_cast<semaphores_t*>(addr);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        std::strcpy(ptr, "Hello, World!");
        semaphores->sem1.post();

        // Keep the blob mapped for 20ms
        std::this_thread::sleep_for(20ms);
    }

    // Let the child process do its stuff
    auto deadline = boost::posix_time::microsec_clock::universal_time();
    deadline += boost::posix_time::seconds(2);
    bool taken = semaphores->sem2.timed_wait(deadline);
    ASSERT_TRUE(taken);

    {
        skal::blob_proxy_t::scoped_map_t m(proxy);
        char* ptr = static_cast<char*>(m.mem());
        ASSERT_EQ(std::strcmp(ptr, "How are you?"), 0);
    }

    int wstatus;
    pid_t ret = ::waitpid(pid, &wstatus, 0);
    ASSERT_EQ(ret, pid);
    int exit_status = WEXITSTATUS(wstatus);
    ASSERT_EQ(0, exit_status) << "Child returned in error";
}
#endif
