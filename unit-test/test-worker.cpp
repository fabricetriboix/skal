/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/skal.hpp>
#include <skal/semaphore.hpp>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <gtest/gtest.h>

struct Worker : public testing::Test
{
    Worker()
    {
        skal::parameters_t parameters;
        skal::init(parameters);
    }

    ~Worker()
    {
    }

    void run(std::chrono::nanoseconds timeout = 1s)
    {
        boost::asio::io_service ios;
        boost::asio::steady_timer timer(ios);
        timer.expires_from_now(timeout);
        timer.async_wait(
                [] (const boost::system::error_code& ec)
                {
                    skal_panic() << "skal::wait() timeout!";
                });
        std::thread thread([&ios] () { ios.run(); });
        skal::wait();
        ios.stop();
        thread.join();
    }
};

TEST_F(Worker, SendAndReceiveMessage)
{
    int n = 0;
    skal::worker_t::create("employee",
            [&n] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "sweat!") {
                    ++n;
                    return false;
                }
                return true;
            });
    skal::worker_t::create("boss",
            [] (std::unique_ptr<skal::msg_t> msg)
            {
                skal::send(skal::msg_t::create("employee", "sweat!"));
                return false;
            });
    run();
    ASSERT_EQ(n, 1);
}

#if 0
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
#endif
