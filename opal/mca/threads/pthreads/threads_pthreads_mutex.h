/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2018 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2015-2016 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2019      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2020      Triad National Security, LLC. All rights
 *                         reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_MUTEX_H
#define OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_MUTEX_H

/**
 * @file:
 *
 * Mutual exclusion functions: Unix implementation.
 *
 * Functions for locking of critical sections.
 *
 * On unix, use pthreads or our own atomic operations as
 * available.
 */

#include "opal_config.h"

#include <pthread.h>
#include <errno.h>
#include <stdio.h>

#include "opal/class/opal_object.h"
#include "opal/sys/atomic.h"
#include "opal/util/output.h"

BEGIN_C_DECLS

struct opal_mutex_t {
    opal_object_t super;

    pthread_mutex_t m_lock_pthread;

#if OPAL_ENABLE_DEBUG
    int m_lock_debug;
    const char *m_lock_file;
    int m_lock_line;
#endif

    opal_atomic_lock_t m_lock_atomic;
    void *data;
};
/*
 * Assume that all the other thread implementations use the same struct size.
 * If it is the case, we can define opal_thread_t here.
 * Also, if we prioritize the implementation of "pthreads", we can statically
 * initialize it by PTHREAD_MUTEX_INITIALIZER etc since Argobots, for
 * example can refer to dynamically allocated memory region for its
 * implementation.  This is bad for non-Pthreads-based implementation, though.
 * If we can use C++'s dynamic initialization, we can fix it.
 *
 * Otherwise, we need to use an atomic trick (see Argobots' opal_mutex_create).
 *
 * In any case, we can add one "void *data" and to prepare for potential other
 * thread implementations (otherwise MCA is not generic), which should be the
 * smallest overhead for the Pthreads implementation.
 */

OPAL_DECLSPEC OBJ_CLASS_DECLARATION(opal_mutex_t);
OPAL_DECLSPEC OBJ_CLASS_DECLARATION(opal_recursive_mutex_t);

#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
#define OPAL_PTHREAD_RECURSIVE_MUTEX_INITIALIZER \
            PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#elif defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER)
#define OPAL_PTHREAD_RECURSIVE_MUTEX_INITIALIZER \
            PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif

#if OPAL_ENABLE_DEBUG
#define OPAL_MUTEX_STATIC_INIT                                          \
    {                                                                   \
        .super = OPAL_OBJ_STATIC_INIT(opal_mutex_t),                    \
        .m_lock_pthread = PTHREAD_MUTEX_INITIALIZER,                    \
        .m_lock_debug = 0,                                              \
        .m_lock_file = NULL,                                            \
        .m_lock_line = 0,                                               \
        .m_lock_atomic = OPAL_ATOMIC_LOCK_INIT,                         \
        .data = NULL,                                                   \
    }
#else
#define OPAL_MUTEX_STATIC_INIT                                          \
    {                                                                   \
        .super = OPAL_OBJ_STATIC_INIT(opal_mutex_t),                    \
        .m_lock_pthread = PTHREAD_MUTEX_INITIALIZER,                    \
        .m_lock_atomic = OPAL_ATOMIC_LOCK_INIT,                         \
        .data = NULL,                                                   \
    }
#endif

#if defined(OPAL_PTHREAD_RECURSIVE_MUTEX_INITIALIZER)

#if OPAL_ENABLE_DEBUG
#define OPAL_RECURSIVE_MUTEX_STATIC_INIT                                \
    {                                                                   \
        .super = OPAL_OBJ_STATIC_INIT(opal_mutex_t),                    \
        .m_lock_pthread = OPAL_PTHREAD_RECURSIVE_MUTEX_INITIALIZER,     \
        .m_lock_debug = 0,                                              \
        .m_lock_file = NULL,                                            \
        .m_lock_line = 0,                                               \
        .m_lock_atomic = OPAL_ATOMIC_LOCK_INIT,                         \
        .data = (void *)0x1,                                            \
    }
#else
#define OPAL_RECURSIVE_MUTEX_STATIC_INIT                                \
    {                                                                   \
        .super = OPAL_OBJ_STATIC_INIT(opal_mutex_t),                    \
        .m_lock_pthread = OPAL_PTHREAD_RECURSIVE_MUTEX_INITIALIZER,     \
        .m_lock_atomic = OPAL_ATOMIC_LOCK_INIT,                         \
        .data = (void *)0x1,                                            \
    }
#endif

#endif

/************************************************************************
 *
 * mutex operations (non-atomic versions)
 *
 ************************************************************************/

OPAL_DECLSPEC int opal_mutex_trylock(opal_mutex_t *m);
OPAL_DECLSPEC void opal_mutex_lock(opal_mutex_t *m);
OPAL_DECLSPEC void opal_mutex_unlock(opal_mutex_t *m);

/************************************************************************
 *
 * mutex operations (atomic versions)
 *
 ************************************************************************/

OPAL_DECLSPEC int opal_mutex_atomic_trylock(opal_mutex_t *m);
OPAL_DECLSPEC void opal_mutex_atomic_lock(opal_mutex_t *m);
OPAL_DECLSPEC void opal_mutex_atomic_unlock(opal_mutex_t *m);

typedef pthread_cond_t opal_cond_t;

#define OPAL_CONDITION_STATIC_INIT PTHREAD_COND_INITIALIZER
/* This has the same issue, but so far there is no good solution.  Maybe
 * Argobots/Qthreads can detect if this is initialize for Pthreads cond.
 * Fortunately, no module uses this OPAL_CONDITION_STATIC_INIT, so it's okay. */

int opal_cond_init(opal_cond_t *cond);
int opal_cond_wait(opal_cond_t *cond, opal_mutex_t *lock);
int opal_cond_broadcast(opal_cond_t *cond);
int opal_cond_signal(opal_cond_t *cond);
int opal_cond_destroy(opal_cond_t *cond);

END_C_DECLS

#endif /* OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_MUTEX_H */
