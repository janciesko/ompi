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
 * Copyright (c) 2017-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_BTL_OFI_ENDPOINT_H
#define MCA_BTL_OFI_ENDPOINT_H

#include "opal/class/opal_list.h"
#include "opal/mca/event/event.h"
#include "btl_ofi.h"

BEGIN_C_DECLS

struct mca_btl_base_endpoint_t {
    opal_list_item_t super;
    fi_addr ofi_addr;
    /** endpoint proc */
    opal_proc_t *ep_proc;
    /** mutex to protect this structure */
    opal_mutex_t ep_lock;
};

typedef struct mca_btl_base_endpoint_t mca_btl_base_endpoint_t;
typedef mca_btl_base_endpoint_t mca_btl_ofi_endpoint_t;
OBJ_CLASS_DECLARATION(mca_btl_ofi_endpoint_t);

mca_btl_base_endpoint_t *mca_btl_ofi_endpoint_create (opal_proc_t *proc);
int mca_btl_ofi_endpoint_connect (mca_btl_ofi_module_t *module,
                                  mca_btl_ofi_endpoint_t *endpoint,
                                  int ep_index);

END_C_DECLS
#endif
