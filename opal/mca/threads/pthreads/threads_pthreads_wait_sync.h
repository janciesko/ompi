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
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */


#ifndef OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_WAIT_SYNC_H
#define OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_WAIT_SYNC_H

typedef struct ompi_wait_sync_t {
    opal_atomic_int32_t count;
    int32_t status;
    pthread_cond_t condition;
    pthread_mutex_t lock;
    struct ompi_wait_sync_t *next;
    struct ompi_wait_sync_t *prev;
    volatile bool signaling;
} ompi_wait_sync_t;
/* Assume that all the other thread implementations use the same struct size.
 * If it is the case, we can define opal_thread_t here. */

OPAL_DECLSPEC int SYNC_WAIT(ompi_wait_sync_t *sync);

/* The loop in release handles a race condition between the signaling
 * thread and the destruction of the condition variable. The signaling
 * member will be set to false after the final signaling thread has
 * finished operating on the sync object. This is done to avoid
 * extra atomics in the signalling function and keep it as fast
 * as possible. Note that the race window is small so spinning here
 * is more optimal than sleeping since this macro is called in
 * the critical path. */
OPAL_DECLSPEC void WAIT_SYNC_RELEASE(ompi_wait_sync_t *sync);
OPAL_DECLSPEC void WAIT_SYNC_RELEASE_NOWAIT(ompi_wait_sync_t *sync);
OPAL_DECLSPEC void WAIT_SYNC_SIGNAL(ompi_wait_sync_t *sync);
OPAL_DECLSPEC void WAIT_SYNC_SIGNALLED(ompi_wait_sync_t *sync);

OPAL_DECLSPEC int ompi_sync_wait_mt(ompi_wait_sync_t *sync);
OPAL_DECLSPEC int sync_wait_st(ompi_wait_sync_t *sync);

OPAL_DECLSPEC void WAIT_SYNC_INIT(ompi_wait_sync_t *sync, int c);

#endif /* OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_WAIT_SYNC_H */
