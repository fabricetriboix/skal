/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/msg.hpp>
#include <skal/detail/msg.hpp>
#include <skal/detail/thread-specific.hpp>
#include <cstring>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <gtest/gtest.h>

TEST(Msg, EncodeDecodeMsg)
{
    skal::domain("abc");
    skal::thread_specific().name = std::string("skal-external@abc");

    boost::posix_time::ptime time_point
        = boost::posix_time::microsec_clock::universal_time();
    uint32_t flags = skal::flag_t::udp | skal::flag_t::multicast;
    skal::msg_t msg("test-msg", "bob", flags, 15);

    boost::posix_time::time_duration delta = msg.timestamp() - time_point;
    auto max = boost::posix_time::microseconds(500);
    EXPECT_LE(delta, max);

    EXPECT_EQ("test-msg", msg.name());
    EXPECT_EQ("skal-external@abc", msg.sender());
    EXPECT_EQ("bob@abc", msg.recipient());
    EXPECT_EQ(flags, msg.flags());
    EXPECT_EQ(15, msg.ttl());

    skal::alarm_t alarm("test-alarm", skal::alarm_t::severity_t::warning,
            true, false, "This is a test alarm");
    EXPECT_EQ("test-alarm", alarm.name());
    EXPECT_EQ(skal::alarm_t::severity_t::warning, alarm.severity());
    EXPECT_TRUE(alarm.is_on());
    EXPECT_FALSE(alarm.auto_off());
    EXPECT_EQ("This is a test alarm", alarm.msg());
    EXPECT_EQ("skal-external@abc", alarm.origin());
    delta = alarm.timestamp() - time_point;
    EXPECT_LE(delta, max);
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
    EXPECT_EQ("test-msg", msg2.name());
    EXPECT_EQ("skal-external@abc", msg2.sender());
    EXPECT_EQ("bob@abc", msg2.recipient());
    EXPECT_EQ(flags, msg2.flags());
    EXPECT_EQ(15, msg2.ttl());

    boost::optional<skal::alarm_t> alarm2(msg2.detach_alarm());
    ASSERT_TRUE(alarm2);
    EXPECT_EQ("test-alarm", alarm2->name());
    EXPECT_EQ(skal::alarm_t::severity_t::warning, alarm2->severity());
    EXPECT_TRUE(alarm2->is_on());
    EXPECT_FALSE(alarm2->auto_off());
    EXPECT_EQ("This is a test alarm", alarm2->msg());
    EXPECT_EQ("skal-external@abc", alarm2->origin());
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
