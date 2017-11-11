/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/error.hpp>
#include <iostream>
#include <exception>
#include <boost/filesystem.hpp>

namespace skal {

assert_t::assert_t(const char* file, int line, const char* cond)
{
    boost::filesystem::path path(file);
    oss << "skal_assert [" << path.filename() << ":" << line << "] "
        << (cond ? cond : "") << " ";
}

assert_t::~assert_t()
{
    oss << std::endl;
    std::cerr << oss.str();
    std::terminate();
}

} // namespace skal
