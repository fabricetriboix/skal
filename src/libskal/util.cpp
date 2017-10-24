/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/detail/util.hpp>
#include <skal/error.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>

namespace skal {

int64_t ptime_to_us(boost::posix_time::ptime timestamp)
{
    // We calculate the timestamp from the Epoch: 1970-01-01 00:00:00
    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
    boost::posix_time::time_duration duration = timestamp - epoch;
    return duration.total_microseconds();
}

boost::posix_time::ptime us_to_ptime(int64_t us)
{
    // We calculate the timestamp from the Epoch: 1970-01-01 00:00:00
    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
    return epoch + boost::posix_time::microseconds(us);
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
    }

    // Parse "host:port"
    pos = s.find(':');
    if (pos != std::string::npos) {
        port_ = s.substr(pos+1);
        s = s.substr(0, pos);
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
