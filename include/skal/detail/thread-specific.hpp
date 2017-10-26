/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <string>

namespace skal {

struct thread_specific_t
{
    std::string name;

    thread_specific_t() : name("skal-external") { }
};

thread_specific_t& thread_specific();

} // namespace skal
