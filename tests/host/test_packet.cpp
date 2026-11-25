#include "test.h"
#include "v2v_packet.h"
#include <cstddef>
#include <cstring>

static void fill(v2v_packet_t &p, uint8_t type, uint8_t vid, uint8_t seq, uint32_t ts) {
    memset(&p, 0, sizeof p);
    p.vehicle_id = vid;
    p.timestamp = ts;
    p.lat_e7 = 120000000;
    p.lon_e7 = 770000000;
    p.speed_cms = 1500;
    p.heading_cdeg = 9000;
    p.flags = FLAG_GPS_VALID;
    p.battery_pct = 90;
    p.gps_quality = (uint8_t)((8 << 4) | 2);
    p.target_id = 0;
    pkt_build(&p, type, seq);
}

static void to_bytes(const v2v_packet_t &p, uint8_t *buf) {
    memcpy(buf, &p, sizeof p);
}

// Receiver context for own ID 1. Returns storage that is overwritten by the next call.
static const rx_ctx_t *ctx(bool gps = false, uint32_t local_ms = 0, bool sim = false) {
    static rx_ctx_t c;
    c.own_id = 1;
    c.own_gps_valid = gps;
    c.local_utc_ms = local_ms;
    c.sim_mode = sim;
    return &c;
}

TEST(packet_size_and_field_offsets) {
    CHECK_EQ(sizeof(v2v_packet_t), 32);
    CHECK_EQ(offsetof(v2v_packet_t, start), 0);
    CHECK_EQ(offsetof(v2v_packet_t, type), 1);
    CHECK_EQ(offsetof(v2v_packet_t, vehicle_id), 2);
    CHECK_EQ(offsetof(v2v_packet_t, seq), 3);
    CHECK_EQ(offsetof(v2v_packet_t, timestamp), 4);
    CHECK_EQ(offsetof(v2v_packet_t, lat_e7), 8);
    CHECK_EQ(offsetof(v2v_packet_t, lon_e7), 12);
    CHECK_EQ(offsetof(v2v_packet_t, speed_cms), 16);
    CHECK_EQ(offsetof(v2v_packet_t, heading_cdeg), 18);
    CHECK_EQ(offsetof(v2v_packet_t, accel_long_cms2), 20);
    CHECK_EQ(offsetof(v2v_packet_t, roll_deg), 22);
    CHECK_EQ(offsetof(v2v_packet_t, pitch_deg), 23);
    CHECK_EQ(offsetof(v2v_packet_t, yaw_rate_cdps), 24);
    CHECK_EQ(offsetof(v2v_packet_t, flags), 26);
    CHECK_EQ(offsetof(v2v_packet_t, battery_pct), 27);
    CHECK_EQ(offsetof(v2v_packet_t, gps_quality), 28);
    CHECK_EQ(offsetof(v2v_packet_t, target_id), 29);
    CHECK_EQ(offsetof(v2v_packet_t, crc), 30);
}

TEST(crc_check_value_123456789) {
    const uint8_t msg[] = "123456789";
    CHECK_EQ(crc16_ccitt(msg, 9), 0x29B1);
}

TEST(build_sets_start_type_seq_and_crc) {
    v2v_packet_t p;
    fill(p, PKT_EEBL, 2, 77, 1000);
    CHECK_EQ(p.start, V2V_START);
    CHECK_EQ(p.type, PKT_EEBL);
    CHECK_EQ(p.seq, 77);
    uint8_t buf[32];
    to_bytes(p, buf);
    CHECK_EQ(crc16_ccitt(buf, 30), p.crc);
}

TEST(valid_packet_accepted_and_copied) {
    v2v_packet_t p, out;
    fill(p, PKT_BSM, 2, 1, 1000);
    uint8_t buf[32];
    to_bytes(p, buf);
    seq_table_t st;
    seq_table_init(&st);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);
    CHECK(memcmp(&out, &p, sizeof p) == 0);
}

TEST(length_check_rejects_wrong_size) {
    v2v_packet_t p;
    fill(p, PKT_BSM, 2, 1, 1000);
    uint8_t buf[32];
    to_bytes(p, buf);
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    CHECK_EQ(pkt_validate(buf, 31, ctx(), &st, 0, &out), PKT_ERR_LEN);
    CHECK_EQ(pkt_validate(buf, 33, ctx(), &st, 0, &out), PKT_ERR_LEN);
}

TEST(start_byte_checked) {
    v2v_packet_t p;
    fill(p, PKT_BSM, 2, 1, 1000);
    uint8_t buf[32];
    to_bytes(p, buf);
    buf[0] = 0x5A;
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_START);
}

TEST(corrupted_byte_fails_crc) {
    v2v_packet_t p;
    fill(p, PKT_BSM, 2, 1, 1000);
    uint8_t buf[32];
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    // Flip one bit in every byte after the start byte (payload and CRC bytes).
    // CRC is step 3, before the own-ID / sequence / target checks, so each must report PKT_ERR_CRC.
    for (int i = 1; i < 32; i++) {
        to_bytes(p, buf);
        buf[i] ^= 0x04;
        CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_CRC);
    }
}

TEST(own_packet_dropped) {
    v2v_packet_t p;
    fill(p, PKT_BSM, 1, 1, 1000);
    uint8_t buf[32];
    to_bytes(p, buf);
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_OWN);
}

TEST(sequence_newer_ok_duplicate_and_older_dropped) {
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;

    fill(p, PKT_BSM, 2, 10, 0);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);

    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 100, &out), PKT_ERR_DUP);   // duplicate

    fill(p, PKT_BSM, 2, 11, 0);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 200, &out), PKT_OK);        // newer

    fill(p, PKT_BSM, 2, 9, 0);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 300, &out), PKT_ERR_DUP);   // older (d = 254)
}

TEST(sequence_wraparound_255_to_0_accepted) {
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    uint8_t seqs[] = {253, 254, 255, 0, 1};
    uint32_t t = 0;
    for (uint8_t s : seqs) {
        fill(p, PKT_BSM, 2, s, 0);
        to_bytes(p, buf);
        CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, t, &out), PKT_OK);
        t += 100;
    }
    fill(p, PKT_BSM, 2, 0, 0);     // repeat of 0 is a duplicate
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, t, &out), PKT_ERR_DUP);
}

TEST(rebooted_sender_accepted_after_5s_silence) {
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    fill(p, PKT_BSM, 2, 50, 0);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);

    fill(p, PKT_BSM, 2, 3, 0);     // seq went back: looks old while sender is active
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 3000, &out), PKT_ERR_DUP);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 6000, &out), PKT_OK);   // silent 6 s: rebooted
}

TEST(freshness_basic_and_midnight_wrap) {
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;

    // Age 100 ms: fresh.
    seq_table_t st;
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 900);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 1000), &st, 0, &out), PKT_OK);

    // Age 600 ms: stale.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 400);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 1000), &st, 0, &out), PKT_ERR_STALE);

    // Midnight wrap: local 100 ms after midnight, packet 200 ms before midnight -> age 200, fresh.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 86399800u);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100), &st, 0, &out), PKT_OK);

    // Midnight wrap: local 50 ms after midnight, packet 90 ms before -> age 140, fresh.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 86399910u);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 50), &st, 0, &out), PKT_OK);

    // Midnight wrap but too old: local 100 ms, packet 2 s before midnight -> stale.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 86398000u);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100), &st, 0, &out), PKT_ERR_STALE);
}

TEST(freshness_skipped_when_not_applicable) {
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    seq_table_t st;

    // Stale timestamp but own GPS invalid -> not checked.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(false, 100000), &st, 0, &out), PKT_OK);

    // Stale timestamp but SIM mode -> not checked.
    seq_table_init(&st);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100000, true), &st, 0, &out), PKT_OK);

    // Sender has no GPS flag -> not checked.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, 1);
    p.flags = 0;
    pkt_build(&p, PKT_BSM, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100000), &st, 0, &out), PKT_OK);

    // TIMESTAMP_NONE -> not checked.
    seq_table_init(&st);
    fill(p, PKT_BSM, 2, 1, TIMESTAMP_NONE);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100000), &st, 0, &out), PKT_OK);

    // PING is not checked for freshness.
    seq_table_init(&st);
    fill(p, PKT_PING, 2, 1, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(true, 100000), &st, 0, &out), PKT_OK);
}

TEST(target_id_checked_for_command_ping_ack_pong) {
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    seq_table_t st;
    seq_table_init(&st);

    fill(p, PKT_COMMAND, 2, 1, 0);
    p.target_id = 2;      // not us (own = 1)
    pkt_build(&p, PKT_COMMAND, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_TARGET);

    fill(p, PKT_COMMAND, 3, 1, 0);
    p.target_id = 1;      // us
    pkt_build(&p, PKT_COMMAND, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);

    fill(p, PKT_ACK, 4, 1, 0);
    p.target_id = 0;      // all
    pkt_build(&p, PKT_ACK, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);

    fill(p, PKT_PONG, 5, 1, 0);
    p.target_id = 9;
    pkt_build(&p, PKT_PONG, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_TARGET);

    // BSM target_id is not checked.
    fill(p, PKT_BSM, 6, 1, 0);
    p.target_id = 9;
    pkt_build(&p, PKT_BSM, 1);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_OK);
}

TEST(seq_table_replaces_least_recently_seen) {
    seq_table_t st;
    seq_table_init(&st);
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    // Fill all 8 slots with senders 2..9 at times 0,100,...,700.
    for (uint8_t i = 0; i < SEQ_TABLE_SIZE; i++) {
        fill(p, PKT_BSM, (uint8_t)(2 + i), 1, 0);
        to_bytes(p, buf);
        CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, (uint32_t)(i * 100), &out), PKT_OK);
    }
    // Sender 10 arrives at 1000 ms: sender 2 (seen longest ago) must be evicted.
    fill(p, PKT_BSM, 10, 1, 0);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 1000, &out), PKT_OK);
    int used = 0, has2 = 0, has10 = 0;
    for (int i = 0; i < SEQ_TABLE_SIZE; i++) {
        if (!st.e[i].used) continue;
        used++;
        if (st.e[i].id == 2) has2 = 1;
        if (st.e[i].id == 10) has10 = 1;
    }
    CHECK_EQ(used, 8);
    CHECK_EQ(has2, 0);
    CHECK_EQ(has10, 1);
}

// §S8.6 "drop at the first failure": a packet rejected at step 7 (wrong target) must not
// advance the sender's sequence state, so a later valid packet with the same seq is accepted.
TEST(rejected_packet_does_not_commit_sequence) {
    v2v_packet_t out;
    uint8_t buf[32];
    v2v_packet_t p;
    seq_table_t st;
    seq_table_init(&st);

    fill(p, PKT_COMMAND, 2, 7, 0);
    p.target_id = 3;      // not us (own = 1) -> dropped at step 7
    pkt_build(&p, PKT_COMMAND, 7);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 0, &out), PKT_ERR_TARGET);

    fill(p, PKT_BSM, 2, 7, 0);   // same sender, same seq: must NOT be seen as a duplicate
    pkt_build(&p, PKT_BSM, 7);
    to_bytes(p, buf);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 10, &out), PKT_OK);
    CHECK_EQ(pkt_validate(buf, 32, ctx(), &st, 20, &out), PKT_ERR_DUP);  // now it is
}
