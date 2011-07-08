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

#define MAX_COUNT 100

struct timestamp {
    cycles_t time0;
    cycles_t time1;
};
static struct timestamp timestamps[MAX_COUNT];

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
#ifdef __BEEHIVE__
    if (i==20) __asm volatile ("simctrl 2");
    __asm volatile("simctrl 8");
    if (i==21) __asm volatile ("simctrl 3");
#endif
    err = binding->tx_vtbl.fsb_empty_request(binding, NOP_CONT);
    assert(err_is_ok(err));
}

static void fsb_init_msg(struct bench_binding *b, coreid_t id)
{
    binding = b;
    printf("Running flounder_stubs_empty between core %d and core %d\n", id, my_core_id);

#ifdef __BEEHIVE__
    // XXX Hijack the bmp binding
    errval_t err;

    
    struct bench_bmp_binding *bmp = (struct bench_bmp_binding *)b;
    struct flounder_bmp_state *s = &(bmp->bmp_state);

    bmp_chan_deregister_recv(&s->chan);

    while (true) {
        // try to retrieve a message from the channel
	struct bmp_recv_msg msg = BMP_RECV_MSG_INIT;
    retry:
        sys_print("R", 1);
        err = bmp_chan_recv(&s->chan, &msg);
        if (err_no(err) == LIB_ERR_NO_LMP_MSG) {
            //messages_wait_and_handle_next();
            thread_yield();
            goto retry;
        }
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "bmp_chan_recv");
            // real error
        }
        sys_print("M", 1);

        sys_print("S", 1);
        err = bmp_chan_send(&s->chan,(uintptr_t *) &msg, 1);
        assert(!err_is_fail(err));
    }
#endif

    experiment();
}

static void fsb_empty_reply(struct bench_binding *b)
{
    experiment();
}

static void fsb_empty_request(struct bench_binding *b)
{
    errval_t err;
    err = b->tx_vtbl.fsb_empty_reply(b, NOP_CONT);
    assert(err_is_ok(err));
#ifdef __BEEHIVE__
    __asm volatile("simctrl 8");
#endif
}

static struct bench_rx_vtbl rx_vtbl = {
    .fsb_init_msg   = fsb_init_msg,
    .fsb_empty_request = fsb_empty_request,
    .fsb_empty_reply = fsb_empty_reply,
};

static void bind_cb(void *st, errval_t binderr, struct bench_binding *b)
{
    // copy my message receive handler vtable to the binding
    b->rx_vtbl = rx_vtbl;

#ifdef __BEEHIVE__
    // XXX Hijack the binding for BMP tests
    struct bench_bmp_binding *bmp = (struct bench_bmp_binding *)b;
    struct flounder_bmp_state *s = &(bmp->bmp_state);

    bmp_chan_deregister_recv(&s->chan);
#endif

    // Send an init message
    errval_t err;
    err = b->tx_vtbl.fsb_init_msg(b, NOP_CONT, my_core_id);
    assert(err_is_ok(err));

#ifdef __BEEHIVE__
    while(1) {
        sys_print("s",1);
	struct bmp_recv_msg sendmsg = BMP_RECV_MSG_INIT;
        err = bmp_chan_send(&s->chan, (uintptr_t *)&sendmsg, 1);
        assert(!err_is_fail(err));
    retry:
        sys_print("r",1);
	struct bmp_recv_msg msg = BMP_RECV_MSG_INIT;
        err = bmp_chan_recv(&s->chan, &msg);
        if (err_no(err) == LIB_ERR_NO_LMP_MSG) {
            //messages_wait_and_handle_next();
            thread_yield();
            goto retry;
        }
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "bmp_chan_recv");
            // real error
        }
        sys_print("m", 1);
        // process control word
        flounder_stub_bmp_process_header(&(bmp->bmp_state), (msg.words)[0], (msg.buf).msglen);
    }
#endif
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

    printf("entered\n");
    bench_init();
    printf("bench_init done\n");

    if (argc == 1) { /* server */

#ifndef __BEEHIVE__
        /*
          1. spawn domain,
          2. setup a server,
          3. wait for client to connect,
          4. run experiment
        */
        char *xargv[] = {my_name, "dummy", NULL};
        printf("spawning...\n");
        err = spawn_program(1, my_name, xargv, NULL,
                            SPAWN_FLAGS_DEFAULT, NULL);
        assert(err_is_ok(err));
#endif
        /* Setup a server */
        err = bench_export(NULL, export_cb, connect_cb, get_default_waitset(),
                           IDC_BIND_FLAGS_DEFAULT);
        assert(err_is_ok(err));
    } else {
        /* Connect to the server */
        iref_t iref;
        printf("ns lookup\n");
        err = nameservice_blocking_lookup("fsb server", &iref);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "nameservice_blocking_lookup failed");
            abort();
        }
        printf("bench_bind\n");
        err = bench_bind(iref, bind_cb, NULL,
                         get_default_waitset(), IDC_BIND_FLAGS_DEFAULT);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "bind failed");
            abort();
        }
    }
    printf("message_loop\n");
    messages_handler_loop();
}
