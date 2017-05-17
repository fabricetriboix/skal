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

// The following is required for `pthread_[sg]etname_np()`
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "skal-plf.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <regex.h>



/*----------------+
 | Types & Macros |
 +----------------*/


/** Size of the guard area of a thread, in bytes */
#define SKAL_GUARD_SIZE_B 1024


struct SkalPlfMutex {
    pthread_mutex_t m;
};


struct SkalPlfCondVar {
    pthread_cond_t cv;
};


struct SkalPlfThread {
    char*                 name;
    SkalPlfThreadFunction func;
    void*                 arg;
    pthread_t             id;
    void*                 specific;
    bool                  debug;
};


struct SkalPlfRegex {
    regex_t regex;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Free a thread-specific data */
static void skalPlfThreadSpecificFree(void* arg);


/** Entry point for a POSIX thread
 *
 * @param arg [in] Argument; actually a `SkalPlfThread*`
 *
 * @return Always NULL
 */
static void* skalPlfThreadRun(void* arg);



/*------------------+
 | Global variables |
 +------------------*/


/** Key to thread-specific values */
static pthread_key_t gKey;


/** Access to "/dev/urandom" */
static int gRandomFd = -1;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalPlfInit(void)
{
    // Ignore SIGPIPE; this evil signal makes the process terminate by default
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    int ret = sigaction(SIGPIPE, &sa, NULL);
    SKALASSERT(0 == ret);

    ret = pthread_key_create(&gKey, &skalPlfThreadSpecificFree);
    SKALASSERT(0 == ret);

    gRandomFd = open("/dev/urandom", O_RDONLY);
    SKALASSERT(gRandomFd >= 0);
}


void SkalPlfExit(void)
{
    int ret = pthread_key_delete(gKey);
    SKALASSERT(0 == ret);

    ret = close(gRandomFd);
    SKALASSERT(0 == ret);
    gRandomFd = -1;
}


void SkalPlfRandom(uint8_t* buffer, int size_B)
{
    SKALASSERT(gRandomFd >= 0);
    while (size_B > 0) {
        int ret = read(gRandomFd, buffer, size_B);
        SKALASSERT(ret > 0);
        size_B -= ret;
        buffer += ret;
    }
}


int64_t SkalPlfNow_ns()
{
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    SKALASSERT(ret == 0);
    return ((int64_t)ts.tv_sec * 1000000000LL) + (int64_t)ts.tv_nsec;
}


int64_t SkalPlfNow_us()
{
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    SKALASSERT(ret == 0);
    return ((int64_t)ts.tv_sec * 1000000LL) + ((int64_t)ts.tv_nsec / 1000LL);
}


void SkalPlfTimestamp(int64_t us, char* ts, int size)
{
    SKALASSERT(ts != NULL);
    SKALASSERT(size > 0);

    int64_t s = us / 1000000LL;
    us -= s * 1000000LL;
    if (us < 0) {
        us += 1000000LL;
        s -= 1LL;
    }

    time_t t = s;
    struct tm tm;
    struct tm* ret = gmtime_r(&t, &tm);
    SKALASSERT(ret != NULL);

    snprintf(ts, size, "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, (int)us);
}


SkalPlfMutex* SkalPlfMutexCreate(void)
{
    SkalPlfMutex* mutex = malloc(sizeof(*mutex));
    SKALASSERT(mutex != NULL);
    int ret = pthread_mutex_init(&mutex->m, NULL);
    SKALASSERT(ret == 0);
    return mutex;
}


void SkalPlfMutexDestroy(SkalPlfMutex* mutex)
{
    SKALASSERT(mutex != NULL);
    int ret = pthread_mutex_destroy(&mutex->m);
    SKALASSERT(ret == 0);
    free(mutex);
}


void SkalPlfMutexLock(SkalPlfMutex* mutex)
{
    SKALASSERT(mutex != NULL);
    int ret = pthread_mutex_lock(&mutex->m);
    SKALASSERT(ret == 0);
}


void SkalPlfMutexUnlock(SkalPlfMutex* mutex)
{
    SKALASSERT(mutex != NULL);
    int ret = pthread_mutex_unlock(&mutex->m);
    SKALASSERT(ret == 0);
}


SkalPlfCondVar* SkalPlfCondVarCreate(void)
{
    SkalPlfCondVar* condvar = malloc(sizeof(SkalPlfCondVar));
    SKALASSERT(condvar != NULL);
    int ret = pthread_cond_init(&condvar->cv, NULL);
    SKALASSERT(ret == 0);
    return condvar;
}


void SkalPlfCondVarDestroy(SkalPlfCondVar* condvar)
{
    SKALASSERT(condvar != NULL);
    int ret = pthread_cond_destroy(&condvar->cv);
    SKALASSERT(ret == 0);
    free(condvar);
}


void SkalPlfCondVarWait(SkalPlfCondVar* condvar, SkalPlfMutex* mutex)
{
    SKALASSERT(condvar != NULL);
    SKALASSERT(mutex != NULL);
    int ret = pthread_cond_wait(&condvar->cv, &mutex->m);
    SKALASSERT(ret == 0);
}


void SkalPlfCondVarSignal(SkalPlfCondVar* condvar)
{
    SKALASSERT(condvar != NULL);
    int ret = pthread_cond_signal(&condvar->cv);
    SKALASSERT(ret == 0);
}


SkalPlfThread* SkalPlfThreadCreate(const char* name,
        SkalPlfThreadFunction threadfn, void* arg)
{
    SKALASSERT(name != NULL);
    SKALASSERT(threadfn != NULL);

    SkalPlfThread* thread = malloc(sizeof(*thread));
    SKALASSERT(thread != NULL);
    memset(thread, 0, sizeof(*thread));

    thread->name = strdup(name);
    SKALASSERT(thread->name != NULL);
    thread->func = threadfn;
    thread->arg  = arg;

    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    SKALASSERT(ret == 0);
    ret = pthread_attr_setguardsize(&attr, SKAL_GUARD_SIZE_B);
    SKALASSERT(ret == 0);

    ret = pthread_create(&thread->id, &attr, skalPlfThreadRun, thread);
    SKALASSERT(ret == 0);

    ret = pthread_attr_destroy(&attr);
    SKALASSERT(ret == 0);

    return thread;
}


void SkalPlfThreadJoin(SkalPlfThread* thread)
{
    SKALASSERT(thread != NULL);
    int ret = pthread_join(thread->id, NULL);
    SKALASSERT(ret == 0);
    free(thread->name);
    free(thread);
}


const char* SkalPlfThreadGetName(void)
{
    SkalPlfThread* thread = pthread_getspecific(gKey);
    SKALASSERT(thread != NULL);
    SKALASSERT(thread->name != NULL);
    return thread->name;
}


char* SkalPlfGetSystemThreadName(void)
{
    char name[16];
    int ret = pthread_getname_np(pthread_self(), name, sizeof(name));
    SKALASSERT(0 == ret);

    char* s = malloc(16);
    SKALASSERT(s != NULL);
    memcpy(s, name, 16);
    s[15] = '\0';
    return s;
}


void SkalPlfThreadSetSpecific(void* value)
{
    SkalPlfThread* thread = pthread_getspecific(gKey);
    SKALASSERT(thread != NULL);
    thread->specific = value;
}


void* SkalPlfThreadGetSpecific(void)
{
    void* specific = NULL;
    SkalPlfThread* thread = pthread_getspecific(gKey);
    if (thread != NULL) {
        specific = thread->specific;
    }
    return specific;
}


bool SkalPlfThreadIsSkal(void)
{
    return (pthread_getspecific(gKey) != NULL);
}


int SkalPlfTid(void)
{
    return (int)syscall(SYS_gettid);
}


const char* SkalPlfTmpDir(void)
{
    return "/tmp";
}


char SkalPlfDirSep(void)
{
    return '/';
}


void SkalPlfThreadMakeSkal_DEBUG(const char* name)
{
    SKALASSERT(name != NULL);

    SkalPlfThread* thread = pthread_getspecific(gKey);
    SKALASSERT(NULL == thread);

    thread = malloc(sizeof(*thread));
    SKALASSERT(thread != NULL);
    memset(thread, 0, sizeof(*thread));
    thread->name = strdup(name);
    SKALASSERT(thread->name != NULL);
    thread->id = pthread_self();
    thread->debug = true;

    int ret = pthread_setspecific(gKey, thread);
    SKALASSERT(0 == ret);
}


void SkalPlfThreadUnmakeSkal_DEBUG(void)
{
    SkalPlfThread* thread = pthread_getspecific(gKey);
    SKALASSERT(thread != NULL);
    free(thread->name);
    free(thread);

    int ret = pthread_setspecific(gKey, NULL);
    SKALASSERT(0 == ret);
}


SkalPlfRegex* SkalPlfRegexCreate(const char* pattern)
{
    SKALASSERT(pattern != NULL);

    SkalPlfRegex* regex = malloc(sizeof(*regex));
    SKALASSERT(regex != NULL);
    memset(regex, 0, sizeof(*regex));

    int ret = regcomp(&regex->regex, pattern, 0);
    if (ret != 0) {
        free(regex);
        regex = NULL;
    }
    return regex;
}


void SkalPlfRegexDestroy(SkalPlfRegex* regex)
{
    if (regex != NULL) {
        regfree(&regex->regex);
        free(regex);
    }
}


bool SkalPlfRegexRun(SkalPlfRegex* regex, const char* str)
{
    SKALASSERT(regex != NULL);
    SKALASSERT(str != NULL);

    int ret = regexec(&regex->regex, str, 0, NULL, 0);
    return (0 == ret);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skalPlfThreadSpecificFree(void* arg)
{
    SkalPlfThread* thread = arg;
    if ((thread != NULL) && thread->debug) {
        free(thread->name);
        free(thread);
    }
}


static void* skalPlfThreadRun(void* arg)
{
    SkalPlfThread* thread = (SkalPlfThread*)arg;
    SKALASSERT(thread != NULL);
    SKALASSERT(thread->func != NULL);

    int ret = pthread_setspecific(gKey, thread);
    SKALASSERT(0 == ret);

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%s", thread->name);
    ret = pthread_setname_np(pthread_self(), tmp);
    SKALASSERT(0 == ret);

    thread->func(thread->arg);

    return NULL;
}
