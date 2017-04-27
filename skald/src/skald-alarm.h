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

#ifndef SKALD_ALARM_h_
#define SKALD_ALARM_h_

#ifdef __cplusplus
extern "C"
#endif

#include "skal-alarm.h"



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise the skald-alarm module */
void SkaldAlarmInit(void);


/** De-initialise the skald-alarm module */
void SkaldAlarmExit(void);


/** Process a new alarm
 *
 * The alarm is raised if the `alarm` is on, or lowered if it is off.
 *
 * The ownership of `alarm` is transferred to this function
 *
 * @param alarm [in] Alarm to process; must not be NULL
 */
void SkaldAlarmProcess(SkalAlarm* alarm);


/** Helper function to create a process a new alarm in one call
 *
 * This is equivalent to calling `SkalAlarmCreate()` followed by
 * `SkaldAlarmProcess()`.
 */
void SkaldAlarmNew(const char* name, SkalAlarmSeverityE severity,
        bool isOn, bool autoOff, const char* format, ...)
    __attribute__(( format(printf, 5, 6) ));



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKALD_ALARM_h_ */
