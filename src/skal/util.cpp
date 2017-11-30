/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/util.hpp>
#include <skal/error.hpp>
#include <cstring>

namespace skal {

bool start_with(const std::string& haystack, const std::string& needle)
{
    return std::strncmp(haystack.c_str(), needle.c_str(), needle.size()) == 0;
}

url_t::url_t(std::string s)
{
    url(s);
}

void url_t::url(std::string s)
{
    // Parse and remove "scheme://"
    size_t pos = s.find(':');
    if (pos == std::string::npos) {
        throw bad_url();
    }
    scheme_ = s.substr(0, pos);
    if (scheme_.empty()) {
        throw bad_url();
    }
    ++pos;
    if ((s[pos] == '/') && (s[pos] == '/')) {
        pos += 2;
    }
    s = s.substr(pos);

    // Parse and remove "/path"
    pos = s.find('/');
    if (pos != std::string::npos) {
        path_ = s.substr(pos);
        s = s.substr(0, pos);
    } else {
        path_.clear();
    }

    // Parse "host:port"
    pos = s.find(':');
    if (pos != std::string::npos) {
        port_ = s.substr(pos+1);
        s = s.substr(0, pos);
    } else {
        port_.clear();
    }
    host_ = s;

    // Re-construct the URL string
    update_url();
}

void url_t::scheme(std::string s)
{
    scheme_ = std::move(s);
    update_url();
}

void url_t::host(std::string s)
{
    host_ = std::move(s);
    update_url();
}

void url_t::port(std::string s)
{
    port_ = std::move(s);
    update_url();
}

void url_t::path(std::string s)
{
    path_ = std::move(s);
    update_url();
}

void url_t::update_url()
{
    url_ = scheme_ + "://" + host_;
    if (!port_.empty()) {
        url_ += ":" + port_;
    }
    url_ += path_;
}

} // namespace skal
