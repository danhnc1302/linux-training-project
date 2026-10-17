#ifndef SGP_ADAPTER_H
#define SGP_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "protocol_adapter.h"

#define SGP_MAGIC       0x7EU
#define SGP_FRAME_SIZE  11U
#define SGP_ACK         0x06U
#define SGP_NAK         0x15U

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflect, no xorout */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

extern const protocol_adapter_t g_sgp_adapter;

#endif
