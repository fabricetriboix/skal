/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include "skal-cfg.hpp"
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace skal {

int64_t ptime_to_us(boost::posix_time::ptime timestamp);

boost::posix_time::ptime us_to_ptime(int64_t us);

} // namespace skal
