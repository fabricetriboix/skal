/* Copyright (c) 2016  Fabrice Triboix
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SKAL_MSG_h_
#define SKAL_MSG_h_

#include "skal.h"



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Create a JSON string that represents the content of the message
 *
 * If any blob is attached to the message, please note they will only be
 * referenced by their ids in the JSON string. The content of each blob must be
 * passed using a separate mean.
 */
// TODO: from here


/** Create a message from a JSON string
 *
 * The created message will be a partial message if it contains any blob. Since
 * the blobs are not part of the JSON, they must be reconstructed outside this
 * function. Until all the blobs attached to this message are reconstructed,
 * this message will remain partial and can't be sent to anyone.
 *
 * \param json [in] JSON string to parse
 *
 * \return The newly created SKAL message, with its reference counter set to 1,
 *         or NULL if the JSON string is not valid.
 */
SkalMsg* SkalMsgCreateFromJson(const char* json);



#endif /* SKAL_MSG_h_ */
