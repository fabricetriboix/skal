/* Copyright (c) 2016,2017  Fabrice Triboix
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

#ifndef SKAL_ALARM_h_
#define SKAL_ALARM_h_

#ifdef __cplusplus
extern "C" {
#endif


#include "skal.h"



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Encode an alarm in JSON
 *
 * Please note the returned JSON string will not have a '\n' at the end.
 *
 * @param alarm   [in] Alarm to encode
 * @param nindent [in] How many spaces to use for initial indentation
 *
 * @return The JSON string representing the alarm; this function never returns
 *         NULL. Once finished with the JSON string, you must release it by
 *         calling `free()` on it.
 */
char* SkalAlarmToJson(const SkalAlarm* alarm, int nindent);


/** Create an alarm from a JSON string
 *
 * @param pJson [in,out]  in: JSON string to parse
 *                       out: Points to first character after parsed JSON alarm
 *
 * @return The newly created alarm, with its reference counter set to 1,
 *         or NULL if the JSON string is not valid.
 */
SkalAlarm* SkalAlarmCreateFromJson(const char** pJson);



#ifdef __cplusplus
}
#endif

#endif /* SKAL_ALARM_h_ */
