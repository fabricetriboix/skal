/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <thread>
#include <string>
#include <sstream>
#include <utility>
#include <stdexcept>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace skal {
namespace log {

/** Severity levels */
enum class level_t {
    debug,
    info,
    notice,
    warning,
    error
};

/** Convert a severity level to a human-readable string */
const char* to_string(level_t level);

/** Get the minimum severity level */
level_t minimum_level();

/** Set the minimum severity level */
void minimum_level(level_t level);

/** Log record */
struct record_t
{
    level_t                  level;
    std::string              file;
    int                      line;
    boost::posix_time::ptime timestamp;
    std::thread::id          thread_id;
    std::string              msg;
};

/** Process a log record */
void process(record_t record);

/** Structure to log a message
 *
 * You should not use this structure on its own, use the `skal_log()` macro
 * instead.
 */
struct log_t
{
    std::ostringstream oss;
    record_t record;

    log_t(level_t level, const char* file, int line);

    ~log_t()
    {
        record.msg = oss.str();
        process(std::move(record));
    }
};

#define skal_log(level) \
    if ((::skal::log::level_t::level) >= ::skal::log::minimum_level()) \
        skal::log::log_t((::skal::log::level_t::level), __FILE__, __LINE__).oss

} // namespace log
} // namespace skal
