/**
 * \file
 * \brief Code for handling booting additional cores
 */

/*
 * Copyright (c) 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include "monitor.h"
#include <inttypes.h>
#include <barrelfish/spawn_client.h> // XXX: for cpu_type_to_archstr()

static errval_t intern_new_monitor(coreid_t id)
{
    errval_t err;

    for (int i = 0; i < MAX_CPUS; i++){
        if (intern[i].closure != NULL && i != id) {
            struct intermon_binding* bind;
            err = intern_get_closure(i, &bind);
            assert(err_is_ok(err));
            while (1) {
                err = bind->tx_vtbl.new_monitor_notify(bind, NOP_CONT, id);
                if (err_is_ok(err)) {
                    break;
                }
                if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
                    messages_wait_and_handle_next();
                } else {
                    return err;
                }
            }
        }
    }

    return SYS_ERR_OK;
}

/* Use to figure out when all monitors initialized. */
int seen_connections = 0;
static int num_monitors = 1;
bool monitors_up[MAX_CPUS];

static void send_boot_core_reply(struct monitor_binding *st,
                                 errval_t err)
{
    if(err_is_fail(err)) {
        err = err_push(err, MON_ERR_SPAWN_XCORE_MONITOR);
    }

    errval_t err2 = st->tx_vtbl.boot_core_reply(st, NOP_CONT, err);
    if (err_is_fail(err2)) {
        USER_PANIC_ERR(err2, "sending boot_core_reply failed");
    }
}

/**
 * \brief Based on number of monitors in the system,
 * returns number of connections created.
 */
static int get_num_connections(int num)
{
    if (num == 1 || num == 2) {
        return 0;
    }
    if (num == 3) {
        return 1;
    }

    return (num - 2) + get_num_connections(num - 1);
}

/**
 * \brief Msg handler for booting a given core
 *
 * \param id     id of the core to boot
 * \param hwid   hardware specific id of the core to boot
 * \param cpu_type  Type of cpu to boot
 * \param cmdline command-line args for kernel
 *
 * \bug Verify that cpu_type matches the elf image
 */
void boot_core_request(struct monitor_binding *st, coreid_t id, int hwid,
                       int int_cpu_type, char *cmdline)
{
    errval_t err;
    enum cpu_type cpu_type = (enum cpu_type)int_cpu_type;

    if (id == my_core_id) {
        err = MON_ERR_SAME_CORE;
        goto out;
    }

    if (cpu_type >= CPU_TYPE_NUM) {
        err = SPAWN_ERR_UNKNOWN_TARGET_ARCH;
        goto out;
    }

    printf("Monitor %d: booting %s core %d as '%s'\n", my_core_id,
           cpu_type_to_archstr(cpu_type), id, cmdline);

    monitors_up[id] = true;

    /* Assure memory server and chips have initialized */
    assert(mem_serv_iref != 0);
    assert(name_serv_iref != 0);
    assert(monitor_mem_iref != 0);

    err = spawn_xcore_monitor(id, hwid, cpu_type, cmdline);
    if(err_is_fail(err)) {
        goto out;
    }

    // Wait until the destination monitor initializes
    bool flag;
    do {
        err = intern_get_initialize(id, &flag);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "intern_get_initialize failed");
        }
        if (!flag) {
            // This waiting is fine, boot_manager will not send another msg
            // till it gets a reply from this.
            messages_wait_and_handle_next();
        }
    } while (!flag);

    /* Inform other monitors of this new monitor */
    err = intern_new_monitor(id);
    if (err_is_fail(err)) {
        err = err_push(err, MON_ERR_INTERN_NEW_MONITOR);
        goto out;
    }

    num_monitors++;

 out:
    free(cmdline);
    send_boot_core_reply(st, err);
}

/**
 * \brief XXX: This is a hack. Currently, we must know when all cores
 * are booted so that the monitors can initialize with each other,
 * setup routing tables and synchronize clocks.
 */
void boot_initialize_request(struct monitor_binding *st)
{
    errval_t err;

    monitors_up[my_core_id] = true;

    /* Wait for all monitors to initialize. */
    int num_connections = get_num_connections(num_monitors);
    while(num_connections > seen_connections) {
        // This waiting is fine, boot_manager will not send another msg
        // till it gets a reply from this.
        messages_wait_and_handle_next();
    }

    // Enable this for ping pong tests to a specific core
#if 0
#       define CORE     1
#       if 1
    assert (intern[CORE].closure);
    struct intermon_binding *mst = intern[CORE].closure;
    err = mst->tx_vtbl.pingpong(mst, NOP_CONT, 1);
    assert(err_is_ok(err));
#       else
    assert (intern[CORE].closure);
    struct intermon_binding *mst = intern[CORE].closure;
    err = mst->tx_vtbl.test_pingpong(mst, NOP_CONT, 1, 2, 3, 4, 5, 6, 7, 8);
    assert(err_is_ok(err));
#       endif
#endif

    printf("all %d monitors up\n", num_monitors);

#if !defined(__scc__) || defined(RCK_EMU)

#if 0 /* The current routing library has been deprecated.  The code is
         not removed as the multi-core cap mgmt code is closely
         intertwined with it. Once we have a usable routing library,
         we can adapt the cap mgmt code to it.
       */
    if (num_monitors > 32) {
        printf("XXX: Not routing for %d cores. "
               "Monitor runs out lmp endpoints. "
               "Remote caps with routing will not work. "
               "New routing lib will fix this.\n",
               num_monitors);
    } else {
        if (num_monitors > 1) {
            printf("monitor: initializing routes\n");
            routes_up = false;
            err = route_initialize_bsp(monitors_up);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "route_initialize failed");
                abort();
            }

            while(!routes_up) {
                // This waiting is fine, boot_manager will not send another msg
                // till it gets a reply from this.
                messages_wait_and_handle_next();
            }
        }
    }
#endif

    if(num_monitors > 1) {
        printf("monitor: synchronizing clocks\n");
        err = timing_sync_timer();
        assert(err_is_ok(err) || err_no(err) == SYS_ERR_SYNC_MISS);
        if(err_no(err) == SYS_ERR_SYNC_MISS) {
            printf("monitor: failed to sync clocks. Bad reference clock?\n");
        }
    }
#endif

    err = st->tx_vtbl.boot_initialize_reply(st, NOP_CONT);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "boot_initialize_reply failed");
    }
}
