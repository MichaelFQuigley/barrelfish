/**
 * \file
 * \brief RAM allocator code (client-side)
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/core_state.h>

#include <if/monitor_defs.h>
#include <if/mem_rpcclient_defs.h>

/* remote (indirect through a channel) version of ram_alloc, for most domains */
static errval_t ram_alloc_remote(struct capref *ret, uint8_t size_bits,
                                 uint64_t minbase, uint64_t maxlimit)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    errval_t err, result;

    // XXX: the transport that ram_alloc uses will allocate slots,
    // which may cause slot_allocator to grow itself.
    // To grow itself, the slot_allocator needs to call ram_alloc.
    // However, ram_alloc has a mutex to protect the lower level transport code.
    // Therefore, we detect the situation when the slot_allocator
    // may grow itself and grow it before acquiring the lock.
    // Once this code become reentrant, this hack can be removed. -Akhi
    struct slot_alloc_state *sas = get_slot_alloc_state();
    struct slot_allocator *ca = (struct slot_allocator*)(&sas->defca);
    if (ca->space == 1) {
        struct capref cap;
        err = slot_alloc(&cap);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_SLOT_ALLOC);
        }
        err = slot_free(cap);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_SLOT_FREE);
        }
    }

    assert(ret != NULL);

    thread_mutex_lock(&ram_alloc_state->ram_alloc_lock);

    struct mem_rpc_client *b = get_mem_client();
    err = b->vtbl.allocate(b, size_bits, minbase, maxlimit, &result, ret);

    thread_mutex_unlock(&ram_alloc_state->ram_alloc_lock);

    if (err_is_fail(err)) {
        return err;
    }

    return result;
}


void ram_set_affinity(uint64_t minbase, uint64_t maxlimit)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    ram_alloc_state->default_minbase = minbase;
    ram_alloc_state->default_maxlimit = maxlimit;
}

void ram_get_affinity(uint64_t *minbase, uint64_t *maxlimit)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    *minbase  = ram_alloc_state->default_minbase;
    *maxlimit = ram_alloc_state->default_maxlimit;
}

#define OBJSPERPAGE_CTE         (1 << (BASE_PAGE_BITS - OBJBITS_CTE))

errval_t ram_alloc_fixed(struct capref *ret, uint8_t size_bits,
                         uint64_t minbase, uint64_t maxlimit)
{
    struct ram_alloc_state *state = get_ram_alloc_state();

    if (size_bits == BASE_PAGE_BITS) {
        // XXX: Return error if check to see if out of slots
        assert(state->base_capnum < OBJSPERPAGE_CTE);
        ret->cnode = cnode_base;
        ret->slot  = state->base_capnum++;
        return SYS_ERR_OK;
    } else {
        return LIB_ERR_RAM_ALLOC_WRONG_SIZE;
    }
}

/**
 * \brief Allocates memory in the form of a RAM capability
 *
 * \param ret Pointer to capref struct, filled-in with allocated cap location
 * \param size_bits Amount of RAM to allocate, as a power of two
 *              slot used for the cap in #ret, if any
 */
errval_t ram_alloc(struct capref *ret, uint8_t size_bits)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    assert(ram_alloc_state->ram_alloc_func != NULL);
    return ram_alloc_state->
        ram_alloc_func(ret, size_bits, ram_alloc_state->default_minbase,
                       ram_alloc_state->default_maxlimit);
}

static void bind_continuation(void *st, errval_t err, struct mem_binding *b)
{
    struct ram_alloc_state *ram_alloc_state = st;
    assert(st == get_ram_alloc_state());

    if (err_is_ok(err)) {
        struct mem_rpc_client *r = malloc(sizeof(struct mem_rpc_client));
        assert(r != NULL);
        err = mem_rpc_client_init(r, b);
        if (err_is_fail(err)) {
            free(r);
            USER_PANIC_ERR(err, "error in mem_rpc_client_init");
        } else {
            set_mem_client(r);
        }
    }

    ram_alloc_state->mem_connect_err = err;
    ram_alloc_state->mem_connect_done = true;
}

static void get_mem_iref_reply(struct monitor_binding *mb, iref_t iref)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    assert(iref != 0);

    errval_t err = mem_bind(iref, bind_continuation, ram_alloc_state,
                            get_default_waitset(), IDC_BIND_FLAG_RPC_CAP_TRANSFER);
    if (err_is_fail(err)) {
        ram_alloc_state->mem_connect_err = err;
        ram_alloc_state->mem_connect_done = true;
    }
}

/**
 * \brief Initialize the dispatcher specific state of ram_alloc
 */
void ram_alloc_init(void)
{
    /* Initialize the ram_alloc_state */
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    ram_alloc_state->mem_connect_done = false;
    ram_alloc_state->mem_connect_err  = 0;
    thread_mutex_init(&ram_alloc_state->ram_alloc_lock);
    ram_alloc_state->ram_alloc_func   = NULL;
    ram_alloc_state->default_minbase  = 0;
    ram_alloc_state->default_maxlimit = 0;
    ram_alloc_state->base_capnum      = 0;
}

static errval_t ram_free_remote(struct capref ramcap);

/**
 * \brief Set ram_alloc to the default ram_alloc_remote or to a given function
 *
 * Also need to provide a corresponding free function, or NULL. If
 * local_free is NULL, memory reclamation is disabled. Upon free(),
 * data will stay in malloc()'s free list. This is useful for special
 * initialization domains (e.g. the monitors) that cannot give back
 * memory and it is better to keep it in their free lists instead of
 * providing a no-op free function, which would leak memory.
 *
 * If local_allocator is NULL, local_free is ignored and both will be
 * initialized to the default remote allocator.
 */
errval_t ram_alloc_set(ram_alloc_func_t local_allocator,
                       ram_free_func_t local_free)
{
    errval_t err;
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();

    /* Special case */
    if (local_allocator != NULL) {
        ram_alloc_state->ram_alloc_func = local_allocator;
        ram_alloc_state->ram_free_func = local_free;
        return SYS_ERR_OK;
    }

    /* fire off a request for the iref for mem_serv */
    struct monitor_binding *mb = get_monitor_binding();
    mb->rx_vtbl.get_mem_iref_reply = get_mem_iref_reply;
    err = mb->tx_vtbl.get_mem_iref_request(mb, NOP_CONT);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_GET_MEM_IREF);
    }

    /* XXX: wait for connection to complete */
    while (!ram_alloc_state->mem_connect_done) {
        messages_wait_and_handle_next();
    }

    if (err_is_ok(ram_alloc_state->mem_connect_err)) {
        ram_alloc_state->ram_alloc_func = ram_alloc_remote;
        ram_alloc_state->ram_free_func = ram_free_remote;
    }
    return ram_alloc_state->mem_connect_err;
}

/* remote (indirect through a channel) version of ram_free, for most domains */
static errval_t ram_free_remote(struct capref ramcap)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    errval_t err, result;

    thread_mutex_lock(&ram_alloc_state->ram_alloc_lock);

    struct mem_rpc_client *b = get_mem_client();
    err = b->vtbl.free(b, ramcap, &result);

    thread_mutex_unlock(&ram_alloc_state->ram_alloc_lock);

    if (err_is_fail(err)) {
        return err;
    }

    return result;
}

errval_t ram_free_fixed(struct capref ramcap)
{
    assert(!"NYI");
    // can't free a cap from the fixed supply
    return LIB_ERR_NOT_IMPLEMENTED;
}

errval_t ram_free(struct capref ramcap)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    assert(ram_alloc_state->ram_free_func != NULL);
    return ram_alloc_state->ram_free_func(ramcap);
}
