/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/group.hpp>
#include <skal/worker.hpp>
#include <skal/global.hpp>
#include <gtest/gtest.h>

struct Group : public testing::Test
{
    skal::executor_t executor;

    Group() : executor(skal::create_scheduler())
    {
    }
};

TEST_F(Group, CreateAndDestroyGroup)
{
    skal::group_t::create("test-group", executor);
    skal::group_t::destroy("test-group");
}

TEST_F(Group, SendAndReceiveMessage)
{
    ft::semaphore_t sem;
    int n = 0;
    executor.add_worker(skal::worker_t::create(
                "employee",
                [&sem, &n] (std::unique_ptr<skal::msg_t> msg)
                {
                    if (msg->action() == "test-msg") {
                        ++n;
                        sem.post();
                    } else if (msg->action() == "subscribe") {
                        skal::group_t::subscribe("test-group", "employee");
                        skal::send(skal::msg_t::create("employee",
                                    "boss", "kick"));
                    }
                    return true;
                }));

    executor.add_worker(skal::worker_t::create(
                "boss",
                [&sem] (std::unique_ptr<skal::msg_t> msg)
                {
                    if (msg->action() == "skal-init") {
                        skal::send(skal::msg_t::create("boss",
                                    "test-group", "test-msg"));
                        sem.post();
                    } else if (msg->action() == "kick") {
                        skal::send(skal::msg_t::create("boss",
                                    "test-group", "test-msg"));
                    }
                    return true;
                }));

    // Wait for "boss" to try to send to group "test-group"
    bool taken = sem.take(1s);
    ASSERT_TRUE(taken);
    ASSERT_EQ(n, 0); // no-one's listening on this group

    skal::send(skal::msg_t::create("employee", "subscribe"));

    // Wait for things to happen
    taken = sem.take(1s);
    ASSERT_TRUE(taken);
    ASSERT_EQ(n, 1);
}
