/** \file
 *  \brief Monitor's connection with the dispatchers on the same core for 
 *  blocking rpc calls.
 */

/*
 * Copyright (c) 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */


#include "monitor.h"
#include <barrelfish/monitor_client.h>

/*-------------------------- Internal structures ----------------------------*/

struct retype_st {
    struct rcap_st rcap_st;             // must always be first
    struct monitor_blocking_binding *b;
    struct capref croot;
    caddr_t src;
    uint64_t new_type;
    uint8_t size_bits;
    caddr_t to;
    caddr_t slot; 
    int dcn_vbits;
};

struct delete_st {
    struct rcap_st rcap_st;             // must always be first
    struct monitor_blocking_binding *b;
    struct capref croot;
    caddr_t src;
    uint8_t vbits;
};

struct revoke_st {
    struct rcap_st rcap_st;             // must always be first
    struct monitor_blocking_binding *b;
    struct capref croot;
    caddr_t src;
    uint8_t vbits;
};

/*------------------------ Static global variables -------------------------*/

static struct retype_st static_retype_state;
static bool static_retype_state_used = false;

static struct delete_st static_delete_state;
static bool static_delete_state_used = false;

static struct revoke_st static_revoke_state;
static bool static_revoke_state_used = false;

static uint32_t current_route_id = 0;

/*-------------------------- Helper Functions ------------------------------*/

static void remote_cap_retype_phase_2(void * st_arg);
static void remote_cap_delete_phase_2(void * st_arg);
static void remote_cap_revoke_phase_2(void * st_arg);

// workaround inlining bug with gcc 4.4.1 shipped with ubuntu 9.10 and 4.4.3 in Debian
#if defined(__i386__) && defined(__GNUC__) \
    && __GNUC__ == 4 && __GNUC_MINOR__ == 4 && __GNUC_PATCHLEVEL__ <= 3
static __attribute__((noinline)) struct retype_st *
alloc_retype_st(struct monitor_blocking_binding *b, struct capref croot,
                caddr_t src, uint64_t new_type, uint8_t size_bits,
                caddr_t to, caddr_t slot, int dcn_vbits)
#else
static struct retype_st *alloc_retype_st(struct monitor_blocking_binding *b,
                                        struct capref croot, caddr_t src, 
                                        uint64_t new_type, uint8_t size_bits,
                                        caddr_t to, caddr_t slot, int dcn_vbits)
#endif
{
    struct retype_st * st;
    if (static_retype_state_used) {
        st = malloc (sizeof(struct retype_st));
        assert(st);
    } else {
        st = &static_retype_state;
        static_retype_state_used = true;
    }

    st->rcap_st.free_at_ccast = false;
    st->rcap_st.cb            = remote_cap_retype_phase_2;
    st->b        = b;
    st->croot    = croot;
    st->src      = src;
    st->new_type = new_type;
    st->size_bits= size_bits;
    st->to       = to;
    st->slot     = slot;
    st->dcn_vbits= dcn_vbits;

    return st;
}

static void free_retype_st(struct retype_st * st)
{
    cap_destroy(st->croot);
    if (st == &static_retype_state) {
        static_retype_state_used = false;
    } else {
        free(st);
    }
}

// workaround inlining bug with gcc 4.4.1 shipped with ubuntu 9.10 and 4.4.3 in Debian
#if defined(__i386__) && defined(__GNUC__) \
    && __GNUC__ == 4 && __GNUC_MINOR__ == 4 && __GNUC_PATCHLEVEL__ <= 3
static __attribute__((noinline)) struct delete_st *
alloc_delete_st(struct monitor_blocking_binding *b, struct capref croot,
                caddr_t src, uint8_t vbits)
#else
static struct delete_st* alloc_delete_st(struct monitor_blocking_binding *b,
                                         struct capref croot, caddr_t src, 
                                         uint8_t vbits)
#endif
{
    struct delete_st * st;
    if (static_delete_state_used) {
        st = malloc (sizeof(struct delete_st));
        assert(st);
    } else {
        st = &static_delete_state;
        static_delete_state_used = true;
    }

    st->rcap_st.free_at_ccast = false;
    st->rcap_st.cb            = remote_cap_delete_phase_2;
    st->b        = b;
    st->croot    = croot;
    st->src      = src;
    st->vbits    = vbits;

    return st;
}

static void free_delete_st(struct delete_st * st)
{
    cap_destroy(st->croot);
    if (st == &static_delete_state) {
        static_delete_state_used = false;
    } else {
        free(st);
    }
}

// workaround inlining bug with gcc 4.4.1 shipped with ubuntu 9.10 and 4.4.3 in Debian
#if defined(__i386__) && defined(__GNUC__) \
    && __GNUC__ == 4 && __GNUC_MINOR__ == 4 && __GNUC_PATCHLEVEL__ <= 3
static __attribute__((noinline)) struct revoke_st *
alloc_revoke_st(struct monitor_blocking_binding *b, struct capref croot,
                caddr_t src, uint8_t vbits)
#else
static struct revoke_st *alloc_revoke_st(struct monitor_blocking_binding *b,
                                         struct capref croot, caddr_t src, 
                                         uint8_t vbits)
#endif
{
    struct revoke_st * st;
    if (static_revoke_state_used) {
        st = malloc (sizeof(struct revoke_st));
        assert(st);
    } else {
        st = &static_revoke_state;
        static_revoke_state_used = true;
    }

    st->rcap_st.free_at_ccast = false;
    st->rcap_st.cb            = remote_cap_revoke_phase_2;
    st->b        = b;
    st->croot    = croot;
    st->src      = src;
    st->vbits    = vbits;

    return st;
}

static void free_revoke_st(struct revoke_st * st)
{
    cap_destroy(st->croot);
    if (st == &static_revoke_state) {
        static_revoke_state_used = false;
    } else {
        free(st);
    }
}


/*---------------------------- Handler functions ----------------------------*/



static void remote_cap_retype(struct monitor_blocking_binding *b,
                              struct capref croot, caddr_t src, 
                              uint64_t new_type, uint8_t size_bits,
                              caddr_t to, caddr_t slot, int dcn_vbits) 
{
    errval_t err;
    bool has_descendants;
    coremask_t on_cores;

    /* Save state for stackripped reply */
    struct retype_st * st = alloc_retype_st(b, croot, src, new_type, size_bits,
                                            to, slot, dcn_vbits);

    
    /* Get the raw cap from the kernel */
    err = monitor_domains_cap_identify(croot, src, CPTR_BITS, 
                                       &(st->rcap_st.capability));
    if (err_is_fail(err)) {
        err_push(err, MON_ERR_CAP_REMOTE);
        goto reply;
    }    

    /* Check if cap is retyped, if it is there is no point continuing,
       This will be checked again once we succeed in locking cap */
    err = rcap_db_get_info(&st->rcap_st.capability, &has_descendants, &on_cores);
    assert(err_is_ok(err));
    if (has_descendants) {
        err = MON_ERR_REMOTE_CAP_NEED_REVOKE;
        goto reply;
    }

    /* request lock */
    err = rcap_db_acquire_lock(&st->rcap_st.capability, (struct rcap_st*)st);
    if (err_is_fail(err)) {
        goto reply;
    }
    return;  // continues in remote_cap_retype_phase_2

reply:
    free_retype_st(st);
    err = b->tx_vtbl.remote_cap_retype_response(b, NOP_CONT, err);
    assert(err_is_ok(err));
}


static void remote_cap_retype_phase_2(void * st_arg)
{
    errval_t err, reply_err;
    bool has_descendants;
    coremask_t on_cores;
    struct retype_st * st = (struct retype_st *) st_arg;    
    struct monitor_blocking_binding *b = st->b;
    

    reply_err = st->rcap_st.err;

    err = rcap_db_get_info(&st->rcap_st.capability, &has_descendants, &on_cores);

    assert(err_is_ok(err));
    if (has_descendants) {
        reply_err = MON_ERR_REMOTE_CAP_NEED_REVOKE;
    }

    if (err_is_fail(reply_err)) {
        // lock failed or cap already retyped, unlock any cores we locked
        err = rcap_db_release_lock(&(st->rcap_st.capability), st->rcap_st.cores_locked);
        assert (err_is_ok(err));
    } else { 
        // all good, do retype on domains behalf
        reply_err = monitor_retype_remote_cap(st->croot, st->src, st->new_type, 
                                              st->size_bits, st->to, st->slot, 
                                              st->dcn_vbits);

        // signal if retype was a success to remote cores
        err = rcap_db_retype(&(st->rcap_st.capability), err_is_ok(reply_err));
        assert (err_is_ok(err));
    }

    free_retype_st(st);
    err = b->tx_vtbl.remote_cap_retype_response(b, NOP_CONT, reply_err);
    assert (err_is_ok(err));
}


static void remote_cap_delete(struct monitor_blocking_binding *b,
                              struct capref croot, caddr_t src, uint8_t vbits)
{
    errval_t err;

    /* Save state for stackripped reply */
    struct delete_st * st = alloc_delete_st(b, croot, src, vbits);
    
    /* Get the raw cap from the kernel */
    err = monitor_domains_cap_identify(croot, src, vbits, 
                                       &(st->rcap_st.capability));
    if (err_is_fail(err)) {
        err_push(err, MON_ERR_CAP_REMOTE);
        goto reply;
    }

    /* request lock */
    err = rcap_db_acquire_lock(&(st->rcap_st.capability), (struct rcap_st*)st);
    if (err_is_fail(err)) {
        goto reply;
    }
    return;  // continues in remote_cap_retype_phase_2

reply:
    free_delete_st(st);
    err = b->tx_vtbl.remote_cap_delete_response(b, NOP_CONT, err);
    assert(err_is_ok(err));
}

static void remote_cap_delete_phase_2(void * st_arg)
{
    errval_t err, reply_err;
    struct delete_st * st = (struct delete_st *) st_arg;    
    struct monitor_blocking_binding *b = st->b;
    
    reply_err = st->rcap_st.err;
    if (err_is_fail(reply_err)) {
        // lock failed, unlock any cores we locked
        err = rcap_db_release_lock(&(st->rcap_st.capability), 
                                   st->rcap_st.cores_locked);
        assert (err_is_ok(err));
    } else {
        // all good, do delete on domains behalf
        reply_err = monitor_delete_remote_cap(st->croot, st->src, st->vbits);
        if (err_is_fail(reply_err)) {
            DEBUG_ERR(reply_err, "delete cap error");
        }
        
        if (err_is_ok(reply_err)) {
            // signal delete to other cores
            err = rcap_db_delete(&st->rcap_st.capability);
            assert(err_is_ok(err));
        }
    }

    free_delete_st(st);
    err = b->tx_vtbl.remote_cap_delete_response(b, NOP_CONT, reply_err);
    assert (err_is_ok(err));
}


static void remote_cap_revoke(struct monitor_blocking_binding *b,
                              struct capref croot, caddr_t src, uint8_t vbits)
{
    errval_t err;
    /* Save state for stackripped reply */
    struct revoke_st * st = alloc_revoke_st(b, croot, src, vbits);
    
    /* Get the raw cap from the kernel */
    err = monitor_domains_cap_identify(croot, src, vbits, 
                                       &(st->rcap_st.capability));
    if (err_is_fail(err)) {
        err_push(err, MON_ERR_CAP_REMOTE);
        goto reply;
    }

    /* request recursive lock on the cap and all of its descendants */
    err = rcap_db_acquire_recursive_lock(&(st->rcap_st.capability), 
                                         (struct rcap_st*)st);
    if (err_is_fail(err)) {
        goto reply;
    }
    return;  // continues in remote_cap_retype_phase_2

reply:
    free_revoke_st(st);
    err = b->tx_vtbl.remote_cap_revoke_response(b, NOP_CONT, err);
    assert(err_is_ok(err));
}

static void remote_cap_revoke_phase_2(void * st_arg)
{
    errval_t err, reply_err;
    struct revoke_st * st = (struct revoke_st *) st_arg;    
    struct monitor_blocking_binding *b = st->b;
    
    reply_err = st->rcap_st.err;
    if (err_is_fail(reply_err)) {
        // recursive lock failed, unlock any cores we locked
        err = rcap_db_release_recursive_lock(&(st->rcap_st.capability), 
                                             st->rcap_st.cores_locked);
        assert (err_is_ok(err));
    } else {
        // all good, do revole on domains behalf
        reply_err = monitor_revoke_remote_cap(st->croot, st->src, st->vbits);
        if (err_is_fail(reply_err)) {
            DEBUG_ERR(reply_err, "revoke cap error");
        }
        
        if (err_is_ok(reply_err)) {
            // signal revoke to other cores
            err = rcap_db_revoke(&st->rcap_st.capability);
            assert(err_is_ok(err));
        }
    }

    free_revoke_st(st);
    err = b->tx_vtbl.remote_cap_revoke_response(b, NOP_CONT, reply_err);
    assert (err_is_ok(err));
}


routeid_t mon_allocate_route_id(void) 
{
    coreid_t coreid = disp_get_core_id();
    assert (coreid < 0xff);
    assert (++(current_route_id) < 0xffffff);   // XXX TODO - deal with rollover
    
    return (coreid << 24) | current_route_id;
}

static void allocate_route_id(struct monitor_blocking_binding *st)
{
    errval_t err;
    uint32_t route_id = mon_allocate_route_id();
    err = st->tx_vtbl.allocate_route_id_response(st, NOP_CONT, route_id);
    assert (err_is_ok(err));
}

static void rsrc_manifest(struct monitor_blocking_binding *b,
                          struct capref dispcap, char *str)
{
    errval_t err, err2;
    rsrcid_t id;

    err = rsrc_new(&id);
    if(err_is_fail(err)) {
        goto out;
    }
    err = rsrc_join(id, dispcap, b);
    if(err_is_fail(err)) {
        // TODO: Cleanup
        goto out;
    }
    err = rsrc_submit_manifest(id, str);

 out:
    err2 = b->tx_vtbl.rsrc_manifest_response(b, NOP_CONT, id, err);
    assert(err_is_ok(err2));
}

static void rsrc_phase(struct monitor_blocking_binding *b,
                       rsrcid_t id, uint32_t phase)
{
    errval_t err;

    err = rsrc_set_phase(id, phase);
    assert(err_is_ok(err));

    err = b->tx_vtbl.rsrc_phase_response(b, NOP_CONT);
    assert(err_is_ok(err));
}

static void rpc_rsrc_join(struct monitor_blocking_binding *b,
                          rsrcid_t id, struct capref dispcap)
{
    errval_t err, err2;

    err = rsrc_join(id, dispcap, b);

    if(err_is_fail(err)) {
        err2 = b->tx_vtbl.rsrc_join_response(b, NOP_CONT, err);
        assert(err_is_ok(err2));
    }
}

static void alloc_monitor_ep(struct monitor_blocking_binding *b)
{
    struct capref retcap = NULL_CAP;
    errval_t err, reterr = SYS_ERR_OK;

    struct monitor_lmp_binding *lmpb =
        malloc(sizeof(struct monitor_lmp_binding));
    assert(lmpb != NULL);

    // setup our end of the binding
    err = monitor_client_lmp_accept(lmpb, get_default_waitset(),
                                    DEFAULT_LMP_BUF_WORDS);
    if (err_is_fail(err)) {
        free(lmpb);
        reterr = err_push(err, LIB_ERR_MONITOR_CLIENT_ACCEPT);
        goto out;
    }

    retcap = lmpb->chan.local_cap;
    monitor_server_init(&lmpb->b);

out:
    err = b->tx_vtbl.alloc_monitor_ep_response(b, NOP_CONT, reterr, retcap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to send alloc_monitor_ep_reply");
    }
}

static void cap_identify(struct monitor_blocking_binding *b,
                         struct capref cap)
{
    errval_t err, reterr;

    union capability_caprep_u u;
    reterr = monitor_cap_identify(cap, &u.cap);
    err = b->tx_vtbl.cap_identify_response(b, NOP_CONT, reterr, u.caprepb);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "reply failed");
    }

    /* XXX: shouldn't we skip this if we're being called from the monitor?
     * apparently not: we make a copy of the cap on LMP to self?!?! */
    err = cap_destroy(cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_destroy failed");
    }
}

static void irq_handle_call(struct monitor_blocking_binding *b, struct capref ep)
{
    /* allocate a new slot in the IRQ table */
    // XXX: probably want to be able to reuse vectors! :)
    static int nextvec = 0;
    int vec = nextvec++;

    /* set it and reply */
    errval_t err = invoke_irqtable_set(cap_irq, vec, ep);
    errval_t err2 = b->tx_vtbl.irq_handle_response(b, NOP_CONT, err, vec);
    assert(err_is_ok(err2));
}

/*------------------------- Initialization functions -------------------------*/

static struct monitor_blocking_rx_vtbl rx_vtbl = {
    .remote_cap_retype_call  = remote_cap_retype,
    .remote_cap_delete_call  = remote_cap_delete,
    .remote_cap_revoke_call  = remote_cap_revoke,

    .allocate_route_id_call  = allocate_route_id,

    .rsrc_manifest_call      = rsrc_manifest,
    .rsrc_join_call          = rpc_rsrc_join,
    .rsrc_phase_call         = rsrc_phase,

    .alloc_monitor_ep_call   = alloc_monitor_ep,
    .cap_identify_call       = cap_identify,
    .irq_handle_call         = irq_handle_call,
};

static void export_callback(void *st, errval_t err, iref_t iref)
{
    assert(err_is_ok(err));
    set_monitor_rpc_iref(iref);
}

static errval_t connect_callback(void *st, struct monitor_blocking_binding *b)
{
    b->rx_vtbl = rx_vtbl;

    // TODO: set error handler
    return SYS_ERR_OK;
}

errval_t monitor_rpc_init(void)
{
    errval_t err;
    err = monitor_blocking_export(NULL, export_callback, connect_callback, 
                                  get_default_waitset(), 
                                  IDC_EXPORT_FLAGS_DEFAULT);

    return err;
}
