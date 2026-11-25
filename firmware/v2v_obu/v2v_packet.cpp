#include "v2v_packet.h"
#include "config.h"
#include <string.h>

#define PKT_CRC_OFFSET 30
#define MS_PER_DAY     86400000

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void pkt_build(v2v_packet_t *p, uint8_t type, uint8_t seq) {
    p->start = V2V_START;
    p->type = type;
    p->seq = seq;
    p->crc = crc16_ccitt((const uint8_t *)p, PKT_CRC_OFFSET);
}

void seq_table_init(seq_table_t *t) {
    memset(t, 0, sizeof *t);
}

// Find the entry for sender id, or NULL.
static seq_entry_t *seq_find(seq_table_t *st, uint8_t id) {
    for (int i = 0; i < SEQ_TABLE_SIZE; i++) {
        if (st->e[i].used && st->e[i].id == id) return &st->e[i];
    }
    return NULL;
}

// Get a free entry, or replace the least-recently-seen one.
static seq_entry_t *seq_alloc(seq_table_t *st, uint32_t now_ms) {
    seq_entry_t *victim = NULL;
    uint32_t oldest_age = 0;
    for (int i = 0; i < SEQ_TABLE_SIZE; i++) {
        seq_entry_t *e = &st->e[i];
        if (!e->used) return e;
        uint32_t age = (uint32_t)(now_ms - e->last_seen_ms);
        if (victim == NULL || age > oldest_age) {
            victim = e;
            oldest_age = age;
        }
    }
    return victim;
}

static bool is_target_checked(uint8_t type) {
    return type == PKT_COMMAND || type == PKT_PING || type == PKT_ACK || type == PKT_PONG;
}

pkt_result_t pkt_validate(const uint8_t *buf, size_t len, const rx_ctx_t *ctx,
                          seq_table_t *st, uint32_t now_ms, v2v_packet_t *out) {
    // 1. Length
    if (len != sizeof(v2v_packet_t)) return PKT_ERR_LEN;

    // 2. Start byte
    if (buf[0] != V2V_START) return PKT_ERR_START;

    // 3. CRC (stored little-endian, same as the in-memory struct on STM32)
    uint16_t crc_rx;
    memcpy(&crc_rx, buf + PKT_CRC_OFFSET, sizeof crc_rx);
    if (crc16_ccitt(buf, PKT_CRC_OFFSET) != crc_rx) return PKT_ERR_CRC;

    v2v_packet_t pkt;
    memcpy(&pkt, buf, sizeof pkt);

    // 4. Own packet
    if (pkt.vehicle_id == ctx->own_id) return PKT_ERR_OWN;

    // 5. Sequence: duplicate / old packets dropped; first packet, reboot (silent > 5 s) accepted.
    seq_entry_t *e = seq_find(st, pkt.vehicle_id);
    if (e == NULL) {
        e = seq_alloc(st, now_ms);
        e->used = true;
        e->id = pkt.vehicle_id;
    } else {
        bool rebooted = (uint32_t)(now_ms - e->last_seen_ms) > (uint32_t)SENDER_REBOOT_MS;
        if (!rebooted) {
            uint8_t d = (uint8_t)(pkt.seq - e->last_seq);
            if (d == 0 || d >= 128) return PKT_ERR_DUP;   // duplicate or older
        }
    }
    e->last_seq = pkt.seq;
    e->last_seen_ms = now_ms;

    // 6. Freshness (only when both sides have GPS and not in SIM_MODE)
    if (!ctx->sim_mode && ctx->own_gps_valid &&
        (pkt.flags & FLAG_GPS_VALID) && pkt.timestamp != TIMESTAMP_NONE &&
        pkt.type != PKT_PING && pkt.type != PKT_PONG) {
        int32_t age = (int32_t)(ctx->local_utc_ms - pkt.timestamp);
        if (age < 0) age += MS_PER_DAY;
        if (age > (int32_t)PACKET_MAX_AGE_MS) return PKT_ERR_STALE;
    }

    // 7. Target
    if (is_target_checked(pkt.type) && pkt.target_id != 0 && pkt.target_id != ctx->own_id) {
        return PKT_ERR_TARGET;
    }

    *out = pkt;
    return PKT_OK;
}
