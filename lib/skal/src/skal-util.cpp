/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-util.hpp"
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

} // namespace skal
