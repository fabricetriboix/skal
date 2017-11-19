/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/queue.hpp>
#include <gtest/gtest.h>

TEST(Queue, PushAndPop)
{
    skal::domain("xyz");

    skal::queue_t queue(3);
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    queue.push(skal::msg_t::create("sender1", "recipient1", "action1"));
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    queue.push(skal::msg_t::create("sender2", "recipient2", "action2",
            skal::msg_t::flag_t::urgent));
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    queue.push(skal::msg_t::create("sender3", "recipient3", "action3"));
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(3u, queue.size());

    queue.push(skal::msg_t::create("sender4", "recipient4", "action4"));
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(4u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->sender(), "sender2@xyz");
        EXPECT_EQ(msg->recipient(), "recipient2@xyz");
        EXPECT_EQ(msg->action(), "action2");
    }
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(3u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->sender(), "sender1@xyz");
        EXPECT_EQ(msg->recipient(), "recipient1@xyz");
        EXPECT_EQ(msg->action(), "action1");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->sender(), "sender3@xyz");
        EXPECT_EQ(msg->recipient(), "recipient3@xyz");
        EXPECT_EQ(msg->action(), "action3");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->sender(), "sender4@xyz");
        EXPECT_EQ(msg->recipient(), "recipient4@xyz");
        EXPECT_EQ(msg->action(), "action4");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_EQ(nullptr, msg);
    }
}

TEST(Queue, Listen)
{
    skal::domain("xyz");

    skal::queue_t queue(3);
    queue.push(skal::msg_t::create("sender1", "recipient1", "action1"));
    int notified = 0;
    queue.listen(
            [&notified] ()
            {
                ++notified;
            });
    ASSERT_EQ(notified, 1);

    queue.push(skal::msg_t::create("sender2", "recipient1", "action1"));
    ASSERT_EQ(notified, 2);
}
