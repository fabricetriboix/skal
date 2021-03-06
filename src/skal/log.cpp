/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/log.hpp>
#include "date.h"
#include <iostream>
#include <iomanip>
#include <mutex>
#include <boost/filesystem.hpp>

namespace skal {
namespace log {

static std::mutex g_mutex;

const char* to_string(level_t level)
{
    switch (level)
    {
    case level_t::debug :
        return "DBUG";

    case level_t::info :
        return "INFO";

    case level_t::notice :
        return "NOTE";

    case level_t::warning :
        return "WARN";

    case level_t::error :
        return " ERR";

    default :
        return "????";
    }
};

static level_t g_minimum_level = level_t::SKAL_DEFAULT_LOG_LEVEL;

level_t minimum_level()
{
    return g_minimum_level;
}

void minimum_level(level_t level)
{
    g_minimum_level = level;
}

void process(record_t record)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    boost::filesystem::path file(record.file);
    std::cerr << date::format("%FT%TZ",
            date::floor<std::chrono::microseconds>(record.timestamp))
        << " {" << record.thread
        << "} " << to_string(record.level)
        << " [" << file.filename().string() << ":" << std::dec << record.line
        << "] " << record.msg
        << std::endl;
}

log_t::log_t(level_t level, const char* file, int line) : oss()
{
    record.timestamp = std::chrono::system_clock::now();
    record.level = level;
    if (file != nullptr) {
        record.file = file;
    }
    record.line = line;
    record.thread = me();
}

} // namespace log
} // namespace skal
