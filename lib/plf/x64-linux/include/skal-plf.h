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

#ifndef SKAL_PLF_h_
#define SKAL_PLF_h_

#ifdef __cplusplus
extern "C" {
#endif


/** Platform-dependent stuff for SKAL
 *
 * @defgroup skalplf Platform-dependent stuff for SKAL
 * @addtogroup skalplf
 * @{
 */

#include "skal-cfg.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef SKAL_WITH_FLLOC
#include <flloc.h>
#endif



/*----------------+
 | Types & Macros |
 +----------------*/


/** Panic macro */
#define SKALPANIC \
    do { \
        fprintf(stderr, "SKAL PANIC at %s:%d\n", __FILE__, __LINE__); \
        abort(); \
    } while (0)


/** Panic macro with message */
#define SKALPANIC_MSG(_fmt, ...) \
    do { \
        fprintf(stderr, "SKAL PANIC: "); \
        fprintf(stderr, (_fmt), ## __VA_ARGS__); \
        fprintf(stderr, " (at %s:%d)\n", __FILE__, __LINE__); \
        abort(); \
    } while (0)


/** Assert macro */
#define SKALASSERT(_cond) \
    do { \
        if (!(_cond)) { \
            fprintf(stderr, "SKAL ASSERT: %s (at %s:%d)\n", #_cond, \
                    __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)


/** Opaque type representing a bare mutex
 *
 * Please note that if you follow the skal framework, you should not use any
 * mutex in your code. You might want to use mutexes in exceptional
 * circumstances, typically because it would be difficult to integrate a
 * third-party software without them.
 *
 * Mutexes are evil. Always try to design your code so that you don't need them.
 */
typedef struct SkalPlfMutex SkalPlfMutex;


/** Opaque type representing a bare condition variable
 *
 * Please note that if you follow the skal framework, you should not use any
 * condition variable in your code. You might want to use condition variables in
 * exceptional circumstances, typically because it would be difficult to
 * integrate a third-party software without them.
 *
 * Condition variables are evil. Always try to design your code so that you
 * don't need them.
 */
typedef struct SkalPlfCondVar SkalPlfCondVar;


/** Opaque type representing a bare thread */
typedef struct SkalPlfThread SkalPlfThread;


/** Function implementing a thread
 *
 * The `arg` argument is the same as the one passed to
 * `SkalPlfThreadCreate()`.
 */
typedef void (*SkalPlfThreadFunction)(void* arg);


/** Opaque type representing a regex */
typedef struct SkalPlfRegex SkalPlfRegex;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise this module */
void SkalPlfInit(void);


/** De-initialise this module */
void SkalPlfExit(void);


/** Generate a random string
 *
 * @param buffer [out] Where to write the random string
 * @param size_B [in]  Number of bytes to generate
 */
void SkalPlfRandom(uint8_t* buffer, int size_B);


/** Generate a random 32-bit number */
static inline uint32_t SkalPlfRandomU32(void)
{
    uint32_t x;
    SkalPlfRandom((uint8_t*)&x, sizeof(x));
    return x;
}


/** Generate a random 64-bit number */
static inline uint64_t SkalPlfRandomU64(void)
{
    uint64_t x;
    SkalPlfRandom((uint8_t*)&x, sizeof(x));
    return x;
}


/** Get the current time in ns relative to an unspecified reference
 *
 * This is a time that increments linearly in reference to an external clock.
 * Thus it is not influenced by daylight savings time shifts, time zone changes,
 * date or time changes, etc.
 *
 * On Linux, this is the time elapsed since the last boot.
 */
int64_t SkalPlfNow_ns();


/** Get the current time in us relative to the Epoch
 *
 * This is the number of micro-seconds between the Epoch and now.
 *
 * The Epoch is 1970-01-01 00:00:00 +0000 (UTC).
 *
 * @return The number of micro-seconds between the Epoch and now.
 */
int64_t SkalPlfNow_us();


/** Print a timestamp in ISO-8601 format, UTC time zone
 *
 * The timestamp will look like this:
 *
 *    yyyy-mm-ddThh:mm:ss.uuuuuuZ
 *
 * Where:
 *  - yyyy is the year on 4 digits
 *  - mm is the month of the year
 *  - dd is the day of the month
 *  - hh is the hour of the day (24 hour clock)
 *  - mm is the minute in the hour
 *  - ss is the second
 *  - uuuuuu is the micro-second
 *
 * Please note that if the provided string is too short, the timestamp will be
 * silently truncated. The output string is null-terminated in any case.
 *
 * @param us   [in]  Number of micro-seconds since the Epoch
 * @param ts   [out] Where to write the timestamp; must not be NULL
 * @param size [in]  Size of `ts`, in characters
 *
 * @return A string that is the timestamp
 */
void SkalPlfTimestamp(int64_t us, char* ts, int size);


/** Parse a timestamp in ISO-8601 format, UTC time zone
 *
 * The timestamp is a string that must look like this:
 *
 *    yyyy-mm-ddThh:mm:ss.uuuuuuZ
 *
 * @param ts [in]  Timestamp string to parse; must not be NULL
 * @param us [out] Parsed time: number of micro-seconds since the Epoch; must
 *                 not be NULL
 *
 * @return `true` if OK, `false` if invalid timestamp
 */
bool SkalPlfParseTimestamp(const char* ts, int64_t* us);


/** Create a mutex
 *
 * @return A newly created mutex; this function never returns NULL
 */
SkalPlfMutex* SkalPlfMutexCreate(void);


/** Destroy a mutex
 *
 * No thread should be waiting on this mutex when this function is called.
 *
 * @param mutex [in,out] Mutex to destroy; must not be NULL
 */
void SkalPlfMutexDestroy(SkalPlfMutex* mutex);


/** Lock a mutex
 *
 * NB: Recursive locking is not supported.
 *
 * @param mutex [in,out] Mutex to lock; must not be NULL
 */
void SkalPlfMutexLock(SkalPlfMutex* mutex);


/** Unlock a mutex
 *
 * @param mutex [in,out] Mutex to unlock; must not be NULL
 */
void SkalPlfMutexUnlock(SkalPlfMutex* mutex);


/** Create a condition variable
 *
 * @return A newly created condition variable; this function never returns NULL
 */
SkalPlfCondVar* SkalPlfCondVarCreate(void);


/** Destroy a condition variable
 *
 * @param condvar [in,out] Condition variable to destroy; must not be NULL
 */
void SkalPlfCondVarDestroy(SkalPlfCondVar* condvar);


/** Wait on a condition variable
 *
 * @param condvar [in,out] Condition variable to wait on; must not be NULL
 * @param mutex   [in,out] Mutex associated with the condition variable; must
 *                         not be NULL
 */
void SkalPlfCondVarWait(SkalPlfCondVar* condvar, SkalPlfMutex* mutex);


/** Wake up one thread currently waiting on a condition variable
 *
 * @param condvar [in,out] Condition variable to signal
 */
void SkalPlfCondVarSignal(SkalPlfCondVar* condvar);


/** Create a thread
 *
 * The thread starts immediately.
 *
 * @param name     [in] Name of the new thread; may be NULL if no name needed
 * @param threadfn [in] Function that implements the thread; must not be NULL
 * @param arg      [in] Argument to pass to `threadfn`
 *
 * @return The newly created thread; this function never returns NULL
 */
SkalPlfThread* SkalPlfThreadCreate(const char* name,
        SkalPlfThreadFunction threadfn, void* arg);


/** Join a thread
 *
 * If the `thread` is still running when this function is called, it will block
 * until the thread terminates.
 *
 * All resources associated with this thread are freed once this function
 * returns.
 *
 * @param thread [in,out] Thread to join
 */
void SkalPlfThreadJoin(SkalPlfThread* thread);


/** Get the name of the current thread
 *
 * @return The thread name, or NULL if no name has been set
 */
const char* SkalPlfThreadGetName(void);


/** Get the system thread name of the current thread
 *
 * @return The system thread name; this function never returns NULL; please call
 *         `free(3)` on it when finished.
 */
char* SkalPlfGetSystemThreadName(void);


/** Set the thread-specific value
 *
 * Please note only one such value can be held for each thread. If you call this
 * function a second time, the new value will silently overwrite the old value.
 *
 * @param value [in] Value to set, specific to the current thread
 */
void SkalPlfThreadSetSpecific(void* value);


/** Get the thread-specific value
 *
 * @return The thread-specific value, or NULL if it was not set
 */
void* SkalPlfThreadGetSpecific(void);


/** Check if the current thread is managed by SKAL or not
 *
 * @return `true` if the current thread is managed by SKAL, `false` if not
 */
bool SkalPlfThreadIsSkal(void);


/** Get the pid or tid of the current thread */
int SkalPlfTid(void);


/** Path to system-wide temporary directory
 *
 * @return Absolute path to system-wide temporary directory; this function never
 *         returns NULL
 */
const char* SkalPlfTmpDir(void);


/** Directory separator character */
char SkalPlfDirSep(void);


/** Make the current thread "SKAL-compliant"
 *
 * This is a debug function intended only for unit tests.
 */
void SkalPlfThreadMakeSkal_DEBUG(const char* name);


/** Un-make the current thread "SKAL-compliant"
 *
 * This is a debug function intended only for unit tests.
 */
void SkalPlfThreadUnmakeSkal_DEBUG(void);


/** Create a regex object to subsequent matching
 *
 * @param pattern [in] The pattern to match; must not be NULL
 *
 * @return The regex object, or NULL if `pattern` is not a valid regex; please
 *         call `SkalPlfRegexDestroy()` on the regex object when you're finished
 */
SkalPlfRegex* SkalPlfRegexCreate(const char* pattern);


/** Destroy a regex object
 *
 * @param regex [in] The regex object to destroy; may be NULL
 */
void SkalPlfRegexDestroy(SkalPlfRegex* regex);


/** Match a string against a regex
 *
 * @param regex [in] The regex object to match against; must not be NULL
 * @param str   [in] The string to match; must not be NULL
 *
 * @return Whether `str` matches `pattern`
 */
bool SkalPlfRegexRun(const SkalPlfRegex* regex, const char* str);



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKAL_PLF_h_ */
