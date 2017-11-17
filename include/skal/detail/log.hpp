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
enum class level_t
{
    /** Debug level
     *
     * For debugging by developers. This produces a lot of output and will have
     * an impact on the real-time behaviour of the skal-based application.
     *
     * Eg: Human-readable dump of each and every message that is passed through
     *     the skal framework.
     */
    debug,

    /** Information level
     *
     * For debugging by the user of the skal framework. This produces
     * informational statements useful to track down problems when running the
     * skal-based application.
     *
     * Eg: When a worker is created or terminated.
     */
    info,

    /** Notices
     *
     * These logs are useful and important messages. When such a message is
     * issued, this is for information only and nothing bad is happening in the
     * skal framework.
     *
     * Eg: When a worker throws an exception.
     */
    notice,

    /** Warnings
     *
     * These logs are issued when an error condition occurred, but it is
     * recoverable and the skal framework will try to recover it.
     *
     * Eg: Timeout when delivering a message.
     */
    warning,

    /** Errors
     *
     * These logs are issued when an unrecoverable error condition occurred.
     * The skal framework can continue, but the functionality of the skal
     * application has been impacted.
     *
     * Eg: Failed to deliver a message that had to be delivered.
     */
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
