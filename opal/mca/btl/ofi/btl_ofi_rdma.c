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

#include "btl_uct_rdma.h"

static mca_btl_ofi_completion_t *comp_completion_alloc (mca_btl_base_rdma_completion_fn_t cbfunc,
                                                        void *cbcontext,
                                                        void *cbdata)
{
    mca_btl_ofi_completion_t *comp = malloc (sizeof (*comp));

    comp->cbfunc = cbfunc;
    comp->cbcontext = context;
    comp->cbdata = cbdata;

    return comp;
}

void mca_btl_ofi_completion_release (mca_btl_ofi_completion_t *comp)
{
    free (comp);
}

int mca_btl_ofi_get (mca_btl_base_module_t *btl, mca_btl_base_endpoint_t *endpoint, void *local_address,
                      uint64_t remote_address, mca_btl_base_registration_handle_t *local_handle,
                      mca_btl_base_registration_handle_t *remote_handle, size_t size, int flags,
                      int order, mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata)
{
    mca_btl_ofi_module_t *ofi_module = (mca_btl_ofi_module_t *) btl;
    mca_btl_ofi_completion_t *comp = NULL;
    int rc;

    if (cbfunc) {
        comp = mca_btl_ofi_completion_alloc (cbfunc, context, cbdata);
        if (OPAL_UNLIKELY(NULL == comp)) {
            return OPAL_ERR_OUT_OF_RESOURCE;
        }
    }

    rc = fi_read(ofi_module->ep,
                 local_address,
                 size,
                 (void *)local_handle->fid_mr,
                 endpoint->ofi_addr,
                 remote_address,
                 remote_handle->rkey,
                 (void *)comp);

    if (OPAL_LIKELY(FI_SUCCESS == rc) {
        /* TODO - need to up outstanding read count here */
        return OPAL_SUCCESS;
    }

    /*
     * free the completion since something messed up with the ofi read
     * and we're not going to get a CQ entry back
     */

    if ( NULL != comp) {
        mca_btl_ofi_completion_release (mca_btl_ofi_completion_t *comp);
    }

    if (-FI_EAGAIN == rc) {
        return OPAL_ERR_RESOURCE_BUSY;
    }

    return OPAL_ERROR;
}

int mca_btl_ofi_put (mca_btl_base_module_t *btl, mca_btl_base_endpoint_t *endpoint, void *local_address,
                      uint64_t remote_address, mca_btl_base_registration_handle_t *local_handle,
                      mca_btl_base_registration_handle_t *remote_handle, size_t size, int flags,
                      int order, mca_btl_base_rdma_completion_fn_t cbfunc, void *cbcontext, void *cbdata)
{
    mca_btl_ofi_module_t *ofi_module = (mca_btl_ofi_module_t *) btl;
    mca_btl_ofi_completion_t *comp;
    int rc;

/*
    if (size >  uct_module->super.btl_put_local_registration_threshold && cbfunc) {
        comp = mca_btl_uct_uct_completion_alloc (btl, endpoint, local_address, local_handle, cbfunc, cbcontext, cbdata);
        if (OPAL_UNLIKELY(NULL == comp)) {
            return OPAL_ERR_OUT_OF_RESOURCE;
        }
    }
*/

    if (cbfunc) {
        comp = mca_btl_ofi_completion_alloc (cbfunc, context, cbdata);
        if (OPAL_UNLIKELY(NULL == comp)) {
            return OPAL_ERR_OUT_OF_RESOURCE;
        }
    }

    rc =  fi_write(ofi_module->ep, 
                   local_address,
                   size,
                   (void *)local_handle->fid_mr,
                   endpoint->ofi_addr,
                   remote_address,
                   remote_handle->rkey,
                   (void *)comp);

    if (OPAL_LIKELY(FI_SUCCESS == rc) {
        /* TODO:  need to update atomically  here */
        ++ofi_module->outstanding_write;
        return OPAL_SUCCESS;
    }

    /*
     * free the completion since something messed up with the ofi read
     * and we're not going to get a CQ entry back
     */

    if ( NULL != comp) {
        mca_btl_ofi_completion_release (mca_btl_ofi_completion_t *comp);
    }

    if (-FI_EAGAIN == rc) {
        return OPAL_ERR_RESOURCE_BUSY;
    }

    return OPAL_ERROR;
}

int mca_btl_ofi_flush (mca_btl_base_module_t *btl, mca_btl_base_endpoint_t *endpoint)
{
    mca_btl_uct_module_t *ofi_module = (mca_btl_uct_module_t *) btl;

    /* flush all device contexts */
    for (int i = 0 ; i < uct_module->uct_worker_count ; ++i) {
        mca_btl_uct_device_context_t *context = uct_module->contexts + i;

        if (NULL == context->uct_worker) {
            continue;
        }

        mca_btl_uct_context_lock (context);
        /* this loop is here because at least some of the TLs do no support a
         * completion callback. its a real PIA but has to be done for now. */
        do {
            uct_worker_progress (context->uct_worker);

            if (NULL != endpoint && endpoint->uct_eps[i]) {
                ucs_status = uct_ep_flush (endpoint->uct_eps[i], 0, NULL);
            } else {
                ucs_status = uct_iface_flush (context->uct_iface, 0, NULL);
            }
        } while (UCS_INPROGRESS == ucs_status);

        mca_btl_uct_context_unlock (context);
    }

    return UCS_OK == ucs_status ? OPAL_SUCCESS : OPAL_ERR_RESOURCE_BUSY;
}
