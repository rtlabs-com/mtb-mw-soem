/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#include "osal.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef COMPONENT_CAT1
#include "cy_ethif.h"
#endif

#include "network.h"

/* Header file includes */
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

/* LWIP */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/lwip_hooks.h"
#include "netif/ethernet.h"

/* Ethernet connection manager header files */
#include "cy_ecm.h"
#include "cy_ecm_error.h"
#include "cy_eth_phy_driver.h"

#include "msg_q.h"

#include "oshw.h"
#include "oshw_xmc72.h"

static cy_ecm_t ecm_handle = NULL;
static uint8_t scratch[1536];

/* Ethernet interface ID */
#ifdef XMC7100D_F176K4160
#define INTERFACE_ID CY_ECM_INTERFACE_ETH0
#else
#define INTERFACE_ID CY_ECM_INTERFACE_ETH1
#endif

static cy_ecm_phy_callbacks_t phy_callbacks = {
    .phy_init = cy_eth_phy_init,
    .phy_configure = cy_eth_phy_configure,
    .phy_enable_ext_reg = cy_eth_phy_enable_ext_reg,
    .phy_discover = cy_eth_phy_discover,
    .phy_get_auto_neg_status = cy_eth_phy_get_auto_neg_status,
    .phy_get_link_partner_cap = cy_eth_phy_get_link_partner_cap,
    .phy_get_linkspeed = cy_eth_phy_get_linkspeed,
    .phy_get_linkstatus = cy_eth_phy_get_linkstatus,
    .phy_reset = cy_eth_phy_reset};

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Size the message buffer to hold a few worst-case Ethernet frames plus headers.
   Adjust to your expected burst: N * (max_frame + 4). */
#ifndef MSGQ_CAPACITY_BYTES
#define MSGQ_CAPACITY_BYTES (8 * 1536) /* ~8 full-size frames */
#endif

static bool init_done = false;
static msg_queue_t g_msgq;

/* LWIP unknown-eth hook: enqueue the raw frame for later processing.
   We return ERR_IF so LWIP frees the pbuf after we copy its payload. */
static err_t eth_recv(struct pbuf *p_buf, struct netif *netif)
{
   (void)netif;

   if ((p_buf == NULL) || (p_buf->tot_len == 0))
   {
      return ERR_IF;
   }

   /* Gather contiguously if pbuf is chained. */
   uint8_t local_copy[1536];
   const u16_t to_copy = (u16_t)MIN(sizeof(local_copy), p_buf->tot_len);

   if (pbuf_copy_partial(p_buf, local_copy, to_copy, 0) != to_copy)
   {
      return ERR_IF;
   }

   BaseType_t xWoken = pdFALSE;
   (void)msg_queue_send_from_isr(&g_msgq, local_copy, to_copy, &xWoken);
   portYIELD_FROM_ISR(xWoken);

   /* Tell LWIP we didn't take ownership of p_buf; it should free it. */
   return ERR_IF;
}

int oshw_mac_init(const uint8_t *mac_address)
{
   cy_rslt_t result = CY_RSLT_SUCCESS;
   (void)mac_address; /* Not used here, kept for signature compatibility */

   if (init_done)
      return 0;

   /* Create the message buffer-backed queue */
   if (!msg_queue_init(&g_msgq, MSGQ_CAPACITY_BYTES))
   {
      printf("Message queue init failed\n");
      return -1;
   }

   /* enable in msg_q.h to characterize system in case of congestion problems */
#ifdef ENABLE_MSG_Q_LOGGER
   msg_queue_start_logger(&g_msgq, 3000, OS_PRIORITY_NORMAL);
#endif
   /* Initialize ethernet connection manager. */
   result = cy_ecm_init();

   if (result != CY_RSLT_SUCCESS)
   {
      printf("Ethernet connection manager initialization failed! Error code: "
             "0x%08" PRIx32 "\n",
             (uint32_t)result);
      CY_ASSERT(0);
   }
   else
   {
      printf("Ethernet connection manager initialized.\n");
   }

   /* Initialize the Ethernet Interface and PHY driver */
   result = cy_ecm_ethif_init(INTERFACE_ID, &phy_callbacks, &ecm_handle);
   if (result != CY_RSLT_SUCCESS)
   {
      printf(
          "Ethernet interface initialization failed! Error code: 0x%08" PRIx32
          "\n",
          (uint32_t)result);

      CY_ASSERT(0);
   }

   /* setup dummy address just to initialize lwip and default netif interface */
   cy_ecm_ip_setting_t static_ip_addr;
   cy_ecm_ip_address_t ip_addr;

   static_ip_addr.ip_address.version = CY_ECM_IP_VER_V4;
   static_ip_addr.ip_address.ip.v4 = APP_STATIC_IP_ADDR;
   static_ip_addr.gateway.version = CY_ECM_IP_VER_V4;
   static_ip_addr.gateway.ip.v4 = APP_STATIC_GATEWAY;
   static_ip_addr.netmask.version = CY_ECM_IP_VER_V4;
   static_ip_addr.netmask.ip.v4 = APP_NETMASK;

   result = cy_ecm_connect(ecm_handle, &static_ip_addr, &ip_addr);

   if (result == CY_RSLT_SUCCESS)
   {
      printf("SUCCESS\n");
   }
   else
   {
      printf("FAIL\n");
      return -1;
   }

   /* Use LWIP unknown hook instead of directly interfacing with ethernet driver
    * due to dependencies between ethernet connection manager component and lwip */
   lwip_set_hook_for_unknown_eth_protocol(netif_default, eth_recv);

   cy_ecm_set_promiscuous_mode(ecm_handle, true);
   cy_ecm_broadcast_disable(ecm_handle, false);

   init_done = true;

   /* Workaround failure to send frames immediately after establishing
      link seen on some boards */
   os_tick_sleep (20);

   return 0;
}

int oshw_mac_send(const void *payload, size_t tot_len)
{
   struct pbuf *p;

   p = pbuf_alloc(PBUF_RAW, tot_len, PBUF_POOL);

   if (p != NULL)
   {
      memcpy(p->payload, payload, tot_len);

      /* Send frame using lwIP */
      LOCK_TCPIP_CORE();
      err_t result = netif_default->linkoutput(netif_default, p);
      UNLOCK_TCPIP_CORE();

      if (result != ERR_OK)
      {
         printf("Error sending Ethernet frame\n");
      }

      pbuf_free(p);
   }

   return (int)tot_len;
}
int oshw_mac_recv(void *buffer, size_t buffer_length)
{
   if (!init_done || buffer == NULL || buffer_length == 0)
   {
      return 0;
   }

   /* Non-blocking pull into a scratch buffer (max Ethernet frame). */
   size_t n = msg_queue_receive(&g_msgq, scratch, sizeof(scratch), 0);
   if (n == 0)
   {
      /* No complete message available right now. */
      return 0;
   }

   /* Truncate copy if caller's buffer is smaller; return actual frame length. */
   size_t to_copy = (n < buffer_length) ? n : buffer_length;
   memcpy(buffer, scratch, to_copy);
   return (int)n;
}
