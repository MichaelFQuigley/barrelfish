/**
 * \file
 * \brief Client for interacting with the spawn daemon on each core
 */

/*
 * Copyright (c) 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <barrelfish/barrelfish.h>
#include <barrelfish/nameservice_client.h>
#include <barrelfish/spawn_client.h>
#include <barrelfish/cpu_arch.h>
#include <if/spawn_rpcclient_defs.h>
#include <vfs/vfs_path.h>

extern char **environ;

struct spawn_bind_retst {
    errval_t err;
    struct spawn_binding *b;
    bool present;
};

static void spawn_bind_cont(void *st, errval_t err, struct spawn_binding *b)
{
    struct spawn_bind_retst *retst = st;
    assert(retst != NULL);
    assert(!retst->present);
    retst->err = err;
    retst->b = b;
    retst->present = true;
}

/* XXX: utility function that doesn't really belong here */
const char *cpu_type_to_archstr(enum cpu_type cpu_type)
{
    STATIC_ASSERT(CPU_TYPE_NUM == 5, "knowledge of all CPU types here");
    switch(cpu_type) {
    case CPU_X86_64:    return "x86_64";
    case CPU_X86_32:    return "x86_32";
    case CPU_BEEHIVE:   return "beehive";
    case CPU_SCC:       return "scc";
    case CPU_ARM:       return "arm";
    default:            USER_PANIC("cpu_type_to_pathstr: %d unknown", cpu_type);
    }
}

static struct spawn_binding *spawn_b = NULL;

static errval_t bind_client(coreid_t coreid)
{
    struct spawn_rpc_client *cl;
    errval_t err = SYS_ERR_OK;

    // do we have a spawn client connection for this core?
    assert(coreid < MAX_CPUS);
    cl = get_spawn_rpc_client(coreid);
    if (cl == NULL) {
        char namebuf[16];
        snprintf(namebuf, sizeof(namebuf), "spawn.%u", coreid);
        namebuf[sizeof(namebuf) - 1] = '\0';

        iref_t iref;
        err = nameservice_lookup(namebuf, &iref);
        if (err_is_fail(err)) {
            //DEBUG_ERR(err, "spawn daemon on core %u not found\n", coreid);
            return err;
        }

       // initiate bind
        struct spawn_bind_retst bindst = { .present = false };
        err = spawn_bind(iref, spawn_bind_cont, &bindst, get_default_waitset(),
                         IDC_BIND_FLAGS_DEFAULT);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "spawn_bind failed");
            return err;
        }

        // XXX: block for bind completion
        while (!bindst.present) {
            messages_wait_and_handle_next();
        }

        spawn_b = bindst.b;

        cl = malloc(sizeof(struct spawn_rpc_client));
        if (cl == NULL) {
            return err_push(err, LIB_ERR_MALLOC_FAIL);
        }

        err = spawn_rpc_client_init(cl, bindst.b);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "spawn_rpc_client_init failed");
            return err;
       }

        set_spawn_rpc_client(coreid, cl);
    }

    return err;
}


/**
 * \brief Request the spawn daemon on a specific core to spawn a program
 *
 * \param coreid Core ID on which to spawn the program
 * \param path   Absolute path in the file system to an executable image
 *                        suitable for the given core
 * \param argv   Command-line arguments, NULL-terminated
 * \param envp   Optional environment, NULL-terminated (pass NULL to inherit)
 * \param flags  Flags to spawn
 * \param ret_domainid If non-NULL, filled in with domain ID of program
 *
 * \bug flags are currently ignored
 */
errval_t spawn_program(coreid_t coreid, const char *path,
                       char *const argv[], char *const envp[],
                       spawn_flags_t flags, domainid_t *ret_domainid)
{
    struct spawn_rpc_client *cl;
    errval_t err, msgerr;

    // default to copying our environment
    if (envp == NULL) {
        envp = environ;
    }

    // do we have a spawn client connection for this core?
    assert(coreid < MAX_CPUS);
    cl = get_spawn_rpc_client(coreid);
    if (cl == NULL) {
        char namebuf[16];
        snprintf(namebuf, sizeof(namebuf), "spawn.%u", coreid);
        namebuf[sizeof(namebuf) - 1] = '\0';

        iref_t iref;
        err = nameservice_lookup(namebuf, &iref);
        if (err_is_fail(err)) {
            //DEBUG_ERR(err, "spawn daemon on core %u not found\n", coreid);
            return err;
        }

        // initiate bind
        struct spawn_bind_retst bindst = { .present = false };
        err = spawn_bind(iref, spawn_bind_cont, &bindst, get_default_waitset(),
                         IDC_BIND_FLAGS_DEFAULT);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "spawn_bind failed");
        }

        // XXX: block for bind completion
        while (!bindst.present) {
            messages_wait_and_handle_next();
        }

        if(err_is_fail(bindst.err)) {
            USER_PANIC_ERR(bindst.err, "asynchronous error during spawn_bind");
        }
        assert(bindst.b != NULL);

        cl = malloc(sizeof(struct spawn_rpc_client));
        assert(cl != NULL);

        err = spawn_rpc_client_init(cl, bindst.b);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "spawn_rpc_client_init failed");
        }

        set_spawn_rpc_client(coreid, cl);
    }

    // construct argument "string"
    // \0-separated strings in contiguous character buffer
    // this is needed, as flounder can't send variable-length arrays of strings
    size_t argstrlen = 0;
    for (int i = 0; argv[i] != NULL; i++) {
        argstrlen += strlen(argv[i]) + 1;
    }

    char argstr[argstrlen];
    size_t argstrpos = 0;
    for (int i = 0; argv[i] != NULL; i++) {
        strcpy(&argstr[argstrpos], argv[i]);
        argstrpos += strlen(argv[i]);
        argstr[argstrpos++] = '\0';
    }
    assert(argstrpos == argstrlen);

    // repeat for environment
    size_t envstrlen = 0;
    for (int i = 0; envp[i] != NULL; i++) {
        envstrlen += strlen(envp[i]) + 1;
    }

    char envstr[envstrlen];
    size_t envstrpos = 0;
    for (int i = 0; envp[i] != NULL; i++) {
        strcpy(&envstr[envstrpos], envp[i]);
        envstrpos += strlen(envp[i]);
        envstr[envstrpos++] = '\0';
    }
    assert(envstrpos == envstrlen);


    domainid_t domain_id;

    // make an unqualified path absolute using the $PATH variable
    // TODO: implement search (currently assumes PATH is a single directory)
    char *searchpath = getenv("PATH");
    if (searchpath == NULL) {
        searchpath = VFS_PATH_SEP_STR; // XXX: just put it in the root
    }
    size_t buflen = strlen(path) + strlen(searchpath) + 2;
    char pathbuf[buflen];
    if (path[0] != VFS_PATH_SEP) {
        snprintf(pathbuf, buflen, "%s%c%s", searchpath, VFS_PATH_SEP, path);
        pathbuf[buflen - 1] = '\0';
        //vfs_path_normalise(pathbuf);
        path = pathbuf;
    }

    err = cl->vtbl.spawn_domain(cl, path, argstr, argstrlen, envstr, envstrlen,
                                &msgerr, &domain_id);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "error sending spawn request");
    } else if (err_is_fail(msgerr)) {
        return msgerr;
    }

    if (ret_domainid != NULL) {
        *ret_domainid = domain_id;
    }

    return msgerr;
}

/**
 * \brief Request a program be spawned on all cores in the system
 *
 * \param same_core Iff false, don't spawn on the same core as the caller
 * \param path   Absolute path in the file system to an executable image
 *                        suitable for the given core
 * \param argv   Command-line arguments, NULL-terminated
 * \param envp   Optional environment, NULL-terminated (pass NULL to inherit)
 * \param flags  Flags to spawn
 * \param ret_domainid If non-NULL, filled in with domain ID of program
 *
 * \note This function is for legacy compatibility with existing benchmark/test
 *    code, and SHOULD NOT BE USED IN NEW CODE UNLESS YOU HAVE A GOOD REASON!
 *    It doesn't make much sense from a scalability perspective, and is
 *    probably useless on a heterogeneous system.
 */
errval_t spawn_program_on_all_cores(bool same_core, const char *path,
                                    char *const argv[], char *const envp[],
                                    spawn_flags_t flags, domainid_t *ret_domainid)
{
    errval_t err;

    // TODO: handle flags, domain ID

    // FIXME: world's most broken implementation...
    for (coreid_t c = 0; c < MAX_CPUS; c++) {
        if (!same_core && c == disp_get_core_id()) {
            continue;
        }

        err = spawn_program(c, path, argv, envp, flags, NULL);
        if (err_is_fail(err)) {
            if (err_no(err) == CHIPS_ERR_UNKNOWN_NAME) {
                continue; // no spawn daemon on this core
            } else {
                DEBUG_ERR(err, "error spawning %s on core %u\n", path, c);
                return err;
            }
        }
    }

    return SYS_ERR_OK;
}

/**
 * \brief Request a spawnd to reconnect to a local memserv
 *
 * \param coreid The core that the spawnd is running on
 */
errval_t spawn_set_local_memserv(coreid_t coreid)
{
    struct spawn_rpc_client *cl;
    errval_t err;

    // do we have a spawn client connection for this core?
    err = bind_client(coreid);
    if (err_is_fail(err)) {
        return err;
    }
    cl = get_spawn_rpc_client(coreid);

    //    err = spawn_b->tx_vtbl.use_local_memserv(spawn_b, NOP_CONT);
    err = cl->vtbl.use_local_memserv(cl);

    return err;
}
