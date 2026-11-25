// Minimal Modbus RTU slave (spec S11.1). See modbus_rtu.h. No Arduino dependencies.
#include "modbus_rtu.h"
#include <string.h>

uint16_t mb_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

void mb_init(mb_slave_t *s, uint8_t slave_id, uint16_t *regs, mb_write_cb_t on_write, void *ctx) {
    memset(s, 0, sizeof *s);
    s->slave_id = slave_id;
    s->regs = regs;
    s->on_write = on_write;
    s->ctx = ctx;
}

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// True if the last two bytes of buf[0..n) are the CRC of the preceding bytes (low byte first).
static bool crc_ok(const uint8_t *buf, size_t n) {
    uint16_t c = mb_crc16(buf, n - 2);
    return buf[n - 2] == (uint8_t)(c & 0xFF) && buf[n - 1] == (uint8_t)(c >> 8);
}

// Appends the CRC to s->tx[0..n) and returns the total response length.
static size_t finish(mb_slave_t *s, size_t n) {
    uint16_t crc = mb_crc16(s->tx, n);
    s->tx[n] = (uint8_t)(crc & 0xFF);
    s->tx[n + 1] = (uint8_t)(crc >> 8);
    return n + 2;
}

static size_t exception(mb_slave_t *s, uint8_t code) {
    if (s->rx[0] == MODBUS_BROADCAST_ID) {
        return 0;
    }
    s->tx[0] = s->slave_id;
    s->tx[1] = (uint8_t)(s->rx[1] | 0x80);
    s->tx[2] = code;
    return finish(s, 3);
}

static void store(mb_slave_t *s, uint16_t addr, uint16_t value) {
    s->regs[addr] = value;
    if (s->on_write) {
        s->on_write(s->ctx, addr, value);
    }
}

// Executes one complete, CRC-checked frame in s->rx[0..len).
static size_t process(mb_slave_t *s, size_t len) {
    (void)len;
    const uint8_t *f = s->rx;
    if (f[0] != s->slave_id && f[0] != MODBUS_BROADCAST_ID) {
        return 0;
    }
    const bool broadcast = (f[0] == MODBUS_BROADCAST_ID);

    switch (f[1]) {
    case MODBUS_FC_READ_HOLDING: {
        if (broadcast) {
            return 0;
        }
        uint16_t addr = be16(f + 2);
        uint16_t qty = be16(f + 4);
        if (qty < 1 || qty > MODBUS_MAX_READ_QTY) {
            return exception(s, MODBUS_EX_ILLEGAL_VALUE);
        }
        if ((uint32_t)addr + qty > MODBUS_REG_COUNT) {
            return exception(s, MODBUS_EX_ILLEGAL_ADDRESS);
        }
        s->tx[0] = s->slave_id;
        s->tx[1] = f[1];
        s->tx[2] = (uint8_t)(qty * 2);
        for (uint16_t i = 0; i < qty; i++) {
            put_be16(&s->tx[3 + 2 * i], s->regs[addr + i]);
        }
        return finish(s, 3 + 2 * (size_t)qty);
    }
    case MODBUS_FC_WRITE_SINGLE: {
        uint16_t addr = be16(f + 2);
        uint16_t value = be16(f + 4);
        if (addr >= MODBUS_REG_COUNT) {
            return exception(s, MODBUS_EX_ILLEGAL_ADDRESS);
        }
        store(s, addr, value);
        if (broadcast) {
            return 0;
        }
        memcpy(s->tx, f, 6);          // the reply is an echo of the request
        return finish(s, 6);
    }
    case MODBUS_FC_WRITE_MULTI: {
        uint16_t addr = be16(f + 2);
        uint16_t qty = be16(f + 4);
        uint8_t bytes = f[6];
        if (qty < 1 || qty > MODBUS_MAX_WRITE_QTY || bytes != 2 * qty) {
            return exception(s, MODBUS_EX_ILLEGAL_VALUE);
        }
        if ((uint32_t)addr + qty > MODBUS_REG_COUNT) {
            return exception(s, MODBUS_EX_ILLEGAL_ADDRESS);
        }
        for (uint16_t i = 0; i < qty; i++) {
            store(s, (uint16_t)(addr + i), be16(f + 7 + 2 * i));
        }
        if (broadcast) {
            return 0;
        }
        s->tx[0] = s->slave_id;
        s->tx[1] = f[1];
        put_be16(&s->tx[2], addr);
        put_be16(&s->tx[4], qty);
        return finish(s, 6);
    }
    default:
        return exception(s, MODBUS_EX_ILLEGAL_FUNCTION);
    }
}

size_t mb_feed(mb_slave_t *s, uint8_t byte, uint32_t now_us) {
    if (s->rx_len > 0 && (uint32_t)(now_us - s->last_us) > MODBUS_FRAME_TIMEOUT_US) {
        s->rx_len = 0;                // partial frame went silent: start over
    }
    s->last_us = now_us;
    if (s->rx_len >= MODBUS_MAX_FRAME) {
        s->rx_len = 0;                // cannot be a valid frame
    }
    s->rx[s->rx_len++] = byte;
    const size_t n = s->rx_len;

    // Expected frame length; 0 = header not complete yet.
    size_t need = 0;
    if (n >= 2) {
        const uint8_t fc = s->rx[1];
        if (fc == MODBUS_FC_READ_HOLDING || fc == MODBUS_FC_WRITE_SINGLE) {
            need = 8;                 // id, fc, addr(2), value/qty(2), crc(2)
        } else if (fc == MODBUS_FC_WRITE_MULTI) {
            if (n >= 7) {
                need = 9 + (size_t)s->rx[6];   // id, fc, addr(2), qty(2), bytecount, data, crc(2)
            }
        } else if (n >= 4 && crc_ok(s->rx, n)) {
            need = n;                 // unknown function: the frame ends where its CRC matches
        }
    }
    if (need > MODBUS_MAX_FRAME) {
        s->rx_len = 0;
        return 0;
    }
    if (need == 0 || n < need) {
        return 0;
    }

    s->rx_len = 0;
    if (!crc_ok(s->rx, n)) {
        return 0;
    }
    return process(s, n);
}
