#include "test.h"
#include "modbus_rtu.h"
#include <cstring>

namespace {

struct Rig {
    uint16_t regs[MODBUS_REG_COUNT];
    mb_slave_t s;
    int cb_calls;
    uint16_t last_addr;
    uint16_t last_value;
    uint32_t t_us;
};

void rig_on_write(void *ctx, uint16_t addr, uint16_t value) {
    Rig *r = static_cast<Rig *>(ctx);
    r->cb_calls++;
    r->last_addr = addr;
    r->last_value = value;
}

void rig_init(Rig *r) {
    memset(r, 0, sizeof *r);
    mb_init(&r->s, MODBUS_SLAVE_ID, r->regs, rig_on_write, r);
}

// Feeds bytes back to back (same timestamp, as a USB burst would be); returns the last response length.
size_t feed(Rig *r, const uint8_t *b, size_t n, uint8_t *out) {
    size_t resp = 0;
    for (size_t i = 0; i < n; i++) {
        size_t k = mb_feed(&r->s, b[i], r->t_us);
        if (k) {
            resp = k;
            memcpy(out, r->s.tx, k);
        }
    }
    return resp;
}

// Appends the CRC (low byte first) to body[0..n) and returns the frame length.
size_t with_crc(const uint8_t *body, size_t n, uint8_t *out) {
    memcpy(out, body, n);
    uint16_t c = mb_crc16(body, n);
    out[n] = static_cast<uint8_t>(c & 0xFF);
    out[n + 1] = static_cast<uint8_t>(c >> 8);
    return n + 2;
}

bool crc_valid(const uint8_t *f, size_t n) {
    uint16_t c = mb_crc16(f, n - 2);
    return f[n - 2] == (c & 0xFF) && f[n - 1] == (c >> 8);
}

}  // namespace

TEST(modbus_crc_vector) {
    const uint8_t req[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t c = mb_crc16(req, 6);
    CHECK_EQ(c & 0xFF, 0xC5);     // sent low byte first
    CHECK_EQ(c >> 8, 0xCD);
}

TEST(modbus_fc03_read_response_bytes) {
    Rig r;
    rig_init(&r);
    r.regs[0] = 100;
    r.regs[1] = 0x1234;
    r.regs[9] = 0xFFFF;
    const uint8_t body[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    size_t n = with_crc(body, 6, req);
    size_t resp = feed(&r, req, n, out);
    CHECK_EQ(resp, 25);            // 3 header + 20 data + 2 CRC
    if (resp == 25) {
        CHECK_EQ(out[0], 0x01);
        CHECK_EQ(out[1], 0x03);
        CHECK_EQ(out[2], 20);
        CHECK_EQ(out[3], 0x00); CHECK_EQ(out[4], 0x64);   // reg 0 = 100
        CHECK_EQ(out[5], 0x12); CHECK_EQ(out[6], 0x34);   // reg 1
        CHECK_EQ(out[21], 0xFF); CHECK_EQ(out[22], 0xFF); // reg 9
        CHECK(crc_valid(out, resp));
    }
}

TEST(modbus_fc06_write_echoes_request) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x01, 0x06, 0x00, 0x05, 0x12, 0x34};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    size_t n = with_crc(body, 6, req);
    size_t resp = feed(&r, req, n, out);
    CHECK_EQ(resp, 8);
    if (resp == 8) CHECK(memcmp(out, req, 8) == 0);
    CHECK_EQ(r.regs[5], 0x1234);
    CHECK_EQ(r.cb_calls, 1);
    CHECK_EQ(r.last_addr, 5);
    CHECK_EQ(r.last_value, 0x1234);
}

TEST(modbus_fc16_write_multiple_response) {
    Rig r;
    rig_init(&r);
    const uint8_t body[11] = {0x01, 0x10, 0x00, 0x02, 0x00, 0x02, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t req[13], out[MODBUS_MAX_FRAME];
    size_t n = with_crc(body, 11, req);
    size_t resp = feed(&r, req, n, out);
    CHECK_EQ(resp, 8);
    if (resp == 8) {
        CHECK_EQ(out[0], 0x01); CHECK_EQ(out[1], 0x10);
        CHECK_EQ(out[2], 0x00); CHECK_EQ(out[3], 0x02);
        CHECK_EQ(out[4], 0x00); CHECK_EQ(out[5], 0x02);
        CHECK(crc_valid(out, resp));
    }
    CHECK_EQ(r.regs[2], 0xAABB);
    CHECK_EQ(r.regs[3], 0xCCDD);
    CHECK_EQ(r.cb_calls, 2);
}

TEST(modbus_exception_address_110) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x01, 0x03, 0x00, 0x6E, 0x00, 0x01};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    size_t resp = feed(&r, req, with_crc(body, 6, req), out);
    CHECK_EQ(resp, 5);
    if (resp == 5) {
        CHECK_EQ(out[0], 0x01); CHECK_EQ(out[1], 0x83); CHECK_EQ(out[2], 0x02);
        CHECK(crc_valid(out, resp));
    }
}

TEST(modbus_exception_quantity_rules) {
    Rig r;
    rig_init(&r);
    uint8_t req[16], out[MODBUS_MAX_FRAME];
    // FC03 quantity 0 -> illegal data value (03)
    const uint8_t q0[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
    size_t resp = feed(&r, req, with_crc(q0, 6, req), out);
    CHECK_EQ(resp, 5);
    if (resp == 5) CHECK_EQ(out[2], 0x03);
    // FC03 addr 105 qty 10 -> overflows 0-109 -> illegal address (02)
    const uint8_t ovf[6] = {0x01, 0x03, 0x00, 0x69, 0x00, 0x0A};
    resp = feed(&r, req, with_crc(ovf, 6, req), out);
    CHECK_EQ(resp, 5);
    if (resp == 5) CHECK_EQ(out[2], 0x02);
    // FC16 byte count mismatch (qty 2 but count 2) -> illegal data value (03)
    const uint8_t bc[10] = {0x01, 0x10, 0x00, 0x02, 0x00, 0x02, 0x02, 0x11, 0x22, 0x33};
    resp = feed(&r, req, with_crc(bc, 9, req), out);
    CHECK_EQ(resp, 5);
    if (resp == 5) CHECK_EQ(out[2], 0x03);
    // Unknown function code 0x07 -> illegal function (01)
    const uint8_t fc7[2] = {0x01, 0x07};
    resp = feed(&r, req, with_crc(fc7, 2, req), out);
    CHECK_EQ(resp, 5);
    if (resp == 5) {
        CHECK_EQ(out[1], 0x87); CHECK_EQ(out[2], 0x01);
    }
    CHECK_EQ(r.cb_calls, 0);
}

TEST(modbus_wrong_slave_ignored) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    CHECK_EQ(feed(&r, req, with_crc(body, 6, req), out), 0);
}

TEST(modbus_bad_crc_ignored_then_recovers) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x01, 0x06, 0x00, 0x05, 0x12, 0x34};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    size_t n = with_crc(body, 6, req);
    req[7] ^= 0x40;                        // corrupt the CRC
    CHECK_EQ(feed(&r, req, n, out), 0);
    CHECK_EQ(r.regs[5], 0);
    CHECK_EQ(r.cb_calls, 0);
    n = with_crc(body, 6, req);            // next good frame is still handled
    CHECK_EQ(feed(&r, req, n, out), 8);
    CHECK_EQ(r.regs[5], 0x1234);
}

TEST(modbus_broadcast_write_executes_without_reply) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x00, 0x06, 0x00, 0x04, 0x00, 0x07};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    CHECK_EQ(feed(&r, req, with_crc(body, 6, req), out), 0);
    CHECK_EQ(r.regs[4], 7);
    CHECK_EQ(r.cb_calls, 1);
}

TEST(modbus_partial_frame_reset_by_gap) {
    Rig r;
    rig_init(&r);
    const uint8_t body[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint8_t req[8], out[MODBUS_MAX_FRAME];
    size_t n = with_crc(body, 6, req);
    r.t_us = 0;
    feed(&r, req, 3, out);                 // first 3 bytes, then silence
    r.t_us = MODBUS_FRAME_TIMEOUT_US + 1000;
    size_t resp = feed(&r, req, n, out);   // complete frame after the gap
    CHECK_EQ(resp, 25);
}
