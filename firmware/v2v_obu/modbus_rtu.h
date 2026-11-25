#pragma once
// Minimal Modbus RTU slave (spec S11.1): FC 03, 06, 16 on holding registers. No Arduino dependencies,
// so it can be unit-tested on the PC (tests/host/test_modbus_rtu.cpp). Byte-driven: call mb_feed() for
// every received byte with a microsecond timestamp; it returns the number of response bytes in s->tx.
#include <stdint.h>
#include <stddef.h>
#include "config.h"

// Called after each holding register is written (ascending address order within one FC16 frame).
// The callback may change regs[addr] (the gateway uses this to clear reserved registers and to
// trigger commands). ctx is the pointer given to mb_init().
typedef void (*mb_write_cb_t)(void *ctx, uint16_t addr, uint16_t value);

typedef struct {
    uint8_t       slave_id;
    uint16_t     *regs;                       // MODBUS_REG_COUNT words, owned by the caller
    mb_write_cb_t on_write;                   // may be NULL
    void         *ctx;
    uint8_t       rx[MODBUS_MAX_FRAME];       // frame being assembled
    size_t        rx_len;
    uint32_t      last_us;                    // timestamp of the previous byte
    uint8_t       tx[MODBUS_MAX_FRAME];       // last response (valid when mb_feed returns > 0)
} mb_slave_t;

uint16_t mb_crc16(const uint8_t *data, size_t len);   // CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF)

void   mb_init(mb_slave_t *s, uint8_t slave_id, uint16_t *regs, mb_write_cb_t on_write, void *ctx);

// Feed one received byte. Returns the number of response bytes now in s->tx (0 = nothing to send).
// Frames for other slave IDs and frames with a bad CRC are dropped silently. Slave ID 0 (broadcast)
// executes FC 06/16 but never replies.
size_t mb_feed(mb_slave_t *s, uint8_t byte, uint32_t now_us);
