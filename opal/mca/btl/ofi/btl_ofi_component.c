/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2014-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */


#include "opal_config.h"

#include "opal/mca/btl/btl.h"
#include "opal/mca/btl/base/base.h"
#include "opal/mca/hwloc/base/base.h"

#include <string.h>

#include "btl_ofi.h"


/*
 * Enumerators
 */

enum {
    BTL_OFI_PROG_AUTO=1,
    BTL_OFI_PROG_MANUAL,
    BTL_OFI_PROG_UNSPEC,
};

mca_base_var_enum_value_t control_prog_type[] = {
    {BTL_OFI_PROG_AUTO, "auto"},
    {BTL_OFI_PROG_MANUAL, "manual"},
    {BTL_OFI_PROG_UNSPEC, "unspec"},
    {0, NULL}
};

mca_base_var_enum_value_t data_prog_type[] = {
    {BTL_OFI_PROG_AUTO, "auto"},
    {BTL_OFI_PROG_MANUAL, "manual"},
    {BTL_OFI_PROG_UNSPEC, "unspec"},
    {0, NULL}
};

enum {
    BTL_OFI_AV_MAP=1,
    BTL_OFI_AV_TABLE,
    BTL_OFI_AV_UNKNOWN,
};

mca_base_var_enum_value_t av_table_type[] = {
    {BTL_OFI_AV_MAP, "map"},
    {BTL_OFI_AV_TABLE, "table"},
    {0, NULL}
};

/** OFI btl component */
mca_btl_uct_component_t mca_btl_ofi_component = {
    .super = {
        .btl_version = {
            MCA_BTL_DEFAULT_VERSION("ofi"),
            .mca_open_component = mca_btl_ofi_component_open,
            .mca_close_component = mca_btl_ofi_component_close,
            .mca_query_component = mca_btl_ofi_component_query,
            .mca_register_component_params = mca_btl_ofi_component_register,
        },
        .btl_data = {
            /* The component is not checkpoint ready */
            .param_field = MCA_BASE_METADATA_PARAM_NONE
        },

        .btl_init = mca_btl_ofi_component_init,
        .btl_progress = mca_btl_ofi_component_progress,
    }
};

static int
mca_btl_ofi_component_register(void)
{
    int ret;
    mca_base_var_enum_t *new_enum = NULL;
    char *desc;

    param_priority = 25;   /* for now give a lower priority than the psm mtl */
    mca_base_component_var_register(&mca_btl_ofi_component.super.mtl_version,
                                    "priority", "Priority of the OFI MTL component",
                                    MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                    OPAL_INFO_LVL_9,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    &param_priority);

    prov_include = "psm,psm2,gni";
    mca_base_component_var_register(&mca_btl_ofi_component.super.mtl_version,
                                    "provider_include",
                                    "Comma-delimited list of OFI providers that are considered for use (e.g., \"psm,psm2\"; an empty value means that all providers will be considered). Mutually exclusive with mtl_ofi_provider_exclude.",
                                    MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                    OPAL_INFO_LVL_1,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    &prov_include);

    prov_exclude = NULL;
    mca_base_component_var_register(&mca_btl_ofi_component.super.mtl_version,
                                    "provider_exclude",
                                    "Comma-delimited list of OFI providers that are not considered for use (default: \"sockets,mxm\"; empty value means that all providers will be considered). Mutually exclusive with mtl_ofi_provider_include.",
                                    MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                    OPAL_INFO_LVL_1,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    &prov_exclude);

    mca_btl_ofi.ofi_progress_event_count = 100;
    asprintf(&desc, "Max number of events to read each call to OFI progress (default: %d events will be read per OFI progress call)", mca_btl_ofi.ofi_progress_event_count);
    mca_base_component_var_register(&mca_btl_ofi_component.super.mtl_version,
                                    "progress_event_cnt",
                                    desc,
                                    MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                    OPAL_INFO_LVL_6,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    &mca_btl_ofi.ofi_progress_event_count);

    free(desc);

    ret = mca_base_var_enum_create ("control_prog_type", control_prog_type, &new_enum);
    if (OPAL_SUCCESS != ret) {
        return ret;
    }

    control_progress = BTL_OFI_PROG_UNSPEC;
    mca_base_component_var_register (&mca_btl_ofi_component.super.mtl_version,
                                     "control_progress",
                                     "Specify control progress model (default: unspecificed, use provider's default). Set to auto or manual for auto or manual progress respectively.",
                                     MCA_BASE_VAR_TYPE_INT, new_enum, 0, 0,
                                     OPAL_INFO_LVL_3,
                                     MCA_BASE_VAR_SCOPE_READONLY,
                                     &control_progress);
    OBJ_RELEASE(new_enum);

    ret = mca_base_var_enum_create ("data_prog_type", data_prog_type, &new_enum);
    if (OPAL_SUCCESS != ret) {
        return ret;
    }

    data_progress = BTL_OFI_PROG_UNSPEC;
    mca_base_component_var_register(&mca_btl_ofi_component.super.mtl_version,
                                    "data_progress",
                                    "Specify data progress model (default: unspecified, use provider's default). Set to auto or manual for auto or manual progress respectively.",
                                    MCA_BASE_VAR_TYPE_INT, new_enum, 0, 0,
                                    OPAL_INFO_LVL_3,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    &data_progress);
    OBJ_RELEASE(new_enum);

    ret = mca_base_var_enum_create ("av_type", av_table_type, &new_enum);
    if (OPAL_SUCCESS != ret) {
        return ret;
    }

    av_type = BTL_OFI_AV_MAP;
    mca_base_component_var_register (&mca_btl_ofi_component.super.mtl_version,
                                     "av",
                                     "Specify AV type to use (default: map). Set to table for FI_AV_TABLE AV type.",
                                     MCA_BASE_VAR_TYPE_INT, new_enum, 0, 0,
                                     OPAL_INFO_LVL_3,
                                     MCA_BASE_VAR_SCOPE_READONLY,
                                     &av_type);
    OBJ_RELEASE(new_enum);

    return OPAL_SUCCESS;
}



static int mca_btl_ofi_component_open(void)
{

    mca_btl_ofi.domain =  NULL;
    mca_btl_ofi.av     =  NULL;
    mca_btl_ofi.cq     =  NULL;
    mca_btl_ofi.ep     =  NULL;

    /**
     * Sanity check: provider_include and provider_exclude must be mutually
     * exclusive
     */
    if (OMPI_SUCCESS !=
        mca_base_var_check_exclusive("ompi",
            mca_btl_ofi_component.super.mtl_version.mca_type_name,
            mca_btl_ofi_component.super.mtl_version.mca_component_name,
            "provider_include",
            mca_btl_ofi_component.super.mtl_version.mca_type_name,
            mca_btl_ofi_component.super.mtl_version.mca_component_name,
            "provider_exclude")) {
        return OPAL_ERR_NOT_AVAILABLE;
    }
    return OPAL_SUCCESS;
}


static int
opal_btl_ofi_component_query(mca_base_module_t **module, int *priority)
{
    *priority = param_priority;
    *module = (mca_base_module_t *)&opal_btl_ofi.base;
    return OMPI_SUCCESS;
}

/*
 * component cleanup - sanity checking of queue lengths
 */
static int mca_btl_ofi_component_close(void)
{
    return OPAL_SUCCESS;
}

static int mca_btl_ofi_modex_send (void)
{
    size_t modex_size = sizeof (mca_btl_ofi_modex_t);
    mca_btl_ofi_modex_t *modex;
    uint8_t *modex_data;
    int rc;

    for (unsigned i = 0 ; i < mca_btl_ofi_component.module_count ; ++i) {
        mca_btl_ofi_module_t *module = mca_btl_ofi_component.modules[i];

        modex_size += (3 + 4 + module->ofi_iface_attr.device_addr_len + module->ofi_iface_attr.iface_addr_len +
                       strlen (module->ofi_tl_full_name) + 1) & ~3;
    }

    modex = alloca (modex_size);
    modex_data = modex->data;

    modex->module_count = mca_btl_ofi_component.module_count;

    for (unsigned i = 0 ; i < mca_btl_ofi_component.module_count ; ++i) {
        mca_btl_ofi_module_t *module = mca_btl_ofi_component.modules[i];
        size_t name_len = strlen (module->ofi_tl_full_name);

        /* pack the size */
        *((uint32_t *) modex_data) = (3 + 4 + module->ofi_iface_attr.device_addr_len + module->ofi_iface_attr.iface_addr_len +
                                     strlen (module->ofi_tl_full_name) + 1) & ~3;
        modex_data += 4;

        strcpy (modex_data, module->ofi_tl_full_name);
        modex_data += name_len + 1;

        /* NTH: only the first context is available. i assume the device addresses of the
         * contexts will be the same but they will have different iface addresses. i also
         * am assuming that it doesn't really matter if all remote contexts connect to
         * the same endpoint since we are only doing RDMA. if any of these assumptions are
         * wrong then we can't delay creating the other contexts and must include their
         * information in the modex. */
        ofi_iface_get_address (module->contexts[0].ofi_iface, (ofi_iface_addr_t *) modex_data);
        modex_data += module->ofi_iface_attr.iface_addr_len;

        ofi_iface_get_device_address (module->contexts[0].ofi_iface, (ofi_device_addr_t *) modex_data);
        modex_data = (uint8_t *) (((uintptr_t)modex_data + module->ofi_iface_attr.device_addr_len + 3) & ~3);
    }

    OPAL_MODEX_SEND(rc, OPAL_PMIX_GLOBAL, &mca_btl_ofi_component.super.btl_version, modex, modex_size);
    return rc;
}

int mca_btl_ofi_context_init (mca_btl_ofi_module_t *module, mca_btl_ofi_device_context_t *context)
{
    ofi_iface_params_t iface_params = {.rndv_cb = NULL, .eager_cb = NULL, .stats_root = NULL,
                                       .rx_headroom = 0, .open_mode = UCT_IFACE_OPEN_MODE_DEVICE,
                                       .mode = {.device = {.tl_name = module->ofi_tl_name,
                                                           .dev_name = module->ofi_dev_name}}};
    ucs_status_t ucs_status;
    int rc = OPAL_SUCCESS;

    mca_btl_ofi_context_lock (context);

    do {
        if (NULL != context->ofi_worker) {
            break;
        }

        /* apparently (in contradiction to the spec) UCT is *not* thread safe. because we have to
         * use our own locks just go ahead and use UCS_THREAD_MODE_SINGLE. if they ever fix their
         * api then change this back to UCS_THREAD_MODE_MULTI and remove the locks around the
         * various UCT calls. */
        ucs_status = ofi_worker_create (module->ucs_async, UCS_THREAD_MODE_SINGLE, &context->ofi_worker);
        if (UCS_OK != ucs_status) {
            BTL_VERBOSE(("Could not create a UCT worker"));
            rc = OPAL_ERROR;
            break;
        }

        ucs_status = ofi_iface_open (module->ofi_md->ofi_md, context->ofi_worker, &iface_params,
                                     module->ofi_tl_config, &context->ofi_iface);
        if (UCS_OK != ucs_status) {
            BTL_VERBOSE(("Could not open UCT interface"));
            rc = OPAL_ERROR;
            break;
        }
    } while (0);

    mca_btl_ofi_context_unlock (context);

    return rc;
}

void mca_btl_ofi_context_fini (mca_btl_ofi_module_t *module, mca_btl_ofi_device_context_t *context)
{
    if (context->ofi_iface) {
        ofi_iface_close (context->ofi_iface);
        context->ofi_iface = NULL;
    }

    if (context->ofi_worker) {
        ofi_worker_destroy (context->ofi_worker);
        context->ofi_worker = NULL;
    }
}

static int mca_btl_ofi_component_process_ofi_tl (const char *md_name, mca_btl_ofi_md_t *md,
                                                 ofi_tl_resource_desc_t *tl_desc,
                                                 char **allowed_ifaces, size_t registration_size)
{
    const uint64_t required_flags = UCT_IFACE_FLAG_PUT_ZCOPY | UCT_IFACE_FLAG_GET_ZCOPY |
        UCT_IFACE_FLAG_ATOMIC_FADD64 | UCT_IFACE_FLAG_ATOMIC_CSWAP64;
    bool found_matching_tl = false;
    mca_btl_ofi_module_t *module;
    ucs_status_t ucs_status;
    char *tl_full_name;
    int rc;

    rc = asprintf (&tl_full_name, "%s-%s", md_name, tl_desc->tl_name);
    if (0 > rc) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    for (int l = 0 ; allowed_ifaces[l] ; ++l) {
        if (0 == strcmp (tl_full_name, allowed_ifaces[l]) || 0 == strcmp (allowed_ifaces[l], "all")) {
            found_matching_tl = true;
            break;
        }
    }

    if (!found_matching_tl) {
        BTL_VERBOSE(("no allowed iface matches %s\n", tl_full_name));
        free (tl_full_name);
        return OPAL_SUCCESS;
    }

    module = malloc (sizeof (*module));
    if (NULL == module) {
        free (tl_full_name);
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    /* copy the module template */
    *module = mca_btl_ofi_module_template;

    OBJ_CONSTRUCT(&module->endpoints, opal_list_t);

    module->ofi_md = md;
    /* keep a reference to the memory domain */
    OBJ_RETAIN(md);

    module->ofi_worker_count = mca_btl_ofi_component.num_contexts_per_module;
    module->ofi_tl_name = strdup (tl_desc->tl_name);
    module->ofi_dev_name = strdup (tl_desc->dev_name);
    module->ofi_tl_full_name = tl_full_name;

    ucs_status = ucs_async_context_create (UCS_ASYNC_MODE_THREAD, &module->ucs_async);
    if (UCS_OK != ucs_status) {
        BTL_VERBOSE(("Could not create a UCT async context"));
        mca_btl_ofi_finalize (&module->super);
        return OPAL_ERROR;
    }

    (void) ofi_md_iface_config_read (md->ofi_md, tl_desc->tl_name, NULL, NULL,
                                     &module->ofi_tl_config);

    for (int context_id = 0 ; context_id < module->ofi_worker_count ; ++context_id) {
        module->contexts[context_id].context_id = context_id;
    }

    /* always initialize the first context */
    rc = mca_btl_ofi_context_init (module, module->contexts);
    if (OPAL_UNLIKELY(OPAL_SUCCESS != rc)) {
        mca_btl_ofi_finalize (&module->super);
        return rc;
    }

    /* only need to query one of the interfaces to get the attributes */
    ucs_status = ofi_iface_query (module->contexts[0].ofi_iface, &module->ofi_iface_attr);
    if (UCS_OK != ucs_status) {
        BTL_VERBOSE(("Error querying UCT interface"));
        mca_btl_ofi_finalize (&module->super);
        return OPAL_ERROR;
    }

    /* UCT bandwidth is in bytes/sec, BTL is in MB/sec */
    module->super.btl_bandwidth = (uint32_t) (module->ofi_iface_attr.bandwidth / 1048576.0);
    /* TODO -- figure out how to translate UCT latency to us */
    module->super.btl_latency = 1;
    module->super.btl_registration_handle_size = registration_size;

    if ((module->ofi_iface_attr.cap.flags & required_flags) != required_flags) {
        BTL_VERBOSE(("Requested UCT transport does not support required features (put, get, amo)"));
        mca_btl_ofi_finalize (&module->super);
        /* not really an error. just an unusable transport */
        return OPAL_SUCCESS;
    }

    module->super.btl_flags = mca_btl_ofi_module_flags (module->ofi_iface_attr.cap.flags);
    module->super.btl_atomic_flags = mca_btl_ofi_module_atomic_flags (module->ofi_iface_attr.cap.flags);
    module->super.btl_get_limit = module->ofi_iface_attr.cap.get.max_zcopy;
    module->super.btl_put_limit = module->ofi_iface_attr.cap.put.max_zcopy;
    module->super.btl_get_local_registration_threshold = 0;
    /* no registration needed when using short put */
    module->super.btl_put_local_registration_threshold = module->ofi_iface_attr.cap.put.max_short;

    mca_btl_ofi_component.modules[mca_btl_ofi_component.module_count++] = module;

    return OPAL_SUCCESS;
}

static int mca_btl_ofi_component_process_ofi_md (ofi_md_resource_desc_t *md_desc, char **allowed_ifaces)
{
    ofi_tl_resource_desc_t *tl_desc;
    ofi_md_config_t *ofi_config;
    ofi_md_attr_t md_attr;
    mca_btl_ofi_md_t *md;
    bool found = false;
    unsigned num_tls;
    int rc;

    for (int j = 0 ; allowed_ifaces[j] ; ++j) {
        if (0 == strncmp (allowed_ifaces[j], md_desc->md_name, strlen (md_desc->md_name)) ||
            0 == strcmp (allowed_ifaces[j], "all")) {
            found = true;
            break;
        }
    }

    if (!found) {
        /* nothing to do */
        return OPAL_SUCCESS;
    }

    md = OBJ_NEW(mca_btl_ofi_md_t);

    ofi_md_config_read (md_desc->md_name, NULL, NULL, &ofi_config);
    ofi_md_open (md_desc->md_name, ofi_config, &md->ofi_md);
    ofi_config_release (ofi_config);

    ofi_md_query (md->ofi_md, &md_attr);
    ofi_md_query_tl_resources (md->ofi_md, &tl_desc, &num_tls);

    for (unsigned j = 0 ; j < num_tls ; ++j) {
        rc = mca_btl_ofi_component_process_ofi_tl (md_desc->md_name, md, tl_desc + j, allowed_ifaces,
                                                   md_attr.rkey_packed_size);
        if (OPAL_SUCCESS != rc) {
            return rc;
        }

        if (MCA_BTL_UCT_MAX_MODULES == mca_btl_ofi_component.module_count) {
            BTL_VERBOSE(("Created the maximum number of allowable modules"));
            break;
        }
    }

    ofi_release_tl_resource_list (tl_desc);

    /* release the initial reference to the md object. if any modules were created the UCT md will remain
     * open until those modules are finalized. */
    OBJ_RELEASE(md);

    return OPAL_SUCCESS;
}

static mca_btl_base_module_t **mca_btl_ofi_component_init (int *num_btl_modules, bool enable_progress_threads,
                                                           bool enable_mpi_threads)
{
    int ret, fi_version;
    struct fi_info *hints;
    struct fi_info *providers = NULL, *prov = NULL;
    struct fi_cq_attr cq_attr = {0};
    struct fi_av_attr av_attr = {0};
    char ep_name[FI_NAME_MAX] = {0};
    size_t namelen;

    BTL_VERBOSE(("initializing ofi btl"));

    /**
     * Hints to filter providers
     * See man fi_getinfo for a list of all filters
     * mode:  Select capabilities MTL is prepared to support.
     *        In this case, MTL will pass in context into communication calls
     * ep_type:  reliable datagram operation
     * caps:     Capabilities required from the provider.
     *           Tag matching is specified to implement MPI semantics.
     * msg_order: Guarantee that messages with same tag are ordered.
     */
    hints = fi_allocinfo();
    if (!hints) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: Could not allocate fi_info\n",
                            __FILE__, __LINE__);
        goto error;
    }
    hints->mode               = FI_CONTEXT;
    hints->ep_attr->type      = FI_EP_RDM;      /* Reliable datagram         */
    hints->caps               = FI_TAGGED;      /* Tag matching interface    */
    hints->tx_attr->msg_order = FI_ORDER_SAS;
    hints->rx_attr->msg_order = FI_ORDER_SAS;

    hints->domain_attr->threading        = FI_THREAD_UNSPEC;

    switch (control_progress) {
    case MTL_OFI_PROG_AUTO:
¬       hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
¬       break;
    case MTL_OFI_PROG_MANUAL:
        hints->domain_attr->control_progress = FI_PROGRESS_MANUAL;
¬       break;
    default:
        hints->domain_attr->control_progress = FI_PROGRESS_UNSPEC;
    }

    switch (data_progress) {
    case MTL_OFI_PROG_AUTO:
¬       hints->domain_attr->data_progress = FI_PROGRESS_AUTO;
¬       break;
    case MTL_OFI_PROG_MANUAL:
        hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
¬       break;
    default:
        hints->domain_attr->data_progress = FI_PROGRESS_UNSPEC;
    }

    if (MTL_OFI_AV_TABLE == av_type) {
        hints->domain_attr->av_type          = FI_AV_TABLE;
    } else {
        hints->domain_attr->av_type          = FI_AV_MAP;
    }

    hints->domain_attr->resource_mgmt    = FI_RM_ENABLED;

   /**
     * FI_VERSION provides binary backward and forward compatibility support
     * Specify the version of OFI is coded to, the provider will select struct
     * layouts that are compatible with this version.
     */
    fi_version = FI_VERSION(1, 0);

    /**
     * fi_getinfo:  returns information about fabric  services for reaching a
     * remote node or service.  this does not necessarily allocate resources.
     * Pass NULL for name/service because we want a list of providers supported.
     */
    ret = fi_getinfo(fi_version,    /* OFI version requested                    */
                     NULL,          /* Optional name or fabric to resolve       */
                     NULL,          /* Optional service name or port to request */
                     0ULL,          /* Optional flag                            */
                     hints,         /* In: Hints to filter providers            */
                     &providers);   /* Out: List of matching providers          */
    if (FI_ENODATA == -ret) {
        // It is not an error if no information is returned.
        goto error;
    } else if (0 != ret) {
        opal_show_help("help-mtl-ofi.txt", "OFI call fail", true,
                       "fi_getinfo",
                       ompi_process_info.nodename, __FILE__, __LINE__,
                       fi_strerror(-ret), -ret);
        goto error;
    }

    /**
     * Select a provider from the list returned by fi_getinfo().
     */
    prov = select_ofi_provider(providers);
    if (!prov) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: select_ofi_provider: no provider found\n",
                            __FILE__, __LINE__);
        goto error;
    }

   /**
     * Open fabric
     * The getinfo struct returns a fabric attribute struct that can be used to
     * instantiate the virtual or physical network. This opens a "fabric
     * provider". See man fi_fabric for details.
     */
    ret = fi_fabric(prov->fabric_attr,    /* In:  Fabric attributes             */
                    &opal_btl_ofi.fabric, /* Out: Fabric handle                 */
                    NULL);                /* Optional context for fabric events */
    if (0 != ret) {
        opal_show_help("help-mtl-ofi.txt", "OFI call fail", true,
                       "fi_fabric",
                       ompi_process_info.nodename, __FILE__, __LINE__,
                       fi_strerror(-ret), -ret);
        goto error;
    }

    /**
     * Create the access domain, which is the physical or virtual network or
     * hardware port/collection of ports.  Returns a domain object that can be
     * used to create endpoints.  See man fi_domain for details.
     */
    ret = fi_domain(opal_btl_ofi.fabric,  /* In:  Fabric object                 */
                    prov,                 /* In:  Provider                      */
                    &opal_btl_ofi.domain, /* Out: Domain oject                  */
                    NULL);                /* Optional context for domain events */
    if (0 != ret) {
        opal_show_help("help-mtl-ofi.txt", "OFI call fail", true,
                       "fi_domain",
                       ompi_process_info.nodename, __FILE__, __LINE__,
                       fi_strerror(-ret), -ret);
        goto error;
    }

    /**
     * Create a transport level communication endpoint.  To use the endpoint,
     * it must be bound to completion counters or event queues and enabled,
     * and the resources consumed by it, such as address vectors, counters,
     * completion queues, etc.
     * see man fi_endpoint for more details.
     */
    ret = fi_endpoint(opal_btl_ofi.domain, /* In:  Domain object   */
                      prov,                /* In:  Provider        */
                      &opal_btl_ofi.ep,    /* Out: Endpoint object */
                      NULL);               /* Optional context     */
    if (0 != ret) {
        opal_show_help("help-mtl-ofi.txt", "OFI call fail", true,
                       "fi_endpoint",
                       ompi_process_info.nodename, __FILE__, __LINE__,
                       fi_strerror(-ret), -ret);
        goto error;
    }

   /**
     * Save the maximum inject size.
     */
    opal_btl_ofi.max_inject_size = prov->tx_attr->inject_size;

    /**
     * Create the objects that will be bound to the endpoint.
     * The objects include:
     *     - completion queue for events
     *     - address vector of other endpoint addresses
     *     - dynamic memory-spanning memory region
     */
    cq_attr.format = FI_CQ_FORMAT_TAGGED;

    /**
     * If a user has set an ofi_progress_event_count > the default, then
     * the CQ size hint is set to the user's desired value such that
     * the CQ created will have enough slots to store up to
     * ofi_progress_event_count events. If a user has not set the
     * ofi_progress_event_count, then the provider is trusted to set a
     * default high CQ size and the CQ size hint is left unspecified.
     */
    if (opal_btl_ofi.ofi_progress_event_count > 100) {
        cq_attr.size = opal_btl_ofi.ofi_progress_event_count;
    }

    ret = fi_cq_open(opal_btl_ofi.domain, &cq_attr, &opal_btl_ofi.cq, NULL);
    if (ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_cq_open failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

    /**
     * Allocate memory for storing the CQ events read in OFI progress.
     */
    opal_btl_ofi.progress_entries = calloc(opal_btl_ofi.ofi_progress_event_count, sizeof(struct fi_cq_tagged_entry));
    if (OPAL_UNLIKELY(!opal_btl_ofi.progress_entries)) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: alloc of CQ event storage failed: %s\n",
                            __FILE__, __LINE__, strerror(errno));
        goto error;
    }

    /**
     * The remote fi_addr will be stored in the ofi_endpoint struct.
     */

    av_attr.type = (MTL_OFI_AV_TABLE == av_type) ? FI_AV_TABLE: FI_AV_MAP;

    ret = fi_av_open(opal_btl_ofi.domain, &av_attr, &opal_btl_ofi.av, NULL);
    if (ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_av_open failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

   /**
     * Bind the CQ and AV to the endpoint object.
     */
    ret = fi_ep_bind(opal_btl_ofi.ep,
                     (fid_t)opal_btl_ofi.cq,
                     FI_SEND | FI_RECV);
    if (0 != ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_bind CQ-EP failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

    ret = fi_ep_bind(opal_btl_ofi.ep,
                     (fid_t)opal_btl_ofi.av,
                     0);
    if (0 != ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_bind AV-EP failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

    /**
     * Enable the endpoint for communication
     * This commits the bind operations.
     */
    ret = fi_enable(opal_btl_ofi.ep);
    if (0 != ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_enable failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

   /**
     * Free providers info since it's not needed anymore.
     */
    fi_freeinfo(hints);
    hints = NULL;
    fi_freeinfo(providers);
    providers = NULL;

    /**
     * Get our address and publish it with modex.
     */
    namelen = sizeof(ep_name);
    ret = fi_getname((fid_t)opal_btl_ofi.ep, &ep_name[0], &namelen);
    if (ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: fi_getname failed: %s\n",
                            __FILE__, __LINE__, fi_strerror(-ret));
        goto error;
    }

    OFI_COMPAT_MODEX_SEND(ret,
                          &mca_btl_ofi_component.super.mtl_version,
                          &ep_name,
                          namelen);
    if (OMPI_SUCCESS != ret) {
        opal_output_verbose(1, opal_btl_base_framework.framework_output,
                            "%s:%d: modex_send failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    opal_btl_ofi.epnamelen = namelen;

    BTL_VERBOSE(("ofi btl initialization complete. found %d suitable transports",
                 mca_btl_ofi_component.module_count));

    *num_btl_modules = 1;
    return (mca_btl_base_module_t **)&opal_btl_ofi.base;


error:
    BTL_VERBOSE(("ofi btl initialization failed");

    if (providers) {
        (void) fi_freeinfo(providers);
    }
    if (hints) {
        (void) fi_freeinfo(hints);
    }
    if (opal_btl_ofi.av) {
        (void) fi_close((fid_t)opal_btl_ofi.av);
    }
    if (opal_btl_ofi.cq) {
        (void) fi_close((fid_t)opal_btl_ofi.cq);
    }
    if (opal_btl_ofi.ep) {
        (void) fi_close((fid_t)opal_btl_ofi.ep);
    }
    if (opal_btl_ofi.domain) {
        (void) fi_close((fid_t)opal_btl_ofi.domain);
    }
    if (opal_btl_ofi.fabric) {
        (void) fi_close((fid_t)opal_btl_ofi.fabric);
    }
    if (opal_btl_ofi.progress_entries) {
        free(opal_btl_ofi.progress_entries);
    }

    return NULL;
}

/**
 * @brief OFI BTL progress function
 *
 * This function explictly progresses all CQs.
 */
static int mca_btl_ofi_component_progress (void)
{
    return _btl_ofi_progress();
}


/** UCT btl component */
mca_btl_ofi_component_t mca_btl_ofi_component = {
    .super = {
        .btl_version = {
            MCA_BTL_DEFAULT_VERSION("ofi"),
            .mca_open_component = mca_btl_ofi_component_open,
            .mca_close_component = mca_btl_ofi_component_close,
            .mca_register_component_params = mca_btl_ofi_component_register,
        },
        .btl_data = {
            /* The component is not checkpoint ready */
            .param_field = MCA_BASE_METADATA_PARAM_NONE
        },

        .btl_init = mca_btl_ofi_component_init,
        .btl_progress = mca_btl_ofi_component_progress,
    }
};
