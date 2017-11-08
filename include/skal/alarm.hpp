/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/detail/domain.hpp>
#include <string>
#include <utility>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace skal {

class msg_t;

/** Class that represents an alarm */
class alarm_t final
{
public :
    enum class severity_t {
        notice,
        warning,
        error
    };

    /** Constructor
     *
     * \param name     [in] Alarm name; names starting with "skal-" are
     *                      reserved for the skal framework
     * \param origin   [in] Name of worker which raised/lowered this alarm;
     *                      empty string if raised/lowered from outside a skal
     *                      worker
     * \param severity [in] Alarm severity
     * \param is_on    [in] Whether the alarm is on or off
     * \param auto_off [in] Whether the alarm is turned off by the software
     *                      or by a human; this boolean is purely informational
     * \param note     [in] Free-form, human-readable message
     */
    alarm_t(std::string name, std::string origin, severity_t severity,
            bool is_on, bool auto_off, std::string note);

    alarm_t() = delete;
    ~alarm_t() = default;
    alarm_t(const alarm_t&) = default;

    friend void swap(alarm_t& left, alarm_t& right)
    {
        using std::swap;
        swap(left.name_, right.name_);
        swap(left.origin_, right.origin_);
        swap(left.severity_, right.severity_);
        swap(left.is_on_, right.is_on_);
        swap(left.auto_off_, right.auto_off_);
        swap(left.note_, right.note_);
        swap(left.timestamp_, right.timestamp_);
    }

    alarm_t& operator=(alarm_t right)
    {
        swap(*this, right);
        return *this;
    }

    const std::string& name() const
    {
        return name_;
    }

    const std::string& origin() const
    {
        return origin_;
    }

    severity_t severity() const
    {
        return severity_;
    }

    bool is_on() const
    {
        return is_on_;
    }

    bool auto_off() const
    {
        return auto_off_;
    }

    const std::string& note() const
    {
        return note_;
    }

    /** Get the timestamp of when this alarm has been raised
     *
     * \return The alarm timestamp
     */
    boost::posix_time::ptime timestamp() const
    {
        return timestamp_;
    }

private :
    std::string name_;
    std::string origin_;
    severity_t  severity_;
    bool        is_on_;
    bool        auto_off_;
    std::string note_;
    boost::posix_time::ptime timestamp_;

    alarm_t(std::string name, std::string origin, severity_t severity,
            bool is_on, bool auto_off, std::string note,
            boost::posix_time::ptime timestamp)
        : name_(std::move(name))
        , origin_(worker_name(origin))
        , severity_(severity)
        , is_on_(is_on)
        , auto_off_(auto_off)
        , note_(std::move(note))
        , timestamp_(std::move(timestamp))
    {
    }

    friend class msg_t;
};

} // namespace skal
