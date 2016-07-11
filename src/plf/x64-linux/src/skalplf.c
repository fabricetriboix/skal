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

// The following is required for `pthread_[sg]etname_np()`
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "skalplf.h"
#include <pthread.h>



/*----------------+
 | Types & Macros |
 +----------------*/


/** Size of the guard area of a thread, in bytes */
#define SKAL_GUARD_SIZE_B 1024


struct SkalPlfMutex
{
    pthread_mutex_t m;
};


struct SkalPlfCondVar
{
    pthread_cond_t cv;
};


struct SkalPlfThread
{
    pthread_t id;
};



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


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
    // XXX SkalPlfCondVar* condvar = malloc(sizeof(*condvar));
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
    SkalPlfThread* thread = malloc(sizeof(*thread));
    SKALASSERT(thread != NULL);

    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    SKALASSERT(ret == 0);
    ret = pthread_attr_setguardsize(&attr, SKAL_GUARD_SIZE_B);
    SKALASSERT(ret == 0);

    ret = pthread_create(&thread->id, &attr, (void* (*)(void*))threadfn, arg);
    SKALASSERT(ret == 0);

    ret = pthread_attr_destroy(&attr);
    SKALASSERT(ret == 0);

    if ((name != NULL) && (name[0] != '\0')) {
        ret = pthread_setname_np(thread->id, name);
        SKALASSERT(ret == 0);
    }

    return thread;
}


void SkalPlfThreadCancel(SkalPlfThread* thread)
{
    SKALASSERT(thread != NULL);
    int ret = pthread_cancel(thread->id);
    SKALASSERT(ret == 0);
}


void SkalPlfThreadJoin(SkalPlfThread* thread)
{
    SKALASSERT(thread != NULL);
    int ret = pthread_join(thread->id, NULL);
    SKALASSERT(ret == 0);
    free(thread);
}


void SkalPlfGetCurrentThreadName(char* buffer, int size)
{
    SKALASSERT(buffer != NULL);
    SKALASSERT(size > 0);
    int ret = pthread_getname_np(pthread_self(), buffer, size);
    SKALASSERT(ret == 0);
}
