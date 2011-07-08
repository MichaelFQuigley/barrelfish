/*
 * Copyright (c) 2007, 2008, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef WEBSERVER_DEBUG_H_
#define WEBSERVER_DEBUG_H_


/*****************************************************************
 * Debug printer:
 *****************************************************************/
//#define WEBSERVER_DEBUG 1
#if defined(WEBSERVER_DEBUG) || defined(GLOBAL_DEBUG)
#define SERVER_DEBUG(x...) printf("webserver: " x)
#else
#define SERVER_DEBUG(x...) ((void)0)
#endif


#endif // WEBSERVER_H_
