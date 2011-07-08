/**
 * \file
 * \brief Flounder stubs
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <barrelfish/barrelfish.h>
#include <string.h>
#include <barrelfish/nameservice_client.h>
#include <barrelfish/spawn_client.h>
#include <bench/bench.h>
#include <if/bench_defs.h>

static char my_name[100];

static struct bench_binding *binding;
static coreid_t my_core_id;

#define MAX_COUNT 1000

struct timestamp {
    cycles_t time0;
    cycles_t time1;
};
static struct timestamp timestamps[MAX_COUNT];
static int benchmark_type = 0;

static void experiment(void)
{
    errval_t err;
    static bool flag = false;
    static int i = 0;

    // Experiment finished
    if (i == MAX_COUNT - 1) {
        timestamps[i].time1 = bench_tsc();

        for (int j = MAX_COUNT / 10; j < MAX_COUNT; j++) {
            printf("page %d took %"PRIuCYCLES"\n", j,
                   timestamps[j].time1 - bench_tscoverhead() -
                   timestamps[j].time0);
        }

        printf("client done\n");
        return;
    }

    if (!flag) { // Start experiment
        timestamps[i].time0 = bench_tsc();
        flag = true;
    } else { // Continue experiment
        timestamps[i].time1 = bench_tsc();
        timestamps[i+1].time0 = timestamps[i].time1;
        i++;
    }

    switch(benchmark_type) {
    case 0:
        err = binding->tx_vtbl.fsb_payload_request(binding, NOP_CONT, 1, 2, 3, 4);
        break;

    case 1:
        err = binding->tx_vtbl.fsb_empty_request(binding, NOP_CONT);
        break;

    case 2:
        err = binding->tx_vtbl.fsb_payload1_request(binding, NOP_CONT, 1);
        break;

    case 3:
        err = binding->tx_vtbl.fsb_payload2_request(binding, NOP_CONT, 1, 2);
        break;

    case 4:
        err = binding->tx_vtbl.fsb_payload8_request(binding, NOP_CONT, 1, 2, 3, 4, 5, 6, 7, 8);
        break;

    case 5:
        err = binding->tx_vtbl.fsb_payload16_request(binding, NOP_CONT, 1, 2, 3, 4, 5, 6, 7, 8,
                                                9, 10, 11, 12, 13, 14, 15, 16);
        break;

    default:
        printf("unknown benchmark type\n");
        abort();
        break;
    }

    assert(err_is_ok(err));
}

static void fsb_init_msg(struct bench_binding *b, coreid_t id)
{
    binding = b;
    printf("Running flounder_stubs_payload between core %d and core %d\n", my_core_id, 1);
    experiment();
}

static void fsb_payload_reply(struct bench_binding *b,
                              int payload0, int payload1,
                              int payload2, int payload3)
{
    experiment();
}

static void fsb_empty_reply(struct bench_binding *b)
{
    experiment();
}

static void fsb_payload1_reply(struct bench_binding *b,
                               int p0)
{
    experiment();
}

static void fsb_payload2_reply(struct bench_binding *b,
                               int p0, int p1)
{
    experiment();
}

static void fsb_payload8_reply(struct bench_binding *b,
                               int p0, int p1, int p2, int p3,
                               int p4, int p5, int p6, int p7)
{
    experiment();
}

static void fsb_payload16_reply(struct bench_binding *b,
                                int p0, int p1, int p2, int p3,
                                int p4, int p5, int p6, int p7,
                                int p8, int p9, int p10, int p11,
                                int p12, int p13, int p14, int p15)
{
    experiment();
}

static void fsb_payload_request(struct bench_binding *b,
                              int payload0, int payload1,
                              int payload2, int payload3)
{
    errval_t err;
    err = b->tx_vtbl.fsb_payload_reply(b, NOP_CONT, 1, 2, 3, 4);
    assert(err_is_ok(err));
}

static void fsb_empty_request(struct bench_binding *b)
{
    errval_t err;
    err = b->tx_vtbl.fsb_empty_reply(b, NOP_CONT);
    assert(err_is_ok(err));
}

static void fsb_payload1_request(struct bench_binding *b,
                              int payload0)
{
    errval_t err;
    err = b->tx_vtbl.fsb_payload1_reply(b, NOP_CONT, 1);
    assert(err_is_ok(err));
}

static void fsb_payload2_request(struct bench_binding *b,
                                 int payload0, int payload1)
{
    errval_t err;
    err = b->tx_vtbl.fsb_payload2_reply(b, NOP_CONT, 1, 2);
    assert(err_is_ok(err));
}

static void fsb_payload8_request(struct bench_binding *b,
                                 int payload0, int payload1,
                                 int payload2, int payload3,
                                 int payload4, int payload5,
                                 int payload6, int payload7)
{
    errval_t err;
    err = b->tx_vtbl.fsb_payload8_reply(b, NOP_CONT, 1, 2, 3, 4, 5, 6, 7, 8);
    assert(err_is_ok(err));
}

static void fsb_payload16_request(struct bench_binding *b,
                                  int payload0, int payload1,
                                  int payload2, int payload3,
                                  int payload4, int payload5,
                                  int payload6, int payload7,
                                  int payload8, int payload9,
                                  int payload10, int payload11,
                                  int payload12, int payload13,
                                  int payload14, int payload15)
{
    errval_t err;
    err = b->tx_vtbl.fsb_payload16_reply(b, NOP_CONT, 1, 2, 3, 4, 5, 6, 7, 8,
                                            9, 10, 11, 12, 13, 14, 15, 16);
    assert(err_is_ok(err));
}

static struct bench_rx_vtbl rx_vtbl = {
    .fsb_init_msg   = fsb_init_msg,
    .fsb_payload_request = fsb_payload_request,
    .fsb_payload_reply = fsb_payload_reply,
    .fsb_empty_request = fsb_empty_request,
    .fsb_payload1_request = fsb_payload1_request,
    .fsb_payload2_request = fsb_payload2_request,
    .fsb_payload8_request = fsb_payload8_request,
    .fsb_payload16_request = fsb_payload16_request,
    .fsb_empty_reply = fsb_empty_reply,
    .fsb_payload1_reply = fsb_payload1_reply,
    .fsb_payload2_reply = fsb_payload2_reply,
    .fsb_payload8_reply = fsb_payload8_reply,
    .fsb_payload16_reply = fsb_payload16_reply,
};

static void bind_cb(void *st, errval_t binderr, struct bench_binding *b)
{
    // copy my message receive handler vtable to the binding
    b->rx_vtbl = rx_vtbl;

    errval_t err;
    err = b->tx_vtbl.fsb_init_msg(b, NOP_CONT, my_core_id);
    assert(err_is_ok(err));
}

static void export_cb(void *st, errval_t err, iref_t iref)
{
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "export failed");
        abort();
    }

    // register this iref with the name service
    err = nameservice_register("fsb server", iref);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "nameservice_register failed");
        abort();
    }
}

static errval_t connect_cb(void *st, struct bench_binding *b)
{
    // copy my message receive handler vtable to the binding
    b->rx_vtbl = rx_vtbl;

    // accept the connection
    return SYS_ERR_OK;
}

int main(int argc, char *argv[])
{
    errval_t err;

    /* Set my core id */
    my_core_id = disp_get_core_id();
    strcpy(my_name, argv[0]);

    bench_init();

    // Default first arg
    if(argc == 1) {
        argc = 2;
        argv[1] = "0";
    }

    if (argc == 2) { /* bsp core */
        benchmark_type = atoi(argv[1]);

        /*
          1. spawn domain,
          2. setup a server,
          3. wait for client to connect,
          4. run experiment
        */
        char *xargv[] = {my_name, "dummy", "dummy", NULL};
        err = spawn_program(1, my_name, xargv, NULL,
                            SPAWN_FLAGS_DEFAULT, NULL);
        assert(err_is_ok(err));

        /* Setup a server */
        err = bench_export(NULL, export_cb, connect_cb, get_default_waitset(),
                           IDC_BIND_FLAGS_DEFAULT);
        assert(err_is_ok(err));
    } else {
        /* Connect to the server */
        iref_t iref;

        err = nameservice_blocking_lookup("fsb server", &iref);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "nameservice_blocking_lookup failed");
            abort();
        }

        err = bench_bind(iref, bind_cb, NULL,
                         get_default_waitset(), IDC_BIND_FLAGS_DEFAULT);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "bind failed");
            abort();
        }
    }

    messages_handler_loop();
}
