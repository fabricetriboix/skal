/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/semaphore.hpp>
#include <gtest/gtest.h>

TEST(Worker, CreateAndDestroyWorker)
{
    skal::worker_t::create("my worker",
            [] (std::unique_ptr<skal::msg_t> msg) { return false; });
}

TEST(Worker, SendAndReceiveMessage)
{
    ft::semaphore_t sem;
    skal::worker_t::create("employee",
            [&sem] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "stop" ) {
                    return false;
                }
                sem.post();
                return true;
            });

    bool taken = sem.take(1s);
    ASSERT_TRUE(taken); // skal-init

    auto msg = skal::msg_t::create("boss", "employee", "sweat!");
    bool posted = skal::worker_t::post(msg);
    ASSERT_TRUE(posted);

    taken = sem.take(1s);
    ASSERT_TRUE(taken); // sweat!

    msg = skal::msg_t::create("", "employee", "stop");
    posted = skal::worker_t::post(msg);
    ASSERT_TRUE(posted);
    std::this_thread::sleep_for(100ms); // XXX get rid of that ugly stuff
}

TEST(Worker, TestThrottling)
{
    skal::worker_t::create("boss",
            [] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "skal-init") {
                    msg = skal::msg_t::create("employee", "work!");
                    bool posted = skal::worker_t::post(msg);
                    skal_assert(posted);
                    msg = skal::msg_t::create("employee", "work more!");
                    posted = skal::worker_t::post(msg);
                    skal_assert(posted);
                } else if (msg->action() == "stop") {
                    return false;
                }
                return true;
            });

    ft::semaphore_t sem;
    skal::worker_t::create("employee",
            [&sem] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "skal-init") {
                    std::this_thread::sleep_for(10ms);
                } else if (msg->action() == "work more!") {
                    sem.post();
                } else if (msg->action() == "stop") {
                    return false;
                }
                return true;
            },
            -1, // numa_node
            1); // Very small message queue

    bool taken = sem.take(1s);
    ASSERT_TRUE(taken); // skal-init

    auto msg = skal::msg_t::create("", "boss", "stop");
    bool posted = skal::worker_t::post(msg);
    ASSERT_TRUE(posted);
    msg = skal::msg_t::create("", "employee", "stop");
    posted = skal::worker_t::post(msg);
    ASSERT_TRUE(posted);
    std::this_thread::sleep_for(100ms); // XXX get rid of that ugly stuff
}
