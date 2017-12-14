/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/skal.hpp>
#include <skal/semaphore.hpp>
#include <vector>
#include <sstream>
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

TEST_F(Worker, Group)
{
    int n1 = 0;
    auto receiver1_job =
        [&n1] (std::unique_ptr<skal::msg_t> msg)
        {
            if (msg->action() == "skal-init") {
                skal::send(skal::msg_t::create("team", "skal-subscribe"));
            } else if (msg->action() == "beep") {
                ++n1;
            } else if (msg->action() == "beeep") {
                ++n1;
                return false;
            }
            return true;
        };

    int n2 = 0;
    auto receiver2_job =
        [&n2] (std::unique_ptr<skal::msg_t> msg)
        {
            if (msg->action() == "skal-init") {
                skal::send(skal::msg_t::create("team", "skal-subscribe"));
            } else if (msg->action() == "beep") {
                ++n2;
            } else if (msg->action() == "beeep") {
                ++n2;
                return false;
            }
            return true;
        };

    skal::worker_t::create("sender",
            [receiver1_job, receiver2_job] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "skal-init") {
                    // Will be dropped because group does not exist yet
                    skal::send(skal::msg_t::create("team", "beep"));
                    skal::worker_t::create("recv1", receiver1_job);
                    skal::worker_t::create("recv2", receiver2_job);
                    // Wait for group to be created
                    std::this_thread::sleep_for(500ms);
                    skal::send(skal::msg_t::create("team", "beep"));
                    skal::send(skal::msg_t::create("team", "beep"));
                    skal::send(skal::msg_t::create("team", "beeep"));
                }
                return false;
            });
    run();
    ASSERT_EQ(n1, 3);
    ASSERT_EQ(n2, 3);
}

TEST_F(Worker, Stress)
{
    constexpr const int nworkers = 50; // Number of workers
    constexpr const int nmsg = 10'00;  // Number of messages
    std::vector<int> counts;
    counts.resize(nworkers, 0);

    struct job_t {
        int n;
        std::string next;
        int& count;

        job_t(int _n, int& _count) : n(_n), count(_count) {
            std::ostringstream oss;
            oss << "worker" << (n+1);
            next = oss.str();
        }

        bool operator()(std::unique_ptr<skal::msg_t> msg) {
            if (msg->action() == "stress") {
                ++count;
                if (msg->has_int("last")) {
                    msg = skal::msg_t::create(next, "stress");
                    msg->add_field("last", 1);
                    skal::send(std::move(msg));
                    return false;
                }
                skal::send(skal::msg_t::create(next, "stress"));
            }
            return true;
        }
    };

    for (int i = 0; i < nworkers; ++i) {
        std::ostringstream oss;
        oss << "worker" << i;
        skal::worker_t::create(oss.str(), job_t(i, counts[i]));
    }

    int source_count = 0;
    skal::worker_t::create("source",
            [nmsg, &source_count] (std::unique_ptr<skal::msg_t> msg)
            {
                if (msg->action() == "kick") {
                    msg = skal::msg_t::create("worker0", "stress");
                    ++source_count;
                    if (source_count >= nmsg) {
                        msg->add_field("last", 1);
                    }
                    skal::send(std::move(msg));
                    if (source_count >= nmsg) {
                        return false;
                    }
                }
                skal::send(skal::msg_t::create("source", "kick"));
                return true;
            });

    run(5min);

    for (int i = 0; i < nworkers; ++i) {
        ASSERT_EQ(counts[i], nmsg);
    }
}
