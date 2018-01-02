/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/skal.hpp>
#include <skal/log.hpp>
#include <skal/util.hpp>

namespace skald {

struct parameters_t {
    /** Domain this skald belongs to
     *
     * This may be the empty string, in which case the default domain will be
     * used.
     */
    std::string domain;

    /** Local address to bind and listen to
     *
     * This is where skal processes can connect to.
     */
    std::string local_url;
};

/** Initialise skald
 *
 * This function returns when skald is up and running.
 *
 * \param params [in] Parameters for skald
 *
 * \throw `skal::bad_url` if `params.local_url` is not a valid socket URL
 */
void init(parameters_t params);

/** Terminate skald
 *
 * This function terminates skald, and blocks for a very short time until
 * skald is terminated.
 */
void terminate();

} // namespace skald
