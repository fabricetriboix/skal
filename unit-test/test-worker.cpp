/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/domain.hpp>
#include <gtest/gtest.h>

TEST(Worker, CreateAndDestroyWorker)
{
    skal::domain("my domain");
    std::unique_ptr<skal::worker_t> worker = skal::worker_t::create(
            "my worker",
            [] (std::unique_ptr<skal::msg_t> msg) { return true; });
    ASSERT_TRUE(worker);
    EXPECT_EQ(worker->name(), "my worker@my domain");
    EXPECT_EQ(worker->internal_msg_count(), 0u);
    EXPECT_EQ(worker->msg_count(), 1u);
    worker.reset();
}

TEST(Worker, SendAndReceiveMessage)
{
    int n = 0;
    skal::domain("factory");
    std::unique_ptr<skal::worker_t> worker = skal::worker_t::create(
            "employee",
            [&n] (std::unique_ptr<skal::msg_t> msg) { ++n; return true; });

    EXPECT_NO_THROW(worker->process_one()); // skal-init
    EXPECT_EQ(n, 1);

    std::unique_ptr<skal::msg_t> msg = skal::msg_t::create("boss",
            "employee", "sweat!");
    bool sent = skal::worker_t::post(msg);
    ASSERT_TRUE(sent);

    EXPECT_NO_THROW(worker->process_one()); // sweat!
    EXPECT_EQ(n, 2);
}

TEST(Worker, TestThrottling)
{
    skal::domain("office");
    std::unique_ptr<skal::worker_t> boss = skal::worker_t::create(
            "boss",
            [] (std::unique_ptr<skal::msg_t> msg) { return true; });
    int n = 0;
    std::unique_ptr<skal::worker_t> mug = skal::worker_t::create(
            "mug",
            [&n] (std::unique_ptr<skal::msg_t> msg) { ++n; return true; },
            1); // Very small message queue

    ASSERT_NO_THROW(mug->process_one()); // skal-init
    ASSERT_EQ(n, 1);
    ASSERT_EQ(mug->msg_count(), 0u);
    ASSERT_FALSE(boss->blocked());

    std::unique_ptr<skal::msg_t> msg = skal::msg_t::create("boss",
            "mug", "work!");
    bool sent = skal::worker_t::post(msg);
    ASSERT_TRUE(sent);
    ASSERT_EQ(mug->msg_count(), 1u);
    ASSERT_FALSE(boss->blocked());

    msg = skal::msg_t::create("boss", "mug", "work more!");
    sent = skal::worker_t::post(msg);
    ASSERT_TRUE(sent);
    ASSERT_EQ(mug->msg_count(), 2u);
    ASSERT_NO_THROW(boss->process_one()); // skal-xoff
    ASSERT_TRUE(boss->blocked());

    ASSERT_NO_THROW(mug->process_one()); // work!
    ASSERT_EQ(n, 2);
    ASSERT_NO_THROW(mug->process_one()); // work more!
    ASSERT_EQ(n, 3);
    ASSERT_NO_THROW(boss->process_one()); // skal-xon
    ASSERT_FALSE(boss->blocked());
}
