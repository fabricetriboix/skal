/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-queue.hpp"
#include "detail/skal-msg-detail.hpp"
#include "detail/skal-thread-specific.hpp"
#include <gtest/gtest.h>

TEST(Queue, PushAndPop)
{
    skal::domain("xyz");
    skal::thread_specific().name = "skal-external@xyz";

    skal::queue_t queue(2);
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    queue.push(std::make_unique<skal::msg_t>("test-msg", "test-recipient"));
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    queue.push(std::make_unique<skal::msg_t>("test-msg-2", "test-recipient-2",
            skal::flag_t::urgent));
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    queue.push(std::make_unique<skal::msg_t>("test-msg-3",
                "test-recipient-3"));
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(3u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop_BLOCKING();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->name(), "test-msg-2");
        EXPECT_EQ(msg->recipient(), "test-recipient-2@xyz");
    }
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->name(), "test-msg");
        EXPECT_EQ(msg->recipient(), "test-recipient@xyz");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_NE(nullptr, msg);
        EXPECT_EQ(msg->name(), "test-msg-3");
        EXPECT_EQ(msg->recipient(), "test-recipient-3@xyz");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    {
        std::unique_ptr<skal::msg_t> msg = queue.pop();
        ASSERT_EQ(nullptr, msg);
    }
}
