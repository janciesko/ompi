/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2009 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2015-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/**
 * @file
 */
#ifndef MCA_BTL_OFI_H
#define MCA_BTL_OFI_H

#include "opal_config.h"
#include <sys/types.h>
#include <string.h>

/* Open MPI includes */
#include "opal/mca/event/event.h"
#include "opal/mca/btl/btl.h"
#include "opal/mca/btl/base/base.h"
#include "opal/mca/mpool/mpool.h"
#include "opal/mca/btl/base/btl_base_error.h"
#include "opal/mca/rcache/base/base.h"
#include "opal/mca/pmix/pmix.h"

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_tagged.h>

BEGIN_C_DECLS

/**
 * MTL Module Interface
 */
typedef struct mca_btl_ofi_module_t {
    mca_btl_base_module_t base;

    /** Fabric Domain handle */
    struct fid_fabric *fabric;

    /** Access Domain handle */
    struct fid_domain *domain;

    /** Address vector handle */
    struct fid_av *av;

    /** Completion queue handle */
    struct fid_cq *cq;

    /** Endpoint to communicate on */
    struct fid_ep *ep;

    /** Endpoint name length */
    size_t epnamelen;

    /** Optional user-specified OFI provider name */
    char *provider_name;

    /** Maximum inject size */
    size_t max_inject_size;

    /** Maximum number of CQ events to read in OFI Progress */
    int ofi_progress_event_count;

    /** CQ event storage */
    struct fi_cq_tagged_entry *progress_entries;

    /** memory region associated with this module (there may be multiple modules using the same domain) */
    struct fid_mr *ofi_mr;

    /** registration cache */
    mca_rcache_base_module_t *rcache;

} mca_btl_ofi_module_t;

extern mca_btl_ofi_module_t opal_btl_ofi;

typedef struct mca_btl_ofi_component_t {
    /** Base MTL component */
    mca_btl_base_component_3_0_0_t super;
} mca_btl_ofi_component_t;

extern mca_btl_ofi_component_t opal_btl_ofi_component;

struct mca_btl_ofi_modex_t {
    uint8_t data[];
};
typedef struct mca_btl_ofi_modex_t mca_btl_ofi_modex_t;

struct mca_btl_ofi_md_t {
    opal_object_t super;
    struct fid_mr *ofi_md;
};
typedef struct mca_btl_ofi_md_t mca_btl_ofi_md_t;

OBJ_CLASS_DECLARATION(mca_btl_ofi_md_t);

struct mca_btl_base_registration_handle_t {
    union {
        struct fid_mr *mr;
        uint64_t mr_key;
    }
};

struct mca_btl_ofi_device_context_t {
    volatile int32_t lock;
    struct fid_ep *ep;   /* leave here for now for when we start using SEP in the module */
    mca_btl_ofi_module_t *module;
} mca_btl_ofi_device_context_t;


struct mca_btl_ofi_reg_t {
    mca_rcache_base_registration_t base;

    /** OFI memory descriptor  */
    struct fid_mr *mr;

    /** remote key */
    uint64_t  mr_key;
} mca_btl_ofi_reg_t;

OBJ_CLASS_DECLARATION(mca_btl_ofi_reg_t);

/**
 * Initiate an asynchronous put.
 * Completion Semantics: if this function returns a 1 then the operation
 *                       is complete. a return of OPAL_SUCCESS indicates
 *                       the put operation has been queued with the
 *                       network. the local_handle can not be deregistered
 *                       until all outstanding operations on that handle
 *                       have been completed.
 *
 * @param btl (IN)            BTL module
 * @param endpoint (IN)       BTL addressing information
 * @param local_address (IN)  Local address to put from (registered)
 * @param remote_address (IN) Remote address to put to (registered remotely)
 * @param local_handle (IN)   Registration handle for region containing
 *                            (local_address, local_address + size)
 * @param remote_handle (IN)  Remote registration handle for region containing
 *                            (remote_address, remote_address + size)
 * @param size (IN)           Number of bytes to put
 * @param flags (IN)          Flags for this put operation
 * @param order (IN)          Ordering
 * @param cbfunc (IN)         Function to call on completion (if queued)
 * @param cbcontext (IN)      Context for the callback
 * @param cbdata (IN)         Data for callback
 *
 * @retval OPAL_SUCCESS    The descriptor was successfully queued for a put
 * @retval OPAL_ERROR      The descriptor was NOT successfully queued for a put
 * @retval OPAL_ERR_OUT_OF_RESOURCE  Insufficient resources to queue the put
 *                         operation. Try again later
 * @retval OPAL_ERR_NOT_AVAILABLE  Put can not be performed due to size or
 *                         alignment restrictions.
 */
int mca_btl_ofi_put (struct mca_btl_base_module_t *btl,
    struct mca_btl_base_endpoint_t *endpoint, void *local_address,
    uint64_t remote_address, struct mca_btl_base_registration_handle_t *local_handle,
    struct mca_btl_base_registration_handle_t *remote_handle, size_t size, int flags,
    int order, mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata);

/**
 * Initiate an asynchronous get.
 * Completion Semantics: if this function returns a 1 then the operation
 *                       is complete. a return of OPAL_SUCCESS indicates
 *                       the get operation has been queued with the
 *                       network. the local_handle can not be deregistered
 *                       until all outstanding operations on that handle
 *                       have been completed.
 *
 * @param btl (IN)            BTL module
 * @param endpoint (IN)       BTL addressing information
 * @param local_address (IN)  Local address to put from (registered)
 * @param remote_address (IN) Remote address to put to (registered remotely)
 * @param local_handle (IN)   Registration handle for region containing
 *                            (local_address, local_address + size)
 * @param remote_handle (IN)  Remote registration handle for region containing
 *                            (remote_address, remote_address + size)
 * @param size (IN)           Number of bytes to put
 * @param flags (IN)          Flags for this put operation
 * @param order (IN)          Ordering
 * @param cbfunc (IN)         Function to call on completion (if queued)
 * @param cbcontext (IN)      Context for the callback
 * @param cbdata (IN)         Data for callback
 *
 * @retval OPAL_SUCCESS    The descriptor was successfully queued for a put
 * @retval OPAL_ERROR      The descriptor was NOT successfully queued for a put
 * @retval OPAL_ERR_OUT_OF_RESOURCE  Insufficient resources to queue the put
 *                         operation. Try again later
 * @retval OPAL_ERR_NOT_AVAILABLE  Put can not be performed due to size or
 *                         alignment restrictions.
 */
int mca_btl_ofi_get (struct mca_btl_base_module_t *btl,
    struct mca_btl_base_endpoint_t *endpoint, void *local_address,
    uint64_t remote_address, struct mca_btl_base_registration_handle_t *local_handle,
    struct mca_btl_base_registration_handle_t *remote_handle, size_t size, int flags,
    int order, mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata);

 /**
  * Fault Tolerance Event Notification Function
  * @param state Checkpoint Stae
  * @return OPAL_SUCCESS or failure status
  */
int mca_btl_ofi_ft_event(int state);

int mca_btl_ofi_aop (struct mca_btl_base_module_t *btl, struct mca_btl_base_endpoint_t *endpoint,
                     uint64_t remote_address, mca_btl_base_registration_handle_t *remote_handle,
                     mca_btl_base_atomic_op_t op, uint64_t operand, int flags, int order,
                     mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata);

int mca_btl_ofi_afop (struct mca_btl_base_module_t *btl, struct mca_btl_base_endpoint_t *endpoint,
                      void *local_address, uint64_t remote_address, mca_btl_base_registration_handle_t *local_handle,
                      mca_btl_base_registration_handle_t *remote_handle, mca_btl_base_atomic_op_t op,
                      uint64_t operand, int flags, int order, mca_btl_base_rdma_completion_fn_t cbfunc,
                      void *cbcontext, void *cbdata);

int mca_btl_ofi_acswap (struct mca_btl_base_module_t *btl, struct mca_btl_base_endpoint_t *endpoint,
                        void *local_address, uint64_t remote_address, mca_btl_base_registration_handle_t *local_handle,
                        mca_btl_base_registration_handle_t *remote_handle, uint64_t compare, uint64_t value, int flags,
                        int order, mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata);


int mca_btl_ofi_flush (struct mca_btl_base_module_t *btl, struct mca_btl_base_endpoint_t *endpoint);

/**
 * @brief initialize a ofi btl device context
 *
 * @param[in] module    ofi btl module
 * @param[in] context   ofi device context to initialize
 *
 * @returns OPAL_SUCCESS on success
 * @returns opal error code on failure
 */
int mca_btl_ofi_context_init (mca_btl_ofi_module_t *module, mca_btl_ofi_device_context_t *context);
void mca_btl_ofi_context_fini (mca_btl_ofi_module_t *module, mca_btl_ofi_device_context_t *context);
int mca_btl_ofi_finalize (mca_btl_base_module_t *btl);

__opal_attribute_always_inline__ static inline int
_btl_ofi_progress(void)
{
    ssize_t ret;
    int count = 0, i, events_read;
    struct fi_cq_err_entry error = { 0 };
    opal_btl_ofi_request_t *ofi_req = NULL;

    /**
     * Read the work completions from the CQ.
     * From the completion's op_context, we get the associated OFI request.
     * Call the request's callback.
     */
    while (true) {
        ret = fi_cq_read(opal_btl_ofi.cq, opal_btl_ofi.progress_entries,
                opal_btl_ofi.ofi_progress_event_count);
        if (ret > 0) {
            count+= ret;
            events_read = ret;
            for (i = 0; i < events_read; i++) {
                if (NULL != opal_btl_ofi.progress_entries[i].op_context) {
                    ofi_req = TO_OFI_REQ(opal_btl_ofi.progress_entries[i].op_context);
                    assert(ofi_req);
                    ret = ofi_req->event_callback(&opal_btl_ofi.progress_entries[i], ofi_req);
                    if (OMPI_SUCCESS != ret) {
                        opal_output(0, "%s:%d: Error returned by request event callback: %zd.\n"
                                       "*** The Open MPI OFI MTL is aborting the MPI job (via exit(3)).\n",
                                       __FILE__, __LINE__, ret);
                        fflush(stderr);
                        exit(1);
                    }
                }
            }
        } else if (OPAL_UNLIKELY(ret == -FI_EAVAIL)) {
            /**
             * An error occured and is being reported via the CQ.
             * Read the error and forward it to the upper layer.
             */
            ret = fi_cq_readerr(opal_btl_ofi.cq,
                                &error,
                                0);
            if (0 > ret) {
                opal_output(0, "%s:%d: Error returned from fi_cq_readerr: %s(%zd).\n"
                               "*** The Open MPI OFI MTL is aborting the MPI job (via exit(3)).\n",
                               __FILE__, __LINE__, fi_strerror(-ret), ret);
                fflush(stderr);
                exit(1);
            }

            assert(error.op_context);
            ofi_req = TO_OFI_REQ(error.op_context);
            assert(ofi_req);
            ret = ofi_req->error_callback(&error, ofi_req);
            if (OMPI_SUCCESS != ret) {
                    opal_output(0, "%s:%d: Error returned by request error callback: %zd.\n"
                                   "*** The Open MPI OFI MTL is aborting the MPI job (via exit(3)).\n",
                                   __FILE__, __LINE__, ret);
                fflush(stderr);
                exit(1);
            }
        } else {
            if (ret == -FI_EAGAIN || ret == -EINTR) {
                break;
            } else {
                opal_output(0, "%s:%d: Error returned from fi_cq_read: %s(%zd).\n"
                               "*** The Open MPI OFI MTL is aborting the MPI job (via exit(3)).\n",
                               __FILE__, __LINE__, fi_strerror(-ret), ret);
                fflush(stderr);
                exit(1);
            }
        }
    }
    return count;
}

#endif  /* MCA_BTL_OFI_H */
END_C_DECLS
