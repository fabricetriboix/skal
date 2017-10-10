/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <string>
#include <stdexcept>
#include <sstream>

namespace skal {

/** Basic skal exception */
struct error : public std::runtime_error
{
    error() : std::runtime_error("skal::error") { }
    error(const std::string& s) : std::runtime_error(s) { }
};

struct assert_t
{
    std::ostringstream oss;
    assert_t(const char* file, int line, const char* cond);
    ~assert_t();
};

#define SKAL_ASSERT(cond) if (!(cond)) \
    skal::assert_t(__FILE__, __LINE__, #cond).oss

} // namespace skal
