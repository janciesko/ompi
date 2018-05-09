/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2014-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#if !defined(BTL_OFI_RDMA_H)
#define BTL_OFI_RDMA_H

#include "btl_ofi.h"
#include "btl_ofi_endpoint.h"

/**
 * @brief structure to keep track of btl callback
 *
 * This structuere is passed to various uct functions. It
 * does the translation between the uct callback and the
 * btl callback.
 */
struct mca_btl_ofi_uct_completion_t {
    /** uct completion structure */
    uct_completion_t uct_comp;
    /** btl module associated with the callback */
    struct mca_btl_base_module_t *btl;
    /** btl endpoint associated with the callback */
    struct mca_btl_base_endpoint_t *endpoint;
    /** local address */
    void *local_address;
    /** local registration handle */
    mca_btl_base_registration_handle_t *local_handle;
    /** user callback function */
    mca_btl_base_rdma_completion_fn_t cbfunc;
    /** user callback context */
    void *cbcontext;
    /** user callback data */
    void *cbdata;
};
typedef struct mca_btl_ofi_uct_completion_t mca_btl_ofi_uct_completion_t;

/**
 * @brief allocate a callback structure
 */
mca_btl_ofi_uct_completion_t *mca_btl_ofi_uct_completion_alloc (mca_btl_base_module_t *btl, mca_btl_base_endpoint_t *endpoint,
                                                                void *local_address, mca_btl_base_registration_handle_t *local_handle,
                                                                mca_btl_base_rdma_completion_fn_t cbfunc,
                                                                void *cbcontext, void *cbdata);
/**
 * @brief release a callback structure
 */
void mca_btl_ofi_uct_completion_release (mca_btl_ofi_uct_completion_t *comp);

#endif /* !defined(BTL_OFI_RDMA_H) */
