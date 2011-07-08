/**
 * \file
 * \brief Distributed (percore) memory server: stealing related code
 */

/*
 * Copyright (c) 2007-2011, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <inttypes.h>
#include <barrelfish/barrelfish.h>
#include <trace/trace.h>
#include <mm/mm.h>

#include <thc/thc.h>
#include <thc/thcsync.h>

#include <if/mem_defs.h>
#include <if/mem_rpcclient_defs.h>
#include <if/mem_thc.h>

// #include "barrier.h"

#include "mem_serv.h"
#include "steal.h"

/// Keep track of our peer mem_servs
struct peer_core {
    coreid_t id;
    bool is_bound;
    struct mem_thc_client_binding_t cl; 
    thc_lock_t lock;
};

coreid_t mycore;
static struct peer_core *peer_cores;
static int num_peers;

// FIXME: possible race if handling two concurrent alloc request that both 
// try to connect to the same peer
static errval_t connect_peer(struct peer_core *peer)
{
    assert(peer != NULL);

    errval_t err;
    struct mem_binding *b;

    // debug_printf("connecting to %u\n", peer->id);

    char service_name[NAME_LEN];
    snprintf(service_name, NAME_LEN, "%s.%d", MEMSERV_DIST, peer->id);
    
    err = mem_thc_connect_by_name(service_name,
                                  get_default_waitset(),
                                  IDC_BIND_FLAGS_DEFAULT,
                                  &b);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not bind (thc)");
        return err;
    }

    err = mem_thc_init_client(&peer->cl, b, b);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not init client (thc)");
        return err;
    }

    thc_lock_init(&peer->lock);

    return SYS_ERR_OK;
}


static errval_t steal_from_serv(struct peer_core *peer, 
                                struct capref *ret_cap, 
                                uint8_t bits, 
                                genpaddr_t minbase, 
                                genpaddr_t maxlimit)
{
    assert(peer != NULL);

    errval_t err;

    // debug_printf("trying to steal from: %u\n", peer->id);

    if (!peer->is_bound) {
        err = connect_peer(peer);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "failed to connect to peer");
            return err;
        }
        peer->is_bound = true;
    }

    // due to the single-waiter rule of thc we need to make sure we only 
    // ever have one of these rpcs outstanding at a time.
    thc_lock_acquire(&peer->lock);
    peer->cl.call_seq.steal(&peer->cl, bits, minbase, maxlimit, 
                                &err, ret_cap);
    thc_lock_release(&peer->lock);

    return err;
}

// Round-robin steal. Try to get memory from each peer in turn.
static errval_t rr_steal(struct capref *ret_cap, uint8_t bits,
                                genpaddr_t minbase, genpaddr_t maxlimit)
{
    errval_t err = MM_ERR_NOT_FOUND;
    static int next_serv = 0;

    int serv = next_serv;
    struct peer_core *peer = &peer_cores[serv];

    int i;
    for (i = 0; i < num_peers; i++) {
        if (serv >= num_peers) {
            serv = 0;
        }
        peer = &peer_cores[serv++];
        if (peer->id == mycore) {
            continue;
        }
        err = steal_from_serv(peer, ret_cap, bits, minbase, maxlimit);
        if (err_is_ok(err)) {
            break;
        }
    }
    next_serv = serv;

    /*
    if (i >= num_peers) {
        debug_printf("rr_steal: could not steal from any peers\n");
    }
    */

    return err; 
}


static errval_t steal_and_alloc(struct capref *ret_cap, uint8_t steal_bits, 
                                uint8_t alloc_bits, 
                                genpaddr_t minbase, genpaddr_t maxlimit)
{
    errval_t err;

    struct capref ramcap;
    struct capability info;

    /*
    debug_printf("steal_and_alloc(steal_bits: %d, alloc_bits: %d, "
                 "minbase: 0x%"PRIxGENPADDR", maxlimit: 0x%"PRIxGENPADDR")\n",
                 steal_bits, alloc_bits, minbase, maxlimit);
    */

    err = rr_steal(&ramcap, steal_bits, minbase, maxlimit);
    if (err_is_fail(err)) {
        return err;
    }

    err = debug_cap_identify(ramcap, &info);
    if (err_is_fail(err)) {
        return err_push(err, MON_ERR_CAP_IDENTIFY);
    }

#if 0
    debug_printf("Cap is type %d Ram base 0x%"PRIxGENPADDR
                 " (%"PRIuGENPADDR") Bits %d\n",
                 info.type, info.u.ram.base, info.u.ram.base, 
                 info.u.ram.bits);
#endif
    assert(steal_bits == info.u.ram.bits);


    memsize_t mem_to_add = (memsize_t)1 << info.u.ram.bits;
    mem_total += mem_to_add;
        
    err = mm_add(&mm_percore, ramcap, info.u.ram.bits, info.u.ram.base);
    if (err_is_fail(err)) {
        return err_push(err, MM_ERR_MM_ADD);
    }
    mem_avail += mem_to_add;

    err = percore_alloc(ret_cap, alloc_bits, minbase, maxlimit);    

    return err;
} 


void try_steal(errval_t *ret, struct capref *cap, uint8_t bits,
               genpaddr_t minbase, genpaddr_t maxlimit)
{
    // debug_printf("failed percore alloc request: bits: %d going to STEAL\n",
    //              bits);
    //DEBUG_ERR(*ret, "allocation of %d bits in 0x%" PRIxGENPADDR 
    //           "-0x%" PRIxGENPADDR " failed", bits, minbase, maxlimit);
    *ret = steal_and_alloc(cap, bits+1, bits, minbase, maxlimit);
    if (err_is_fail(*ret)) {
        // DEBUG_ERR(*ret, "stealing of %d bits in 0x%" PRIxGENPADDR "-0x%" 
        //          PRIxGENPADDR " failed", bits, minbase, maxlimit);
        *cap = NULL_CAP;
    }
}

errval_t init_peers(coreid_t core, int len_cores, coreid_t *cores) 
{
    // initialise info about our peers 
    mycore = core;
    num_peers = len_cores;
    peer_cores = malloc(num_peers * sizeof(struct peer_core));
    if (peer_cores == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    
    for (int i = 0; i < num_peers; i++) {
        peer_cores[i].id = cores[i];
        peer_cores[i].is_bound = false;
    }

    return SYS_ERR_OK;
}

errval_t percore_steal_handler_common(uint8_t bits,
                                      genpaddr_t minbase, 
                                      genpaddr_t maxlimit,
                                      struct capref *retcap)
{
    struct capref cap;
    errval_t err, ret;

    trace_event(TRACE_SUBSYS_PERCORE_MEMSERV, TRACE_EVENT_ALLOC, bits);
    // debug_printf("percore steal request: bits: %d\n", bits);

    // refill slot allocator if needed 
    err = slot_prealloc_refill(mm_percore.slot_alloc_inst);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Warning: failure in slot_prealloc_refill\n");
    }

    // refill slab allocator if needed 
    err = slab_refill(&mm_percore.slabs);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Warning: failure when refilling mm_percore slab\n");
    }

    // get actual ram cap 
    ret = percore_alloc(&cap, bits, minbase, maxlimit);
    if (err_is_fail(ret)){
        // debug_printf("percore steal request failed\n");
        //DEBUG_ERR(ret, "allocation of stolen %d bits in 0x%" PRIxGENPADDR 
        //          "-0x%" PRIxGENPADDR " failed", bits, minbase, maxlimit);
        cap = NULL_CAP;
    }

    trace_event(TRACE_SUBSYS_PERCORE_MEMSERV, TRACE_EVENT_ALLOC_COMPLETE, bits);

    *retcap = cap;
    return ret;
}


int main(int argc, char ** argv)
{
    return common_main(argc, argv);
}
