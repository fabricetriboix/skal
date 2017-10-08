/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "skal-alarm.hpp"
#include "detail/skal-thread-specific.hpp"

namespace skal {

alarm_t::alarm_t(std::string name, severity_t severity, bool is_on,
        bool auto_off, std::string msg)
    : name_(std::move(name))
    , severity_(severity)
    , is_on_(is_on)
    , auto_off_(auto_off)
    , msg_(std::move(msg))
    , origin_(thread_specific().name)
    , timestamp_(boost::posix_time::microsec_clock::universal_time())
{
}

} // namespace skal
