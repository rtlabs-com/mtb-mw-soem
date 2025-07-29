/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#include "oshw.h"
#include "oshw_xmc72.h"
#include <stdlib.h>

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define PP_HTONS(x) (x)
#define PP_NTOHS(x) (x)
#else /* BYTE_ORDER != BIG_ENDIAN */
/* These macros should be calculated by the preprocessor and are used
   with compile-time constants only (so that there is no little-endian
   overhead at runtime). */
#define PP_HTONS(x) __builtin_bswap16(x)
#define PP_NTOHS(x) __builtin_bswap16(x)
#endif /* BYTE_ORDER == BIG_ENDIAN */

/**
 * Host to Network byte order (i.e. to big endian).
 *
 * Note that Ethercat uses little endian byte order, except for the Ethernet
 * header which is big endian as usual.
 */
uint16 oshw_htons(const uint16 host)
{
   uint16 network = (uint16)PP_HTONS(host);
   return network;
}

/**
 * Network (i.e. big endian) to Host byte order.
 *
 * Note that Ethercat uses little endian byte order, except for the Ethernet
 * header which is big endian as usual.
 */
uint16 oshw_ntohs(const uint16 network)
{
   uint16 host = (uint16)PP_NTOHS(network);
   return host;
}

static ec_adaptert default_adapter = {
    .name = "eth0",
    .desc = "default interface"};

/* Create list over available network adapters.
 * @return First element in linked list of adapters
 */
ec_adaptert *oshw_find_adapters(void)
{
   ec_adaptert *ret_adapter = NULL;

   ret_adapter = &default_adapter;

   return ret_adapter;
}

/** Free memory allocated memory used by adapter collection.
 * @param[in] adapter = First element in linked list of adapters
 * EC_NOFRAME.
 */
void oshw_free_adapters(ec_adaptert *adapter)
{
   /* TODO if needed */
}
