/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-queue.hpp"
#include <gtest/gtest.h>

TEST(Queue, PushAndPop)
{
    skal::queue_t queue(2);
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    {
        skal::msg_t msg("test-msg", "test-recipient");
        queue.push(std::move(msg));
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    {
        skal::msg_t msg("test-msg-2", "test-recipient-2", skal::flag_t::urgent);
        queue.push(std::move(msg));
    }
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    {
        skal::msg_t msg("test-msg-3", "test-recipient-3");
        queue.push(std::move(msg));
    }
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(3u, queue.size());

    {
        skal::msg_t msg = queue.pop_BLOCKING();
        EXPECT_EQ(msg.name(), "test-msg-2");
        EXPECT_EQ(msg.recipient(), "test-recipient-2");
    }
    EXPECT_TRUE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(2u, queue.size());

    {
        skal::msg_t msg = queue.pop();
        EXPECT_EQ(msg.name(), "test-msg");
        EXPECT_EQ(msg.recipient(), "test-recipient");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_TRUE(queue.is_half_full());
    EXPECT_EQ(1u, queue.size());

    {
        skal::msg_t msg = queue.pop();
        EXPECT_EQ(msg.name(), "test-msg-3");
        EXPECT_EQ(msg.recipient(), "test-recipient-3");
    }
    EXPECT_FALSE(queue.is_full());
    EXPECT_FALSE(queue.is_half_full());
    EXPECT_EQ(0u, queue.size());

    EXPECT_THROW(skal::msg_t msg = queue.pop(), std::out_of_range);
}
