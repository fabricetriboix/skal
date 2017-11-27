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

/** Get the fully qualified name
 *
 * This function appends the local domain if the given name does not have a
 * domain.
 *
 * \param name [in] Name
 *
 * \return Fully qualifed name
 */
std::string full_name(std::string name);

/** Is this process running standalone?
 *
 * A standalone process is not connected to a skald.
 */
bool is_standalone();

} // namespace skal
