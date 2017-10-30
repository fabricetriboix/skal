/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>

namespace skal {

/** Get the domain this process belongs to */
const std::string& domain();

/** Set the domain this process belongs to
 *
 * \param domain [in] Domain this process belongs to
 */
void domain(std::string domain);

/** Get the full worker name
 *
 * This function appends the local domain if the worker name does not have a
 * domain.
 *
 * \param name [in] Worker name
 *
 * \return Full worker name
 */
std::string worker_name(std::string name);

} // namespace skal
