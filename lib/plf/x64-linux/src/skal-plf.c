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
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <regex.h>
#include <linux/limits.h>



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


struct SkalPlfShm {
    char*    name;        /**< shm name */
    char*    path;        /**< shm path, as used in `shm_open()` */
    int      fd;          /**< shm file */
    int64_t  size_B;      /**< Number of bytes initially requested */
    int64_t  totalSize_B; /**< Actual size of the shm, including its header */
    uint8_t* ptr;         /**< Start of shm if mapped, NULL if not mapped */
};


/** Structure at the beginning of a shared memory area */
typedef struct {
    int64_t ref;         /**< Reference counter */
    int64_t size_B;      /**< Number of bytes initially requested */
    int64_t totalSize_B; /**< Total size of the shm, including hdr */
    int64_t pad;
} skalPlfShmHeader;


/** Debug: How many references have we to shared memory areas? */
static int64_t gShmRefCount = 0;



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


/** Check the name of a shared memory area
 *
 * Assert if the name is not valid. If the name is valid, returns a string with
 * a '/' prepended to the name.
 *
 * @return Path suitable for Linux use; please call `free(3)` on it when
 *         finished; this function never returns NULL
 */
static char* skalPlfCheckShmName(const char* name);



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


bool SkalPlfParseTimestamp(const char* ts, int64_t* us)
{
    SKALASSERT(ts != NULL);
    SKALASSERT(us != NULL);

    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    if (sscanf(ts, "%d", &tm.tm_year) != 1) {
        return false;
    }
    tm.tm_year -= 1900;
    ts += 4;
    if ('-' != *ts) {
        return false;
    }
    ts++;

    if (sscanf(ts, "%d", &tm.tm_mon) != 1) {
        return false;
    }
    if ((tm.tm_mon < 1) || (tm.tm_mon > 12)) {
        return false;
    }
    tm.tm_mon--;
    ts += 2;
    if ('-' != *ts) {
        return false;
    }
    ts++;

    if (sscanf(ts, "%d", &tm.tm_mday) != 1) {
        return false;
    }
    ts += 2;
    if ('T' != *ts) {
        return false;
    }
    ts++;

    if (sscanf(ts, "%d", &tm.tm_hour) != 1) {
        return false;
    }
    ts += 2;
    if (':' != *ts) {
        return false;
    }
    ts++;

    if (sscanf(ts, "%d", &tm.tm_min) != 1) {
        return false;
    }
    ts += 2;
    if (':' != *ts) {
        return false;
    }
    ts++;

    if (sscanf(ts, "%d", &tm.tm_sec) != 1) {
        return false;
    }
    ts += 2;
    if ('.' != *ts) {
        return false;
    }
    ts++;

    // Skip zeros
    int count = 0;
    while ((*ts != '\0') && ('0' == *ts)) {
        ts++;
        count++;
    }
    if (count > 6) {
        return false;
    }
    long long tmp;
    if (sscanf(ts, "%lld", &tmp) != 1) {
        return false;
    }
    ts += (6 - count);
    if (*ts != 'Z') {
        return false;
    }

    time_t t = timegm(&tm);
    *us = (1000000LL * (int64_t)t) + tmp;
    return true;
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


bool SkalPlfRegexRun(const SkalPlfRegex* regex, const char* str)
{
    SKALASSERT(regex != NULL);
    SKALASSERT(str != NULL);

    int ret = regexec(&regex->regex, str, 0, NULL, 0);
    return (0 == ret);
}


SkalPlfShm* SkalPlfShmCreate(const char* name, int64_t size_B)
{
    char* path = skalPlfCheckShmName(name);
    SKALASSERT(size_B > 0);
    int fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, SKAL_SHM_PERM);
    if (fd < 0) {
        free(path);
        return NULL;
    }

    int64_t totalSize_B = (off_t)size_B + sizeof(skalPlfShmHeader);
    int ret = ftruncate(fd, (off_t)totalSize_B);
    if (ret < 0) {
        close(fd);
        shm_unlink(path);
        free(path);
        return NULL;
    }

    // Initialise shared memory header
    skalPlfShmHeader* hdr = (skalPlfShmHeader*)mmap(NULL,
            totalSize_B, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    SKALASSERT(hdr != MAP_FAILED);
    hdr->ref = 1;
    gShmRefCount++;
    hdr->size_B = size_B;
    hdr->totalSize_B = totalSize_B;

    SkalPlfShm* shm = malloc(sizeof(*shm));
    SKALASSERT(shm != NULL);
    shm->name = strdup(name);
    SKALASSERT(shm->name != NULL);
    shm->path = path;
    shm->fd = fd;
    shm->size_B = size_B;
    shm->totalSize_B = totalSize_B;
    shm->ptr = (uint8_t*)hdr;
    return shm;
}


SkalPlfShm* SkalPlfShmOpen(const char* name)
{
    char* path = skalPlfCheckShmName(name);
    int fd = shm_open(path, O_RDWR, SKAL_SHM_PERM);
    if (fd < 0) {
        free(path);
        return NULL;
    }

    skalPlfShmHeader* hdr = (skalPlfShmHeader*)mmap(NULL,
            sizeof(*hdr), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    SKALASSERT(hdr != MAP_FAILED);
    int64_t size_B = hdr->size_B;
    int64_t totalSize_B = hdr->totalSize_B;
    int ret = munmap(hdr, sizeof(*hdr));
    SKALASSERT(0 == ret);

    SkalPlfShm* shm = malloc(sizeof(*shm));
    SKALASSERT(shm != NULL);
    shm->name = strdup(name);
    SKALASSERT(shm->name != NULL);
    shm->path = path;
    shm->fd = fd;
    shm->size_B = size_B;
    shm->totalSize_B = totalSize_B;
    shm->ptr = NULL;
    return shm;
}


void SkalPlfShmClose(SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    SkalPlfShmUnmap(shm);
    if (shm->fd >= 0) {
        close(shm->fd);
    }
    free(shm->name);
    free(shm->path);
    free(shm);
}


void SkalPlfShmRef(SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    if (NULL == shm->ptr) {
        (void)SkalPlfShmMap(shm);
    }
    skalPlfShmHeader* hdr = (skalPlfShmHeader*)shm->ptr;
    (hdr->ref)++;
    gShmRefCount++;
}


void SkalPlfShmUnref(SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    if (NULL == shm->ptr) {
        (void)SkalPlfShmMap(shm);
    }
    skalPlfShmHeader* hdr = (skalPlfShmHeader*)shm->ptr;
    (hdr->ref)--;
    gShmRefCount--;
    if (hdr->ref <= 0) {
        SkalPlfShmUnmap(shm);
        close(shm->fd);
        shm->fd = -1;
        shm_unlink(shm->path);
    }
}


uint8_t* SkalPlfShmMap(SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    if (NULL == shm->ptr) {
        shm->ptr = mmap(NULL,
            shm->totalSize_B, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
        SKALASSERT(shm->ptr != MAP_FAILED);
    }
    return shm->ptr + sizeof(skalPlfShmHeader);
}


void SkalPlfShmUnmap(SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    if (shm->ptr != NULL) {
        int ret = munmap(shm->ptr, shm->totalSize_B);
        SKALASSERT(0 == ret);
        shm->ptr = NULL;
    }
}


int64_t SkalPlfShmSize_B(const SkalPlfShm* shm)
{
    SKALASSERT(shm != NULL);
    return shm->size_B;
}


int64_t SkalPlfShmRefCount_DEBUG(void)
{
    return gShmRefCount;
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


static char* skalPlfCheckShmName(const char* name)
{
    SKALASSERT(name != NULL);
    for (size_t i = 0; i < (NAME_MAX - 1); i++) {
        char c = name[i];
        if ('\0' == c) {
            size_t len = i + 2;
            char* path = malloc(len);
            SKALASSERT(path != NULL);
            int n = snprintf(path, len, "/%s", name);
            SKALASSERT(n < (int)len);
            return path;
        }
        SKALASSERT((c >= 0x20) && (c <= 0x7e));
        SKALASSERT(c != '/');
    }
    SKALPANIC_MSG("String too long");
}
