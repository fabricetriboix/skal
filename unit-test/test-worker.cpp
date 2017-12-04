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

TEST_F(Worker, CreateAfterInit)
{
    int n = 0;
    auto employee_job = [&n] (std::unique_ptr<skal::msg_t> msg)
    {
        if (msg->action() == "sweat!") {
            ++n;
            return false;
        }
        return true;
    };

    skal::worker_t::create("boss",
            [employee_job] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "skal-init") {
                    skal::worker_t::create("employee", employee_job);
                    skal::send(skal::msg_t::create("employee", "sweat!"));
                }
                return false;
            });
    run();
    ASSERT_EQ(n, 1);
}

TEST_F(Worker, TestThrottling)
{
    int n = 0;
    auto boss_job = [&n] (std::unique_ptr<skal::msg_t> msg)
    {
        if (msg->action() == "skal-init") {
            msg = skal::msg_t::create("employee", "work!");
            bool posted = skal::worker_t::post(msg);
            skal_assert(posted);
            msg = skal::msg_t::create("employee", "work more!");
            posted = skal::worker_t::post(msg);
            skal_assert(posted);
        } else if (msg->action() == "skal-throttle-on") {
            ++n;
        } else if (msg->action() == "skal-throttle-off") {
            ++n;
            return false;
        }
        return true;
    };

    skal::worker_t::params_t params;
    params.name = "employee";
    params.process_msg = [boss_job] (std::unique_ptr<skal::msg_t> msg)
    {
        if (msg->action() == "skal-init") {
            skal::worker_t::params_t params { "boss", boss_job };
            params.xoff_timeout = 1s;
            skal::worker_t::create(std::move(params));
            std::this_thread::sleep_for(10ms);
        } else if (msg->action() == "work more!") {
            return false;
        }
        return true;
    };
    params.queue_threshold = 1; // Very small message queue
    skal::worker_t::create(std::move(params));

    run();
    ASSERT_EQ(n, 2);
}
