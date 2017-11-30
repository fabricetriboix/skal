/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>

namespace skal {

/** Get the domain this process belongs to */
const std::string& domain();

/** Get the name of the current worker */
const std::string& me();

struct global_t
{
private:
    /** Set the domain this process belongs to
     *
     * \param domain [in] Domain this process belongs to
     */
    static void set_domain(std::string domain);

    /** Set the name of the current thread
     *
     * \param me [in] Name of current thread
     */
    static void set_me(std::string me);
};

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
