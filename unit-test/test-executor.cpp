/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/executor.hpp>
#include <skal/global.hpp>
#include <skal/semaphore.hpp>
#include <gtest/gtest.h>

TEST(Executor, SendAndReceiveOneMessage)
{
    skal::domain("factory");

    std::unique_ptr<skal::scheduler_t> scheduler = skal::create_scheduler(
            skal::policy_t::fair);
    ASSERT_TRUE(scheduler);

    skal::executor_t executor(std::move(scheduler));

    ft::semaphore_t sem_boss;
    std::unique_ptr<skal::worker_t> boss = skal::worker_t::create(
            "boss",
            [&sem_boss] (std::unique_ptr<skal::msg_t> msg)
            {
                sem_boss.post();
                if (msg->action() == "work!") {
                    skal::send(skal::msg_t::create("boss",
                                "mug", "you work!"));
                }
                return true;
            });
    executor.add_worker(std::move(boss));
    bool taken = sem_boss.take(100ms);
    ASSERT_TRUE(taken); // skal-init

    ft::semaphore_t sem_mug;
    std::unique_ptr<skal::worker_t> mug = skal::worker_t::create(
            "mug",
            [&sem_mug] (std::unique_ptr<skal::msg_t> msg)
            {
                sem_mug.post();
                return true;
            });
    executor.add_worker(std::move(mug));
    taken = sem_mug.take(100ms);
    ASSERT_TRUE(taken); // skal-init

    skal::send(skal::msg_t::create("boss@factory", "work!"));
    taken = sem_boss.take(100ms);
    ASSERT_TRUE(taken); // work!

    taken = sem_mug.take(100ms);
    ASSERT_TRUE(taken); // you work!
}
