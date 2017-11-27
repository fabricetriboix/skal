/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/alarm.hpp>

namespace skal {

alarm_t::alarm_t(std::string name, std::string origin, severity_t severity,
        bool is_on, bool auto_off, std::string note)
    : name_(std::move(name))
    , origin_(full_name(origin))
    , severity_(severity)
    , is_on_(is_on)
    , auto_off_(auto_off)
    , note_(std::move(note))
    , timestamp_(std::chrono::system_clock::now())
{
}

} // namespace skal
