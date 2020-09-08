/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2016 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2019      Sandia National Laboratories.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "opal_config.h"

#include <errno.h>
#include <pthread.h>

#include "opal/mca/threads/mutex.h"
#include "opal/mca/threads/tsd.h"
#include "opal/mca/threads/pthreads/threads_pthreads_mutex.h"
#include "opal/constants.h"

/*
 * Wait and see if some upper layer wants to use threads, if support
 * exists.
 */
bool opal_uses_threads = false;

struct opal_pthread_mutex_t {
    opal_object_t super;

    pthread_mutex_t m_lock_pthread;
    opal_atomic_lock_t m_lock_atomic;

#if OPAL_ENABLE_DEBUG
    int m_lock_debug;
    const char *m_lock_file;
    int m_lock_line;
#endif
};

typedef struct opal_pthread_mutex_t opal_pthread_mutex_t;
typedef struct opal_pthread_mutex_t opal_pthread_recursive_mutex_t;

static void mca_threads_pthreads_mutex_constructor(opal_mutex_t *p_mutex)
{
    pthread_mutex_init(&p_mutex->m_lock_pthread, NULL);
#if OPAL_ENABLE_DEBUG
    p_mutex->m_lock_debug = 0;
    p_mutex->m_lock_file = NULL;
    p_mutex->m_lock_line = 0;
#endif
    opal_atomic_lock_init(&p_mutex->m_lock_atomic, 0);
}

static void mca_threads_pthreads_mutex_destructor(opal_mutex_t *p_mutex)
{
    pthread_mutex_destroy(&p_mutex->m_lock_pthread);
}

static void mca_threads_pthreads_recursive_mutex_constructor
        (opal_recursive_mutex_t *p_mutex)
{
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&p_mutex->m_lock_pthread, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
#if OPAL_ENABLE_DEBUG
    p_mutex->m_lock_debug = 0;
    p_mutex->m_lock_file = NULL;
    p_mutex->m_lock_line = 0;
#endif
    opal_atomic_lock_init(&p_mutex->m_lock_atomic, 0);
}

static void mca_threads_pthreads_recursive_mutex_destructor
        (opal_recursive_mutex_t *p_mutex)
{
    pthread_mutex_destroy(&p_mutex->m_lock_pthread);
}

OBJ_CLASS_INSTANCE(opal_mutex_t,
                   opal_object_t,
                   mca_threads_pthreads_mutex_constructor,
                   mca_threads_pthreads_mutex_destructor);

OBJ_CLASS_INSTANCE(opal_recursive_mutex_t,
                   opal_object_t,
                   mca_threads_pthreads_recursive_mutex_constructor,
                   mca_threads_pthreads_recursive_mutex_destructor);

static inline int opal_mutex_trylock_impl(opal_mutex_t *m)
{
    int ret = pthread_mutex_trylock(&m->m_lock_pthread);
    if (EDEADLK == ret) {
#if OPAL_ENABLE_DEBUG
        opal_output(0, "opal_mutex_trylock() %d",ret);
#endif
        return 1;
    }
    return 0 == ret ? 0 : 1;
}

static inline void opal_mutex_lock_impl(opal_mutex_t *m)
{
#if OPAL_ENABLE_DEBUG
    int ret = pthread_mutex_lock(&m->m_lock_pthread);
    if (EDEADLK == ret) {
        errno = ret;
        opal_output(0, "opal_mutex_lock() %d", ret);
    }
#else
    pthread_mutex_lock(&m->m_lock_pthread);
#endif
}

static inline void opal_mutex_unlock_impl(opal_mutex_t *m)
{
#if OPAL_ENABLE_DEBUG
    int ret = pthread_mutex_unlock(&m->m_lock_pthread);
    if (EPERM == ret) {
        errno = ret;
        opal_output(0, "opal_mutex_unlock() %d", ret);
    }
#else
    pthread_mutex_unlock(&m->m_lock_pthread);
#endif
}

int opal_mutex_trylock(opal_mutex_t *m)
{
    return opal_mutex_trylock_impl(m);
}

void opal_mutex_lock(opal_mutex_t *m)
{
    opal_mutex_lock_impl(m);
}

void opal_mutex_unlock(opal_mutex_t *m)
{
    opal_mutex_unlock_impl(m);
}

#if OPAL_HAVE_ATOMIC_SPINLOCKS

/************************************************************************
 * Spin Locks
 ************************************************************************/

int opal_mutex_atomic_trylock(opal_mutex_t *m)
{
    return opal_atomic_trylock(&m->m_lock_atomic);
}

void opal_mutex_atomic_lock(opal_mutex_t *m)
{
    opal_atomic_lock(&m->m_lock_atomic);
}

void opal_mutex_atomic_unlock(opal_mutex_t *m)
{
    opal_atomic_unlock(&m->m_lock_atomic);
}

#else

/************************************************************************
 * Standard locking
 ************************************************************************/

int opal_mutex_atomic_trylock(opal_mutex_t *m)
{
    return opal_mutex_trylock_impl(m);
}

void opal_mutex_atomic_lock(opal_mutex_t *m)
{
    opal_mutex_lock_impl(m);
}

void opal_mutex_atomic_unlock(opal_mutex_t *m)
{
    opal_mutex_unlock_impl(m);
}

#endif

int opal_cond_init(opal_cond_t *cond)
{
    int ret = pthread_cond_init(cond, NULL);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_cond_wait(opal_cond_t *cond, opal_mutex_t *lock)
{
    int ret = pthread_cond_wait(cond, &lock->m_lock_pthread);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_cond_broadcast(opal_cond_t *cond)
{
    int ret = pthread_cond_broadcast(cond);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_cond_signal(opal_cond_t *cond)
{
    int ret = pthread_cond_signal(cond);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_cond_destroy(opal_cond_t *cond)
{
    int ret = pthread_cond_destroy(cond);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_tsd_key_delete(opal_tsd_key_t key)
{
    int ret = pthread_key_delete(key);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_tsd_set(opal_tsd_key_t key, void *value)
{
    int ret = pthread_setspecific(key, value);
    return 0 == ret ? OPAL_SUCCESS : OPAL_ERR_IN_ERRNO;
}

int opal_tsd_get(opal_tsd_key_t key, void **valuep)
{
    *valuep = pthread_getspecific(key);
    return OPAL_SUCCESS;
}
