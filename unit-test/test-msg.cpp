/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/msg.hpp>
#include <skal/domain.hpp>
#include <skal/util.hpp>
#include <cstring>
#include <gtest/gtest.h>

TEST(Msg, EncodeDecodeMsg)
{
    skal::domain("abc");

    auto time_point = std::chrono::system_clock::now();
    uint32_t flags = skal::msg_t::flag_t::urgent;
    skal::msg_t msg("alice", "bob", "test-msg", flags, 15);

    EXPECT_LE(msg.timestamp() - time_point, 1ms);
    EXPECT_EQ("test-msg", msg.action());
    EXPECT_EQ("alice@abc", msg.sender());
    EXPECT_EQ("bob@abc", msg.recipient());
    EXPECT_EQ(flags, msg.flags());
    EXPECT_EQ(15, msg.ttl());

    skal::alarm_t alarm("test-alarm", "alice",
            skal::alarm_t::severity_t::warning,
            true, false, "This is a test alarm");
    EXPECT_EQ("test-alarm", alarm.name());
    EXPECT_EQ(skal::alarm_t::severity_t::warning, alarm.severity());
    EXPECT_TRUE(alarm.is_on());
    EXPECT_FALSE(alarm.auto_off());
    EXPECT_EQ("This is a test alarm", alarm.note());
    EXPECT_EQ("alice@abc", alarm.origin());
    EXPECT_LE(alarm.timestamp() - time_point, 1ms);
    msg.attach_alarm(std::move(alarm));

    msg.add_field("test-int", 7);
    msg.add_field("test-double", 0.0123);
    msg.add_field("test-string", "Hello, World!");

    skal::miniblob_t miniblob;
    miniblob.push_back(0xde);
    miniblob.push_back(0xad);
    miniblob.push_back(0xbe);
    miniblob.push_back(0xef);
    msg.add_field("test-miniblob", std::move(miniblob));

    skal::blob_proxy_t proxy = skal::create_blob("malloc", "", 100);
    {
        skal::blob_proxy_t::scoped_map_t scoped_mapping(proxy);
        char* ptr = static_cast<char*>(scoped_mapping.mem());
        std::strcpy(ptr, "I am a malloc blob");
    }
    msg.add_field("test-blob", std::move(proxy));

    std::string data = msg.serialize();

    // De-serialize the message and check its content
    skal::msg_t msg2(data);
    EXPECT_EQ(msg.timestamp(), msg2.timestamp());
    EXPECT_EQ("test-msg", msg2.action());
    EXPECT_EQ("alice@abc", msg2.sender());
    EXPECT_EQ("bob@abc", msg2.recipient());
    EXPECT_EQ(flags, msg2.flags());
    EXPECT_EQ(15, msg2.ttl());

    boost::optional<skal::alarm_t> alarm2(msg2.detach_alarm());
    ASSERT_TRUE(alarm2);
    EXPECT_EQ("test-alarm", alarm2->name());
    EXPECT_EQ(skal::alarm_t::severity_t::warning, alarm2->severity());
    EXPECT_TRUE(alarm2->is_on());
    EXPECT_FALSE(alarm2->auto_off());
    EXPECT_EQ("This is a test alarm", alarm2->note());
    EXPECT_EQ("alice@abc", alarm2->origin());
    EXPECT_EQ(alarm.timestamp(), alarm2->timestamp());

    boost::optional<skal::alarm_t> alarm3(msg2.detach_alarm());
    EXPECT_FALSE(alarm3);

    EXPECT_EQ(7, msg2.get_int("test-int"));
    EXPECT_DOUBLE_EQ(0.0123, msg2.get_double("test-double"));
    EXPECT_EQ("Hello, World!", msg2.get_string("test-string"));
    EXPECT_EQ(miniblob, msg2.get_miniblob("test-miniblob"));

    skal::blob_proxy_t proxy2 = msg2.get_blob("test-blob");
    {
        skal::blob_proxy_t::scoped_map_t mapping(proxy2);
        char* ptr = static_cast<char*>(mapping.mem());
        EXPECT_EQ(std::strcmp(ptr, "I am a malloc blob"), 0);
    }

    skal::blob_proxy_t proxy3 = msg2.detach_blob("test-blob");
    {
        skal::blob_proxy_t::scoped_map_t mapping(proxy3);
        char* ptr = static_cast<char*>(mapping.mem());
        EXPECT_EQ(std::strcmp(ptr, "I am a malloc blob"), 0);
    }

    EXPECT_THROW(skal::blob_proxy_t proxy4 = msg2.detach_blob("test-blob"),
            std::out_of_range);
}
