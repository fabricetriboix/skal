/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-util.hpp"
#include <gtest/gtest.h>

TEST(Util, ParseFullUrl)
{
    skal::url_t url("tcp://bob:1234/somewhere");
    EXPECT_EQ(url.scheme(), "tcp");
    EXPECT_EQ(url.host(), "bob");
    EXPECT_EQ(url.port(), "1234");
    EXPECT_EQ(url.path(), "/somewhere");
}

TEST(Util, ParsePartialUrl)
{
    skal::url_t url("udp://alice.com:www");
    EXPECT_EQ(url.scheme(), "udp");
    EXPECT_EQ(url.host(), "alice.com");
    EXPECT_EQ(url.port(), "www");
    EXPECT_TRUE(url.path().empty());
}

TEST(Util, ParseFileUrl)
{
    skal::url_t url("local:///tmp/sock");
    EXPECT_EQ(url.scheme(), "local");
    EXPECT_EQ(url.host(), "");
    EXPECT_EQ(url.port(), "");
    EXPECT_EQ(url.path(), "/tmp/sock");
}

TEST(Util, MakeUrl)
{
    skal::url_t url;
    url.scheme("sctp");
    url.host("test-1.example.com");
    url.port("9000");
    EXPECT_EQ(url.url(), "sctp://test-1.example.com:9000");
}
