/**
 * \file
 * \brief Distributed (percore) memory server
 */

/*
 * Copyright (c) 2007-2011, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __MEM_SERV_H__
#define __MEM_SERV_H__

#ifndef MIN
#define MIN(a,b) ((a)<=(b)?(a):(b))
#endif

#define MEMSERV_DIST "mem_serv_dist"
#define NAME_LEN 20

#define MEMSERV_PERCORE_DYNAMIC
#define MEMSERV_AFFINITY

// FIXME: this should end up in trace.h
//#define TRACE_SUBSYS_PERCORE_MEMSERV (TRACE_SUBSYS_MEMSERV + 0xDDD)
#define TRACE_SUBSYS_PERCORE_MEMSERV 0xADDD
#define TRACE_EVENT_ALLOC_COMPLETE    0x0002
#define TRACE_EVENT_INIT    0x0000

// appropriate size type for available RAM
typedef genpaddr_t memsize_t;
#define PRIuMEMSIZE PRIuGENPADDR

/* parameters for size of supported RAM and thus required storage */

// XXX: Even though we could manage an arbitrary amount of RAM on any
// architecture, we use paddr_t as the type to represent region
// limits, which limits us its size.
#if defined(__x86_64__)
#       define MAXSIZEBITS     38    ///< Max size of memory in allocator
#elif defined(__i386__)
#       define MAXSIZEBITS     32
#elif defined(__BEEHIVE__)
/* XXX This is better if < 32! - but there were no compile time warnings! */
#       define MAXSIZEBITS     31
#elif defined(__arm__)
/* XXX This is better if < 32! - but there were no compile time warnings! */
#       define MAXSIZEBITS     31
#else
#       error Unknown architecture
#endif

#define MINALLOCBITS    17 /// HACK: Min bits to alloc from mem_serv for local
			  /// percore memory. leaves some over for other uses
#define MINSIZEBITS     OBJBITS_DISPATCHER ///< Min size of each allocation
#define MAXCHILDBITS    4               ///< Max branching of BTree nodes

#define LOCAL_MEMBITS     18     // amount of memory that we keep for 
				   // internal use in each local server
#define LOCAL_MEM ((genpaddr_t)1 << LOCAL_MEMBITS)



/// Maximum depth of the BTree, assuming only branching by two at each level
#define MAXDEPTH        (MAXSIZEBITS - MINSIZEBITS + 1)
/// Maximum number of BTree nodes
#define NNODES          ((1UL << MAXDEPTH) - 1)

/* Parameters for static per-core memserv */
#define PERCORE_BITS 27
#define PERCORE_MEM ((memsize_t)1<<PERCORE_BITS)  ///< How much memory per-core

// size of initial RAM cap to fill allocator
#define SMALLCAP_BITS 20


/**
 * \brief Size of CNodes to be created by slot allocator.
 *
 * Must satisfy both:
 *    #CNODE_BITS >= MAXCHILDBITS        (cnode enough for max branching factor)
 *    (1UL << #CNODE_BITS) ** 2 >= #NNODES  (total number of slots is enough)
 */
#define CNODE_BITS      12
#define NCNODES         (1UL << CNODE_BITS)     ///< Maximum number of CNodes

/// Watermark at which we must refill the slab allocator used for nodes
#define MINSPARENODES   (MAXDEPTH * 8) // XXX: FIXME: experimentally determined!

extern memsize_t mem_total;
extern memsize_t mem_avail;

/// MM per-core allocator instance data: B-tree to manage mem regions
extern struct mm mm_percore;

/// MM local allocator instance data: B-tree to manage mem regions
extern struct mm mm_local;


errval_t slab_refill(struct slab_alloc *slabs);

errval_t percore_free_handler_common(struct capref ramcap);
memsize_t mem_available_handler_common(void);
errval_t percore_alloc(struct capref *ret, uint8_t bits,
                       genpaddr_t minbase, genpaddr_t maxlimit);
errval_t percore_allocate_handler_common(uint8_t bits,
                                         genpaddr_t minbase, 
                                         genpaddr_t maxlimit,
                                         struct capref *retcap);

errval_t initialize_percore_mem_serv(coreid_t core, 
                                     coreid_t *cores, 
                                     int len_cores,
                                     memsize_t percore_mem);


errval_t percore_mem_serv(coreid_t core, coreid_t *cores, 
                          int len_cores, memsize_t ram);

int common_main(int argc, char **argv);

#endif
