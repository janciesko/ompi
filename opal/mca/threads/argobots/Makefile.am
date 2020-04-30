#
# Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2005 The University of Tennessee and The University
#                         of Tennessee Research Foundation.  All rights
#                         reserved.
# Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2008      Cisco Systems, Inc.  All rights reserved.
# Copyright (c) 2019      Sandia National Laboratories.  All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AM_CPPFLAGS = $(opal_argo_CPPFLAGS)

sources = \
    threads_argobots.h \
    threads_argobots_component.c \
    threads_argobots_condition.c \
    threads_argobots_module.c \
    threads_argobots_mutex.c \
    threads_argobots_mutex.h \
    threads_argobots_threads.h \
    threads_argobots_tsd.h \
    threads_argobots_wait_sync.c \
    threads_argobots_wait_sync.h

lib = libmca_threads_argobots.la
lib_sources = $(sources)

noinst_LTLIBRARIES = $(lib)
libmca_threads_argobots_la_SOURCES = $(lib_sources)
libmca_threads_argobots_la_LDFLAGS = -module -avoid-version $(opal_argo_LDFLAGS)
libmca_threads_argobots_la_LIBADD = $(opal_argo_LIBS)



