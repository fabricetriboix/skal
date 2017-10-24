/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include "skal-cfg.hpp"
#include <utility>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace skal {

int64_t ptime_to_us(boost::posix_time::ptime timestamp);

boost::posix_time::ptime us_to_ptime(int64_t us);

class url_t final
{
public :
    url_t() = default;
    ~url_t() = default;

    /** Construct from URL string
     *
     * \param s [in] URL string to parse
     *
     * \throw `bad_url()` if `s` is malformatted
     */
    explicit url_t(std::string s);

    /** Construct from the parts
     */
    url_t(std::string scheme, std::string host, std::string port,
            std::string path)
        : scheme_(std::move(scheme))
        , host_(std::move(host))
        , port_(std::move(port))
        , path_(std::move(path))
    {
        update_url();
    }

    const std::string& url() const
    {
        return url_;
    }

    const std::string& scheme() const
    {
        return scheme_;
    }

    const std::string& host() const
    {
        return host_;
    }

    const std::string& port() const
    {
        return port_;
    }

    const std::string& path() const
    {
        return path_;
    }

    void url(std::string s);
    void scheme(std::string s);
    void host(std::string s);
    void port(std::string s);
    void path(std::string s);

    friend void swap(url_t& left, url_t& right)
    {
        using std::swap;
        swap(left.url_, right.url_);
        swap(left.scheme_, right.scheme_);
        swap(left.host_, right.host_);
        swap(left.port_, right.port_);
        swap(left.path_, right.path_);
    }

    url_t& operator=(url_t right)
    {
        swap(*this, right);
        return *this;
    }

    url_t& operator=(std::string s)
    {
        url(s);
        return *this;
    }

private :
    void update_url();

    std::string url_;
    std::string scheme_;
    std::string host_;
    std::string port_;
    std::string path_;
};

} // namespace skal
