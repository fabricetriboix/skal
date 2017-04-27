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

#include "skald-alarm.h"
#include "cdsmap.h"
#include <stdarg.h>



/*----------------+
 | Macros & Types |
 +----------------*/


/** Item of the alarm map
 *
 * We don't keep track of the reference count because this item is only
 * ever referenced once by the `gAlarms` map.
 */
typedef struct {
    CdsMapItem item;
    char*      key; /**< "alarm-type#alarm-origin" */
    SkalAlarm* alarm;
} skaldAlarm;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** De-reference an item from the alarm map */
static void skaldAlarmUnref(CdsMapItem* mitem);



/*------------------+
 | Global variables |
 +------------------*/


/** Alarms currently active
 *
 * Map is made of `skaldAlarm`, and the key is `skaldAlarm->key`.
 */
static CdsMap* gAlarms = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldAlarmInit(void)
{
    SKALASSERT(NULL == gAlarms);

    gAlarms = CdsMapCreate("alarms",          // name
                           0,                 // capacity
                           SkalStringCompare, // compare
                           NULL,              // cookie
                           NULL,              // keyUnref
                           skaldAlarmUnref);  // itemUnref
}


void SkaldAlarmExit(void)
{
    CdsMapDestroy(gAlarms);
    gAlarms = NULL;
}


void SkaldAlarmProcess(SkalAlarm* alarm)
{
    const char* origin = SkalAlarmOrigin(alarm);
    if (NULL == origin) {
        origin = "";
    }
    char* key = SkalSPrintf("%s#%s", origin, SkalAlarmName(alarm));

    if (SkalAlarmIsOn(alarm)) {
        skaldAlarm* item = SkalMallocZ(sizeof(*item));
        item->key = key;
        item->alarm = alarm;
        bool inserted = CdsMapInsert(gAlarms, item->key, &item->item);
        SKALASSERT(inserted);
    } else {
        (void)CdsMapRemove(gAlarms, key);
        SkalAlarmUnref(alarm);
        free(key);
    }
}


void SkaldAlarmNew(const char* name, SkalAlarmSeverityE severity,
        bool isOn, bool autoOff, const char* format, ...)
{
    char* comment = NULL;
    if (format != NULL) {
        va_list ap;
        va_start(ap, format);
        comment = SkalVSPrintf(format, ap);
        va_end(ap);
    }

    SkalAlarm* alarm = NULL;
    if (comment != NULL) {
        alarm = SkalAlarmCreate(name, severity, isOn, autoOff, "%s", comment);
    } else {
        alarm = SkalAlarmCreate(name, severity, isOn, autoOff, NULL);
    }
    SkaldAlarmProcess(alarm);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skaldAlarmUnref(CdsMapItem* mitem)
{
    skaldAlarm* item = (skaldAlarm*)mitem;
    SKALASSERT(item != NULL);
    SKALASSERT(item->alarm != NULL);
    SkalAlarmUnref(item->alarm);
    free(item->key);
    free(item);
}
