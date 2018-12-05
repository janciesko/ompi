/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2018      Triad National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include "instance.h"

#include "opal/util/arch.h"

#include "opal/util/show_help.h"
#include "opal/util/argv.h"
#include "opal/runtime/opal_params.h"

#include "ompi/mca/pml/pml.h"
#include "ompi/runtime/params.h"

#include "ompi/interlib/interlib.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/errhandler/errcode.h"
#include "ompi/message/message.h"
#include "ompi/info/info.h"
#include "ompi/attribute/attribute.h"
#include "ompi/op/op.h"
#include "ompi/dpm/dpm.h"
#include "ompi/file/file.h"
#include "ompi/mpiext/mpiext.h"

#include "ompi/mca/hook/base/base.h"
#include "ompi/mca/rte/base/base.h"
#include "ompi/mca/op/base/base.h"
#include "opal/mca/allocator/base/base.h"
#include "opal/mca/rcache/base/base.h"
#include "opal/mca/mpool/base/base.h"
#include "ompi/mca/bml/base/base.h"
#include "ompi/mca/pml/base/base.h"
#include "ompi/mca/coll/base/base.h"
#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/io/base/base.h"
#include "ompi/mca/topo/base/base.h"
#include "opal/mca/pmix/base/base.h"

#include "opal/mca/mpool/base/mpool_base_tree.h"
#include "ompi/mca/pml/base/pml_base_bsend.h"

ompi_predefined_instance_t ompi_mpi_instance_null = {{{{0}}}};

static opal_recursive_mutex_t instance_lock = OPAL_RECURSIVE_MUTEX_STATIC_INIT;

/** MPI_Init instance */
ompi_instance_t *ompi_mpi_instance_default = NULL;

enum {
    OMPI_INSTANCE_INITIALIZING = -1,
    OMPI_INSTANCE_FINALIZING   = -2,
};

opal_atomic_int32_t ompi_instance_count = 0;

static const char *ompi_instance_builtin_psets[] = {
    "mpi://world",
    "mpi://self",
    "mpi://shared",
};

static const int32_t ompi_instance_builtin_count = 3;

/** finalization functions that need to be called on teardown */
static opal_list_t ompi_instance_finalize_fns;
static opal_list_t ompi_instance_finalize_fns_basic;

struct ompi_instance_finalize_fn_item_t {
    opal_list_item_t super;
    ompi_instance_finalize_fn_t finalize_fn;
};

typedef struct ompi_instance_finalize_fn_item_t ompi_instance_finalize_fn_item_t;
OBJ_CLASS_DECLARATION(ompi_instance_finalize_fn_item_t);
OBJ_CLASS_INSTANCE(ompi_instance_finalize_fn_item_t, opal_list_item_t, NULL, NULL);

static void ompi_instance_construct (ompi_instance_t *instance)
{
    instance->i_f_to_c_index = opal_pointer_array_add (&ompi_instance_f_to_c_table, instance);
    instance->i_name[0] = '\0';
    instance->i_flags = 0;
    instance->i_keyhash = NULL;
    instance->errhandler_type = OMPI_ERRHANDLER_TYPE_INSTANCE;
}

OBJ_CLASS_INSTANCE(ompi_instance_t, opal_infosubscriber_t, ompi_instance_construct, NULL);

/* NTH: frameworks needed by MPI */
static mca_base_framework_t *ompi_framework_dependencies[] = {
    &ompi_hook_base_framework, &ompi_rte_base_framework, &ompi_op_base_framework,
    &opal_allocator_base_framework, &opal_rcache_base_framework, &opal_mpool_base_framework,
    &ompi_bml_base_framework, &ompi_pml_base_framework, &ompi_coll_base_framework,
    &ompi_osc_base_framework, NULL,
};

static mca_base_framework_t *ompi_lazy_frameworks[] = {
    &ompi_io_base_framework, &ompi_topo_base_framework, NULL,
};


/*
 * Hash tables for MPI_Type_create_f90* functions
 */
opal_hash_table_t ompi_mpi_f90_integer_hashtable = {{0}};
opal_hash_table_t ompi_mpi_f90_real_hashtable = {{0}};
opal_hash_table_t ompi_mpi_f90_complex_hashtable = {{0}};

static size_t  ompi_mpi_instance_num_pmix_psets;
static char  **ompi_mpi_instance_pmix_psets;
/*
 * Per MPI-2:9.5.3, MPI_REGISTER_DATAREP is a memory leak.  There is
 * no way to *de*register datareps once they've been registered.  So
 * we have to track all registrations here so that they can be
 * de-registered during MPI_FINALIZE so that memory-tracking debuggers
 * don't show Open MPI as leaking memory.
 */
opal_list_t ompi_registered_datareps = {{0}};

opal_pointer_array_t ompi_instance_f_to_c_table = {{0}};

static int ompi_instance_print_error (const char *error, int ret)
{
    /* Only print a message if one was not already printed */
    if (NULL != error && OMPI_ERR_SILENT != ret) {
        const char *err_msg = opal_strerror(ret);
        opal_show_help("help-mpi-runtime.txt",
                       "mpi_init:startup:internal-failure", true,
                       "MPI_INIT", "MPI_INIT", error, err_msg, ret);
    }

    return ret;
}

static int ompi_mpi_instance_cleanup_pml (void)
{
    /* call del_procs on all allocated procs even though some may not be known
     * to the pml layer. the pml layer is expected to be resilient and ignore
     * any unknown procs. */
    size_t nprocs = 0;
    ompi_proc_t **procs;

    procs = ompi_proc_get_allocated (&nprocs);
    MCA_PML_CALL(del_procs(procs, nprocs));
    free(procs);

    return OMPI_SUCCESS;
}

/**
 * Static functions used to configure the interactions between the OPAL and
 * the runtime.
 */
static char *_process_name_print_for_opal (const opal_process_name_t procname)
{
    ompi_process_name_t *rte_name = (ompi_process_name_t*)&procname;
    return OMPI_NAME_PRINT(rte_name);
}

static int _process_name_compare (const opal_process_name_t p1, const opal_process_name_t p2)
{
    ompi_process_name_t *o1 = (ompi_process_name_t *) &p1;
    ompi_process_name_t *o2 = (ompi_process_name_t *) &p2;
    return ompi_rte_compare_name_fields(OMPI_RTE_CMP_ALL, o1, o2);
}

static int _convert_string_to_process_name (opal_process_name_t *name, const char* name_string)
{
    return ompi_rte_convert_string_to_process_name(name, name_string);
}

static int _convert_process_name_to_string (char **name_string, const opal_process_name_t *name)
{
    return ompi_rte_convert_process_name_to_string(name_string, name);
}

static int32_t ompi_mpi_instance_init_basic_count;
static bool ompi_instance_basic_init;

void ompi_mpi_instance_release (void)
{
    ompi_instance_finalize_fn_item_t *finalize_item;
    
    opal_mutex_lock (&instance_lock);

    opal_argv_free (ompi_mpi_instance_pmix_psets);
    ompi_mpi_instance_pmix_psets = NULL;

    if (0 != --ompi_mpi_instance_init_basic_count) {
        opal_mutex_unlock (&instance_lock);
        return;
    }

    OPAL_LIST_FOREACH_REV(finalize_item, &ompi_instance_finalize_fns_basic, ompi_instance_finalize_fn_item_t) {
        (void) finalize_item->finalize_fn ();
    }

    OPAL_LIST_DESTRUCT(&ompi_instance_finalize_fns_basic);

    opal_finalize_util ();

    opal_mutex_unlock (&instance_lock);
}

int ompi_mpi_instance_retain (void)
{
    int ret;

    opal_mutex_lock (&instance_lock);

    if (0 < ompi_mpi_instance_init_basic_count++) {
        opal_mutex_unlock (&instance_lock);
        return OMPI_SUCCESS;
    }

    /* Setup enough to check get/set MCA params */
    if (OPAL_SUCCESS != (ret = opal_init_util (NULL, NULL))) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_mpi_instance_init: opal_init_util failed", ret);
    }

    ompi_instance_basic_init = true;

    OBJ_CONSTRUCT(&ompi_instance_finalize_fns_basic, opal_list_t);

    /* Setup f to c table */
    OBJ_CONSTRUCT(&ompi_instance_f_to_c_table, opal_pointer_array_t);
    if (OPAL_SUCCESS != opal_pointer_array_init (&ompi_instance_f_to_c_table, 8,
                                                 OMPI_FORTRAN_HANDLE_MAX, 32)) {
        return OMPI_ERROR;
    }

    /* setup the default error handler on instance_null */
    OBJ_CONSTRUCT(&ompi_mpi_instance_null, ompi_instance_t);
    ompi_mpi_instance_null.instance.error_handler = &ompi_mpi_errors_return.eh;

    /* Convince OPAL to use our naming scheme */
    opal_process_name_print = _process_name_print_for_opal;
    opal_compare_proc = _process_name_compare;
    opal_convert_string_to_process_name = _convert_string_to_process_name;
    opal_convert_process_name_to_string = _convert_process_name_to_string;
    opal_proc_for_name = ompi_proc_for_name;

    /* Register MCA variables */
    if (OPAL_SUCCESS != (ret = ompi_mpi_register_params ())) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_mpi_init: ompi_register_mca_variables failed", ret);
    }

    /* initialize error handlers */
    if (OMPI_SUCCESS != (ret = ompi_errhandler_init ())) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_errhandler_init() failed", ret);
    }

    /* initialize error codes */
    if (OMPI_SUCCESS != (ret = ompi_mpi_errcode_init ())) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_mpi_errcode_init() failed", ret);
    }

    /* initialize internal error codes */
    if (OMPI_SUCCESS != (ret = ompi_errcode_intern_init ())) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_errcode_intern_init() failed", ret);
    }

    /* initialize info */
    if (OMPI_SUCCESS != (ret = ompi_mpiinfo_init ())) {
        return ompi_instance_print_error ("ompi_info_init() failed", ret);
    }

    ompi_instance_basic_init = false;

    opal_mutex_unlock (&instance_lock);

    return OMPI_SUCCESS;
}

/**
 * @brief Function that starts up the common components needed by all instances
 */
static int ompi_mpi_instance_init_common (void)
{
    ompi_errhandler_errtrk_t errtrk;
    ompi_proc_t **procs;
    size_t nprocs;
    opal_list_t info;
    opal_value_t *kv;
    int ret;

    ret = ompi_mpi_instance_retain ();
    if (OPAL_UNLIKELY(OMPI_SUCCESS != ret)) {
        return ret;
    }

    OBJ_CONSTRUCT(&ompi_instance_finalize_fns, opal_list_t);

    if (OPAL_SUCCESS != (ret = opal_arch_set_fortran_logical_size(sizeof(ompi_fortran_logical_t)))) {
        return ompi_instance_print_error ("ompi_mpi_init: opal_arch_set_fortran_logical_size failed", ret);
    }

    /* _After_ opal_init_util() but _before_ orte_init(), we need to
       set an MCA param that tells libevent that it's ok to use any
       mechanism in libevent that is available on this platform (e.g.,
       epoll and friends).  Per opal/event/event.s, we default to
       select/poll -- but we know that MPI processes won't be using
       pty's with the event engine, so it's ok to relax this
       constraint and let any fd-monitoring mechanism be used. */

    ret = mca_base_var_find("opal", "event", "*", "event_include");
    if (ret >= 0) {
        char *allvalue = "all";
        /* We have to explicitly "set" the MCA param value here
           because libevent initialization will re-register the MCA
           param and therefore override the default. Setting the value
           here puts the desired value ("all") in different storage
           that is not overwritten if/when the MCA param is
           re-registered. This is unless the user has specified a different
           value for this MCA parameter. Make sure we check to see if the
           default is specified before forcing "all" in case that is not what
           the user desires. Note that we do *NOT* set this value as an
           environment variable, just so that it won't be inherited by
           any spawned processes and potentially cause unintented
           side-effects with launching RTE tools... */
        mca_base_var_set_value(ret, allvalue, 4, MCA_BASE_VAR_SOURCE_DEFAULT, NULL);
    }

    /* open the ompi hook framework */
    for (int i = 0 ; ompi_framework_dependencies[i] ; ++i) {
        ret = mca_base_framework_open (ompi_framework_dependencies[i], 0);
        if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
            char error_msg[256];
            snprintf (error_msg, sizeof(error_msg), "mca_base_framework_open on %s_%s failed",
                      ompi_framework_dependencies[i]->framework_project,
                      ompi_framework_dependencies[i]->framework_name);
            return ompi_instance_print_error (error_msg, ret);
        }
    }

    /* Setup RTE */
    if (OMPI_SUCCESS != (ret = ompi_rte_init (NULL, NULL))) {
        return ompi_instance_print_error ("ompi_mpi_init: ompi_rte_init failed", ret);
    }

    ompi_rte_initialized = true;

    /* Register the default errhandler callback  */
    errtrk.status = OPAL_ERROR;
    errtrk.active = true;
    /* we want to go first */
    OBJ_CONSTRUCT(&info, opal_list_t);
    kv = OBJ_NEW(opal_value_t);
    kv->key = strdup(OPAL_PMIX_EVENT_HDLR_PREPEND);
    opal_list_append(&info, &kv->super);
    /* give it a name so we can distinguish it */
    kv = OBJ_NEW(opal_value_t);
    kv->key = strdup(OPAL_PMIX_EVENT_HDLR_NAME);
    kv->type = OPAL_STRING;
    kv->data.string = strdup("MPI-Default");
    opal_list_append(&info, &kv->super);
    opal_pmix.register_evhandler(NULL, &info, ompi_errhandler_callback,
                                 ompi_errhandler_registration_callback,
                                 (void*)&errtrk);
    OMPI_LAZY_WAIT_FOR_COMPLETION(errtrk.active);

    OPAL_LIST_DESTRUCT(&info);
    if (OPAL_SUCCESS != errtrk.status) {
        return ompi_instance_print_error ("Error handler registration", errtrk.status);
    }

    /* declare our presence for interlib coordination, and
     * register for callbacks when other libs declare. XXXXXX -- TODO -- figure out how
     * to specify the thread level when different instances may request different levels. */
    if (OMPI_SUCCESS != (ret = ompi_interlib_declare(MPI_THREAD_MULTIPLE, OMPI_IDENT_STRING))) {
        return ompi_instance_print_error ("ompi_interlib_declare", ret);
    }

    /* initialize datatypes. This step should be done early as it will
     * create the local convertor and local arch used in the proc
     * init.
     */
    if (OMPI_SUCCESS != (ret = ompi_datatype_init())) {
        return ompi_instance_print_error ("ompi_datatype_init() failed", ret);
    }

    /* Initialize OMPI procs */
    if (OMPI_SUCCESS != (ret = ompi_proc_init())) {
        return ompi_instance_print_error ("mca_proc_init() failed", ret);
    }

    /* Initialize the op framework. This has to be done *after*
       ddt_init, but befor mca_coll_base_open, since some collective
       modules (e.g., the hierarchical coll component) may need ops in
       their query function. */
    if (OMPI_SUCCESS != (ret = ompi_op_base_find_available (OPAL_ENABLE_PROGRESS_THREADS, ompi_mpi_thread_multiple))) {
        return ompi_instance_print_error ("ompi_op_base_find_available() failed", ret);
    }

    if (OMPI_SUCCESS != (ret = ompi_op_init())) {
        return ompi_instance_print_error ("ompi_op_init() failed", ret);
    }

    /* In order to reduce the common case for MPI apps (where they
       don't use MPI-2 IO or MPI-1/3 topology functions), the io and
       topo frameworks are initialized lazily, at the first use of
       relevant functions (e.g., MPI_FILE_*, MPI_CART_*, MPI_GRAPH_*),
       so they are not opened here. */

    /* Select which MPI components to use */

    if (OMPI_SUCCESS != (ret = mca_pml_base_select (OPAL_ENABLE_PROGRESS_THREADS, ompi_mpi_thread_multiple))) {
        return ompi_instance_print_error ("mca_pml_base_select() failed", ret);
    }

    /* exchange connection info - this function may also act as a barrier
     * if data exchange is required. The modex occurs solely across procs
     * in our job. If a barrier is required, the "modex" function will
     * perform it internally */
    opal_pmix.commit ();

    /* select buffered send allocator component to be used */
    if (OMPI_SUCCESS != (ret = mca_pml_base_bsend_init ())) {
        return ompi_instance_print_error ("mca_pml_base_bsend_init() failed", ret);
    }

    if (OMPI_SUCCESS != (ret = mca_coll_base_find_available (OPAL_ENABLE_PROGRESS_THREADS, ompi_mpi_thread_multiple))) {
        return ompi_instance_print_error ("mca_coll_base_find_available() failed", ret);
    }

    if (OMPI_SUCCESS != (ret = ompi_osc_base_find_available (OPAL_ENABLE_PROGRESS_THREADS, ompi_mpi_thread_multiple))) {
        return ompi_instance_print_error ("ompi_osc_base_find_available() failed", ret);
    }

    /* io and topo components are not selected here -- see comment
       above about the io and topo frameworks being loaded lazily */

    /* Initialize each MPI handle subsystem */
    /* initialize requests */
    if (OMPI_SUCCESS != (ret = ompi_request_init ())) {
        return ompi_instance_print_error ("ompi_request_init() failed", ret);
    }

    if (OMPI_SUCCESS != (ret = ompi_message_init ())) {
        return ompi_instance_print_error ("ompi_message_init() failed", ret);
    }

    /* initialize groups  */
    if (OMPI_SUCCESS != (ret = ompi_group_init ())) {
        return ompi_instance_print_error ("ompi_group_init() failed", ret);
    }

    /* initialize communicator subsystem */
    if (OMPI_SUCCESS != (ret = ompi_comm_init ())) {
        opal_mutex_unlock (&instance_lock);
        return ompi_instance_print_error ("ompi_comm_init() failed", ret);
    }

    if (mca_pml_base_requires_world ()) {
        /* need to set up comm world for this instance -- XXX -- FIXME -- probably won't always
         * be the case. */
        if (OMPI_SUCCESS != (ret = ompi_comm_init_mpi3 ())) {
            return ompi_instance_print_error ("ompi_comm_init_mpi3 () failed", ret);
        }
    }

    /* initialize file handles */
    if (OMPI_SUCCESS != (ret = ompi_file_init ())) {
        return ompi_instance_print_error ("ompi_file_init() failed", ret);
    }

    /* initialize windows */
    if (OMPI_SUCCESS != (ret = ompi_win_init ())) {
        return ompi_instance_print_error ("ompi_win_init() failed", ret);
    }

    /* initialize attribute meta-data structure for comm/win/dtype */
    if (OMPI_SUCCESS != (ret = ompi_attr_init ())) {
        return ompi_instance_print_error ("ompi_attr_init() failed", ret);
    }

    /* Setup the dynamic process management (DPM) subsystem */
    if (OMPI_SUCCESS != (ret = ompi_dpm_init ())) {
        return ompi_instance_print_error ("ompi_dpm_init() failed", ret);
    }


    /* identify the architectures of remote procs and setup
     * their datatype convertors, if required
     */
    if (OMPI_SUCCESS != (ret = ompi_proc_complete_init())) {
        return ompi_instance_print_error ("ompi_proc_complete_init failed", ret);
    }

    /* start PML/BTL's */
    ret = MCA_PML_CALL(enable(true));
    if( OMPI_SUCCESS != ret ) {
        return ompi_instance_print_error ("PML control failed", ret);
    }

    /* some btls/mtls require we call add_procs with all procs in the job.
     * since the btls/mtls have no visibility here it is up to the pml to
     * convey this requirement */
    if (mca_pml_base_requires_world ()) {
        if (NULL == (procs = ompi_proc_world (&nprocs))) {
            return ompi_instance_print_error ("ompi_proc_get_allocated () failed", ret);
        }
    } else {
        /* add all allocated ompi_proc_t's to PML (below the add_procs limit this
         * behaves identically to ompi_proc_world ()) */
        if (NULL == (procs = ompi_proc_get_allocated (&nprocs))) {
            return ompi_instance_print_error ("ompi_proc_get_allocated () failed", ret);
        }
    }

    ompi_mpi_instance_append_finalize (ompi_mpi_instance_cleanup_pml);

    ret = MCA_PML_CALL(add_procs(procs, nprocs));
    free(procs);
    /* If we got "unreachable", then print a specific error message.
       Otherwise, if we got some other failure, fall through to print
       a generic message. */
    if (OMPI_ERR_UNREACH == ret) {
        opal_show_help("help-mpi-runtime.txt",
                       "mpi_init:startup:pml-add-procs-fail", true);
        return ret;
    } else if (OMPI_SUCCESS != ret) {
        return ompi_instance_print_error ("PML add procs failed", ret);
    }

    /* Determine the overall threadlevel support of all processes
       in MPI_COMM_WORLD. This has to be done before calling
       coll_base_comm_select, since some of the collective components
       e.g. hierarch, might create subcommunicators. The threadlevel
       requested by all processes is required in order to know
       which cid allocation algorithm can be used. */
    if (OMPI_SUCCESS != ( ret = ompi_comm_cid_init ())) {
        return ompi_instance_print_error ("ompi_mpi_init: ompi_comm_cid_init failed", ret);
    }

    /* Check whether we have been spawned or not.  We introduce that
       at the very end, since we need collectives, datatypes, ptls
       etc. up and running here.... */
    if (OMPI_SUCCESS != (ret = ompi_dpm_dyn_init())) {
        return ompi_instance_print_error ("ompi_dpm_dyn_init() failed", ret);
    }

    /* Undo OPAL calling opal_progress_event_users_increment() during
       opal_init, to get better latency when not using TCP.  Do
       this *after* dyn_init, as dyn init uses lots of RTE
       communication and we don't want to hinder the performance of
       that code. */
    opal_progress_event_users_decrement();

    /* see if yield_when_idle was specified - if so, use it */
    opal_progress_set_yield_when_idle (ompi_mpi_yield_when_idle);

    /* negative value means use default - just don't do anything */
    if (ompi_mpi_event_tick_rate >= 0) {
        opal_progress_set_event_poll_rate(ompi_mpi_event_tick_rate);
    }

    /* At this point, we are fully configured and in MPI mode.  Any
       communication calls here will work exactly like they would in
       the user's code.  Setup the connections between procs and warm
       them up with simple sends, if requested */

    if (OMPI_SUCCESS != (ret = ompi_mpiext_init())) {
        return ompi_instance_print_error ("ompi_mpiext_init", ret);
    }

    /* Initialize the registered datarep list to be empty */
    OBJ_CONSTRUCT(&ompi_registered_datareps, opal_list_t);

    /* Initialize the arrays used to store the F90 types returned by the
     *  MPI_Type_create_f90_XXX functions.
     */
    OBJ_CONSTRUCT( &ompi_mpi_f90_integer_hashtable, opal_hash_table_t);
    opal_hash_table_init(&ompi_mpi_f90_integer_hashtable, 16 /* why not? */);

    OBJ_CONSTRUCT( &ompi_mpi_f90_real_hashtable, opal_hash_table_t);
    opal_hash_table_init(&ompi_mpi_f90_real_hashtable, FLT_MAX_10_EXP);

    OBJ_CONSTRUCT( &ompi_mpi_f90_complex_hashtable, opal_hash_table_t);
    opal_hash_table_init(&ompi_mpi_f90_complex_hashtable, FLT_MAX_10_EXP);

    return OMPI_SUCCESS;
}

int ompi_mpi_instance_init (MPI_Flags *flags, opal_info_t *info, ompi_errhandler_t *errhandler, ompi_instance_t **instance)
{
    ompi_instance_t *new_instance = OBJ_NEW(ompi_instance_t);
    int ret;

    *instance = &ompi_mpi_instance_null.instance;

    if (OPAL_UNLIKELY(NULL == new_instance)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    /* If thread support was enabled, then setup OPAL to allow for them by deault. This must be done
     * early to prevent a race condition that can occur with orte_init(). */
    if (*flags & MPI_FLAG_THREAD_CONCURRENT) {
        opal_set_using_threads(true);
    }

    opal_mutex_lock (&instance_lock);
    if (0 == opal_atomic_fetch_add_32 (&ompi_instance_count, 1)) {
        ret = ompi_mpi_instance_init_common ();
        if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
            opal_mutex_unlock (&instance_lock);
            OBJ_RELEASE(new_instance);
            return ret;
        }
    }
    opal_mutex_unlock (&instance_lock);

    new_instance->error_handler = errhandler;
    OBJ_RETAIN(new_instance->error_handler);

    /* Copy info if there is one. */
    if (OPAL_UNLIKELY(NULL != info)) {
        new_instance->super.s_info = OBJ_NEW(opal_info_t);
        if (info) {
            opal_info_dup(info, &new_instance->super.s_info);
        }
    }



    *instance = new_instance;

    return OMPI_SUCCESS;
}

static int ompi_mpi_instance_finalize_common (void)
{
    ompi_instance_finalize_fn_item_t *finalize_item;
    uint32_t key;
    ompi_datatype_t *datatype;
    int ret;

    /* As finalize is the last legal MPI call, we are allowed to force the release
     * of the user buffer used for bsend, before going anywhere further.
     */
    (void) mca_pml_base_bsend_detach (NULL, NULL);

    /* Shut down any bindings-specific issues: C++, F77, F90 */

    /* Remove all memory associated by MPI_REGISTER_DATAREP (per
       MPI-2:9.5.3, there is no way for an MPI application to
       *un*register datareps, but we don't want the OMPI layer causing
       memory leaks). */
    OPAL_LIST_DESTRUCT(&ompi_registered_datareps);

    /* Remove all F90 types from the hash tables */
    OPAL_HASH_TABLE_FOREACH(key, uint32, datatype, &ompi_mpi_f90_integer_hashtable)
        OBJ_RELEASE(datatype);
    OBJ_DESTRUCT(&ompi_mpi_f90_integer_hashtable);
    OPAL_HASH_TABLE_FOREACH(key, uint32, datatype, &ompi_mpi_f90_real_hashtable)
        OBJ_RELEASE(datatype);
    OBJ_DESTRUCT(&ompi_mpi_f90_real_hashtable);
    OPAL_HASH_TABLE_FOREACH(key, uint32, datatype, &ompi_mpi_f90_complex_hashtable)
        OBJ_RELEASE(datatype);
    OBJ_DESTRUCT(&ompi_mpi_f90_complex_hashtable);

    /* If requested, print out a list of memory allocated by ALLOC_MEM
       but not freed by FREE_MEM */
    if (0 != ompi_debug_show_mpi_alloc_mem_leaks) {
        mca_mpool_base_tree_print (ompi_debug_show_mpi_alloc_mem_leaks);
    }

    OPAL_LIST_FOREACH_REV(finalize_item, &ompi_instance_finalize_fns, ompi_instance_finalize_fn_item_t) {
        ret = finalize_item->finalize_fn ();
        if (OPAL_UNLIKELY(OMPI_SUCCESS != ret)) {
            return ret;
        }
    }

    OPAL_LIST_DESTRUCT(&ompi_instance_finalize_fns);

    if (NULL != ompi_mpi_main_thread) {
        OBJ_RELEASE(ompi_mpi_main_thread);
        ompi_mpi_main_thread = NULL;
    }

    /* Leave the RTE */
    if (OMPI_SUCCESS != (ret = ompi_rte_finalize())) {
        return ret;
    }

    ompi_rte_initialized = false;

    for (int i = 0 ; ompi_lazy_frameworks[i] ; ++i) {
        if (0 < ompi_lazy_frameworks[i]->framework_refcnt) {
            /* May have been "opened" multiple times. We want it closed now! */
            ompi_lazy_frameworks[i]->framework_refcnt = 1;

            ret = mca_base_framework_close (ompi_lazy_frameworks[i]);
            if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
                return ret;
            }
        }
    }

    for (int j = 0 ; ompi_framework_dependencies[j] ; ++j) {
        ret = mca_base_framework_close (ompi_framework_dependencies[j]);
        if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
            return ret;
        }
    }

    OBJ_DESTRUCT(&ompi_mpi_instance_null);

    ompi_mpi_instance_release ();

    if (0 == opal_initialized) {
        /* if there is no MPI_T_init_thread that has been MPI_T_finalize'd,
         * then be gentle to the app and release all the memory now (instead
         * of the opal library destructor */
        opal_class_finalize ();
    }

    return OMPI_SUCCESS;
}

int ompi_mpi_instance_finalize (ompi_instance_t **instance)
{
    int ret = OMPI_SUCCESS;

    OBJ_RELEASE(*instance);

    opal_mutex_lock (&instance_lock);
    if (0 == opal_atomic_add_fetch_32 (&ompi_instance_count, -1)) {
        ret = ompi_mpi_instance_finalize_common ();
        if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
            opal_mutex_unlock (&instance_lock);
        }
    }
    opal_mutex_unlock (&instance_lock);

    *instance = &ompi_mpi_instance_null.instance;

    return ret;
}

static void ompi_instance_get_num_psets_complete (int status, opal_list_t *info,
                                                  void *cbdata, opal_pmix_release_cbfunc_t release_fn,
                                                  void *release_cbdata)
{
    opal_pmix_lock_t *lock = (opal_pmix_lock_t *) cbdata;
    opal_value_t *kv;

    OPAL_LIST_FOREACH(kv, info, opal_value_t) {
        if (0 == strcmp (kv->key, OPAL_PMIX_QUERY_NUM_PSETS)) {
            if (kv->data.size != ompi_mpi_instance_num_pmix_psets) {
                opal_argv_free (ompi_mpi_instance_pmix_psets);
                ompi_mpi_instance_pmix_psets = NULL;
            }

            ompi_mpi_instance_num_pmix_psets = kv->data.size;
        } else if (0 == strcmp (kv->key, OPAL_PMIX_QUERY_PSET_NAMES)) {
            if (ompi_mpi_instance_pmix_psets) {
                opal_argv_free (ompi_mpi_instance_pmix_psets);
            }

            fprintf (stderr, "PSETS: %s\n", kv->data.string);

            ompi_mpi_instance_pmix_psets = opal_argv_split (kv->data.string, ',');
            ompi_mpi_instance_num_pmix_psets = opal_argv_count (ompi_mpi_instance_pmix_psets);
            fprintf (stderr, "Num PSETS: %d\n", ompi_mpi_instance_num_pmix_psets);
        }
    }

    if (NULL != release_fn) {
        release_fn(release_cbdata);
    }
    OPAL_PMIX_WAKEUP_THREAD(lock);
}

static void ompi_instance_refresh_pmix_psets (const char *key)
{
    opal_list_t pmix_query_list;
    opal_pmix_query_t query;
    opal_pmix_lock_t lock;

    opal_mutex_lock (&instance_lock);

    OBJ_CONSTRUCT(&pmix_query_list, opal_list_t);
    OBJ_CONSTRUCT(&query, opal_pmix_query_t);
    OPAL_PMIX_CONSTRUCT_LOCK(&lock);

    opal_argv_append_nosize(&query.keys, key);
    opal_list_append (&pmix_query_list, &query.super);

    opal_pmix.query (&pmix_query_list, ompi_instance_get_num_psets_complete, (void *) &lock);
    OPAL_PMIX_WAIT_THREAD(&lock);

    opal_list_remove_item (&pmix_query_list, &query.super);
    OBJ_DESTRUCT(&query);
    OBJ_DESTRUCT(&pmix_query_list);
    OPAL_PMIX_DESTRUCT_LOCK(&lock);

    opal_mutex_unlock (&instance_lock);
}


int ompi_instance_get_num_psets (ompi_instance_t *instance, int *npset_names)
{
    ompi_instance_refresh_pmix_psets (OPAL_PMIX_QUERY_NUM_PSETS);
    *npset_names = ompi_instance_builtin_count + ompi_mpi_instance_num_pmix_psets;

    return OMPI_SUCCESS;
}

int ompi_instance_get_psetlen (ompi_instance_t *instance, int n, int *pset_name_len)
{
    if (NULL == ompi_mpi_instance_pmix_psets && n >= ompi_instance_builtin_count) {
        ompi_instance_refresh_pmix_psets (OPAL_PMIX_QUERY_PSET_NAMES);
    }

    if ((size_t) n >= (ompi_instance_builtin_count + ompi_mpi_instance_num_pmix_psets) || n < 0) {
        return OMPI_ERR_BAD_PARAM;
    }

    if (n < ompi_instance_builtin_count) {
        *pset_name_len = strlen (ompi_instance_builtin_psets[n]);
    } else {
        *pset_name_len = strlen (ompi_mpi_instance_pmix_psets[n - ompi_instance_builtin_count]);
    }

    return OMPI_SUCCESS;
}

int ompi_instance_get_nth_pset (ompi_instance_t *instance, int n, int len, char *pset_name)
{
    if (NULL == ompi_mpi_instance_pmix_psets && n >= ompi_instance_builtin_count) {
        ompi_instance_refresh_pmix_psets (OPAL_PMIX_QUERY_PSET_NAMES);
    }

    if ((size_t) n >= (ompi_instance_builtin_count + ompi_mpi_instance_num_pmix_psets) || n < 0) {
        return OMPI_ERR_BAD_PARAM;
    }

    if (n < ompi_instance_builtin_count) {
        strncpy (pset_name, ompi_instance_builtin_psets[n], len + 1);
    } else {
        strncpy (pset_name, ompi_mpi_instance_pmix_psets[n - ompi_instance_builtin_count], len + 1);
    }

    return OMPI_SUCCESS;
}

static int ompi_instance_group_world (ompi_instance_t *instance, ompi_group_t **group_out)
{
    ompi_group_t *group;
    size_t size;

    size = ompi_process_info.num_procs;

    group = ompi_group_allocate (size);
    if (OPAL_UNLIKELY(NULL == group)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    for (size_t i = 0 ; i < size ; ++i) {
        opal_process_name_t name = {.vpid = i, .jobid = OMPI_PROC_MY_NAME->jobid};
        /* look for existing ompi_proc_t that matches this name */
        group->grp_proc_pointers[i] = (ompi_proc_t *) ompi_proc_lookup (name);
        if (NULL == group->grp_proc_pointers[i]) {
            /* set sentinel value */
            group->grp_proc_pointers[i] = (ompi_proc_t *) ompi_proc_name_to_sentinel (name);
        } else {
            OBJ_RETAIN (group->grp_proc_pointers[i]);
        }
    }

    ompi_set_group_rank (group, ompi_proc_local());

    group->grp_instance = instance;

    *group_out = group;
    return OMPI_SUCCESS;
}

static int ompi_instance_group_shared (ompi_instance_t *instance, ompi_group_t **group_out)
{
    ompi_group_t *group;
    opal_process_name_t wildcard_rank;
    int ret;
    size_t size;
    char **peers;
    char *val;

    /* Find out which processes are local */
    wildcard_rank.jobid = OMPI_PROC_MY_NAME->jobid;
    wildcard_rank.vpid = OMPI_NAME_WILDCARD->vpid;

    OPAL_MODEX_RECV_VALUE(ret, OPAL_PMIX_LOCAL_PEERS, &wildcard_rank, &val, OPAL_STRING);
    if (OPAL_SUCCESS != ret || NULL == val) {
        return OMPI_ERROR;
    }

    peers = opal_argv_split(val, ',');
    free (val);
    if (OPAL_UNLIKELY(NULL == peers)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    size = opal_argv_count (peers);

    group = ompi_group_allocate (size);
    if (OPAL_UNLIKELY(NULL == group)) {
        opal_argv_free (peers);
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    for (size_t i = 0 ; NULL != peers[i] ; ++i) {
        opal_process_name_t name = {.vpid = strtoul(peers[i], NULL, 10), .jobid = OMPI_PROC_MY_NAME->jobid};
        /* look for existing ompi_proc_t that matches this name */
        group->grp_proc_pointers[i] = (ompi_proc_t *) ompi_proc_lookup (name);
        if (NULL == group->grp_proc_pointers[i]) {
            /* set sentinel value */
            group->grp_proc_pointers[i] = (ompi_proc_t *) ompi_proc_name_to_sentinel (name);
        } else {
            OBJ_RETAIN (group->grp_proc_pointers[i]);
        }
    }

    opal_argv_free (peers);

    /* group is dense */
    ompi_set_group_rank (group, ompi_proc_local());

    group->grp_instance = instance;

    *group_out = group;
    return OMPI_SUCCESS;
}

static int ompi_instance_group_self (ompi_instance_t *instance, ompi_group_t **group_out)
{
    ompi_group_t *group;
    size_t size;

    group = OBJ_NEW(ompi_group_t);
    if (OPAL_UNLIKELY(NULL == group)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    group->grp_proc_pointers = ompi_proc_self(&size);
    group->grp_my_rank       = 0;
    group->grp_proc_count   = size;

    /* group is dense */
    OMPI_GROUP_SET_DENSE (group);

    group->grp_instance = instance;

    *group_out = group;
    return OMPI_SUCCESS;
}

static int ompi_instance_group_pmix_pset (ompi_instance_t *instance, const char *pset_name, ompi_group_t **group_out)
{
    ompi_group_t *group;
    size_t size = 0;

    /* make the group large enough to hold world */
    group = ompi_group_allocate (ompi_process_info.num_procs);
    if (OPAL_UNLIKELY(NULL == group)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    for (size_t i = 0 ; i < size ; ++i) {
        opal_process_name_t name = {.vpid = i, .jobid = OMPI_PROC_MY_NAME->jobid};
        opal_value_t *val = NULL;
        int ret;

        ret = opal_pmix.get (&name, OPAL_PMIX_PSET_NAME, NULL, &val);
        if (OPAL_UNLIKELY(OPAL_SUCCESS != ret)) {
            OBJ_RELEASE(group);
            return ret;
        }

        if (0 != strcmp (pset_name, val->data.string)) {
            OBJ_RELEASE(val);
            continue;
        }
        OBJ_RELEASE(val);

        /* look for existing ompi_proc_t that matches this name */
        group->grp_proc_pointers[size] = (ompi_proc_t *) ompi_proc_lookup (name);
        if (NULL == group->grp_proc_pointers[size]) {
            /* set sentinel value */
            group->grp_proc_pointers[size] = (ompi_proc_t *) ompi_proc_name_to_sentinel (name);
        } else {
            OBJ_RETAIN (group->grp_proc_pointers[size]);
        }
        ++size;
    }

    /* shrink the proc array if needed */
    if (size < (size_t) group->grp_proc_count) {
        void *tmp = realloc (group->grp_proc_pointers, size * sizeof (group->grp_proc_pointers[0]));
        if (OPAL_UNLIKELY(NULL == tmp)) {
            OBJ_RELEASE(group);
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        group->grp_proc_pointers = (ompi_proc_t **) tmp;
        group->grp_proc_count = (int) size;
    }

    ompi_set_group_rank (group, ompi_proc_local());

    group->grp_instance = instance;

    *group_out = group;
    return OMPI_SUCCESS;
}

int ompi_group_from_pset (ompi_instance_t *instance, const char *pset_name, ompi_group_t **group_out)
{
    if (0 == strcmp (pset_name, "mpi://world")) {
        return ompi_instance_group_world (instance, group_out);
    }
    if (0 == strcmp (pset_name, "mpi://self")) {
        return ompi_instance_group_self (instance, group_out);
    }
    if (0 == strcmp (pset_name, "mpi://shared")) {
        return ompi_instance_group_shared (instance, group_out);
    }

    return ompi_instance_group_pmix_pset (instance, pset_name, group_out);
}

void ompi_mpi_instance_append_finalize (ompi_instance_finalize_fn_t finalize_fn)
{
    ompi_instance_finalize_fn_item_t *item = OBJ_NEW(ompi_instance_finalize_fn_item_t);
    /* NTH: this is tiny. if we couldn't allocate the sky is falling and we need to just
     * abort. */
    assert (NULL != item && NULL != finalize_fn);

    item->finalize_fn = finalize_fn;
    if (ompi_instance_basic_init) {
        opal_list_append (&ompi_instance_finalize_fns_basic, &item->super);
    } else {
        opal_list_append (&ompi_instance_finalize_fns, &item->super);
    }
}
