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

#ifndef SKAL_PLF_h_
#define SKAL_PLF_h_

/** Platform-dependent stuff for SKAL
 *
 * \defgroup skalplf Platform-dependent stuff for SKAL
 * \addtogroup skalplf
 * @{
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>



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



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Generate a random string
 *
 * \param buffer [out] Where to write the random string
 * \param size_B [in]  Number of bytes to generate
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


/** Create a mutex
 *
 * \return A newly created mutex; this function never returns NULL
 */
SkalPlfMutex* SkalPlfMutexCreate(void);


/** Destroy a mutex
 *
 * No thread should be waiting on this mutex when this function is called.
 *
 * \param mutex [in,out] Mutex to destroy; must not be NULL
 */
void SkalPlfMutexDestroy(SkalPlfMutex* mutex);


/** Lock a mutex
 *
 * NB: Recursive locking is not supported.
 *
 * \param mutex [in,out] Mutex to lock; must not be NULL
 */
void SkalPlfMutexLock(SkalPlfMutex* mutex);


/** Unlock a mutex
 *
 * \param mutex [in,out] Mutex to unlock; must not be NULL
 */
void SkalPlfMutexUnlock(SkalPlfMutex* mutex);


/** Create a condition variable
 *
 * \return A newly created condition variable; this function never returns NULL
 */
SkalPlfCondVar* SkalPlfCondVarCreate(void);


/** Destroy a condition variable
 *
 * \param condvar [in,out] Condition variable to destroy; must not be NULL
 */
void SkalPlfCondVarDestroy(SkalPlfCondVar* condvar);


/** Wait on a condition variable
 *
 * \param condvar [in,out] Condition variable to wait on; must not be NULL
 * \param mutex   [in,out] Mutex associated with the condition variable; must
 *                         not be NULL
 */
void SkalPlfCondVarWait(SkalPlfCondVar* condvar, SkalPlfMutex* mutex);


/** Wake up one thread currently waiting on a condition variable
 *
 * \param condvar [in,out] Condition variable to signal
 */
void SkalPlfCondVarSignal(SkalPlfCondVar* condvar);


/** Create a thread
 *
 * The thread starts immediately.
 *
 * \param name     [in] Name of the new thread; may be NULL if no name needed
 * \param threadfn [in] Function that implements the thread; must not be NULL
 * \param arg      [in] Argument to pass to `threadfn`
 *
 * \return The newly created thread; this function never returns NULL
 */
SkalPlfThread* SkalPlfThreadCreate(const char* name,
        SkalPlfThreadFunction threadfn, void* arg);


/** Cancel a thread
 *
 * Once this function returns, please call `SkalPlfThreadJoin()` on it to free
 * up its resources.
 *
 * \param thread [in,out] Thread to cancel
 */
void SkalPlfThreadCancel(SkalPlfThread* thread);


/* Join a thread
 *
 * If the `thread` is still running when this function is called, it will block
 * until the thread terminates.
 *
 * All resources associated with this thread are freed once this function
 * returns.
 *
 * \param thread [in,out] Thread to join
 */
void SkalPlfThreadJoin(SkalPlfThread* thread);


/** Get the name of the current thread
 *
 * \param buffer [out] Where to write the current thread's name; must not be
 *                     NULL
 * \param size   [in]  Size of the above buffer, in chars
 */
void SkalPlfGetCurrentThreadName(char* buffer, int size);



/* @} */
#endif /* SKAL_PLF_h_ */
