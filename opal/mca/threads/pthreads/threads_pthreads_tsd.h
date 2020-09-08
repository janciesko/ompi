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


#ifndef OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_TSD_H
#define OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_TSD_H

#include <pthread.h>
#include <signal.h>

typedef pthread_key_t opal_tsd_key_t;
/* Assume that all the other thread implementations use the same struct size.
 * If it is the case, we can define opal_thread_t here. */
OPAL_DECLSPEC int opal_tsd_key_delete(opal_tsd_key_t key);
OPAL_DECLSPEC int opal_tsd_set(opal_tsd_key_t key, void *value);
OPAL_DECLSPEC int opal_tsd_get(opal_tsd_key_t key, void **valuep);
/* They are temporarily implemented in threads_pthreads_mutex.c */

#endif /* OPAL_MCA_THREADS_PTHREADS_THREADS_PTHREADS_TSD_H */
