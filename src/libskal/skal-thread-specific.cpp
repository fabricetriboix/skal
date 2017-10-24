/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-thread-specific.hpp"
#include <thread>

namespace skal {

static thread_local thread_specific_t g_thread_specific;

thread_specific_t& thread_specific()
{
    return g_thread_specific;
}

} // namespace skal
