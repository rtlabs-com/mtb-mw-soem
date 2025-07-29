/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#ifndef OSHW_XMC72_H
#define OSHW_XMC72_H

int oshw_mac_init(const uint8_t *mac_address);
int oshw_mac_send(const void *payload, size_t tot_len);
int oshw_mac_recv(void *buffer, size_t buffer_length);

#endif // OSHW_XMC72_H
