#include "test.h"
#include "neighbors.h"
#include <cstring>

static v2v_packet_t make(uint8_t type, uint8_t vid, uint8_t seq) {
    v2v_packet_t p;
    memset(&p, 0, sizeof p);
    p.type = type;
    p.vehicle_id = vid;
    p.seq = seq;
    p.flags = FLAG_GPS_VALID;
    return p;
}

TEST(nb_basic_update_find_and_get) {
    nb_init();
    CHECK_EQ(nb_count(), 0);
    v2v_packet_t p = make(PKT_BSM, 2, 1);
    nb_update(&p, -80, 55, 0);
    CHECK_EQ(nb_count(), 1);
    const neighbor_t *n = nb_find(2);
    CHECK(n != NULL);
    if (n) {
        CHECK_EQ(n->id, 2);
        CHECK_EQ(n->rssi_dbm, -80);
        CHECK_EQ(n->snr_cdb, 55);
        CHECK_EQ(n->last.vehicle_id, 2);
        CHECK_EQ(n->last.type, PKT_BSM);
        CHECK_EQ(n->pdr_permille, 1000);
    }
    CHECK(nb_get(0) == nb_find(2));
    CHECK(nb_get(1) == NULL);
    CHECK(nb_find(7) == NULL);
}

TEST(nb_expires_after_1500ms_silence) {
    nb_init();
    v2v_packet_t p = make(PKT_BSM, 2, 1);
    nb_update(&p, -80, 0, 0);
    nb_expire(1000);
    CHECK_EQ(nb_count(), 1);
    nb_expire(1500);
    CHECK_EQ(nb_count(), 1);   // exactly 1500 ms is not yet over the timeout
    nb_expire(1501);
    CHECK_EQ(nb_count(), 0);
    CHECK(nb_find(2) == NULL);
}

TEST(nb_non_data_types_update_rssi_not_last) {
    nb_init();
    v2v_packet_t p = make(PKT_ACK, 3, 9);
    nb_update(&p, -70, 40, 100);
    const neighbor_t *n = nb_find(3);
    CHECK(n != NULL);
    if (n) {
        CHECK_EQ(n->rssi_dbm, -70);
        CHECK_EQ(n->last.type, 0);   // ACK is not copied into .last
        CHECK_EQ(n->last_seen_ms, 100);
    }
}

TEST(nb_full_table_replaces_oldest) {
    nb_init();
    v2v_packet_t p;
    p = make(PKT_BSM, 2, 1); nb_update(&p, 0, 0, 0);
    p = make(PKT_BSM, 3, 1); nb_update(&p, 0, 0, 100);
    p = make(PKT_BSM, 4, 1); nb_update(&p, 0, 0, 200);
    CHECK_EQ(nb_count(), MAX_NEIGHBORS);
    p = make(PKT_BSM, 5, 1); nb_update(&p, 0, 0, 300);
    CHECK_EQ(nb_count(), MAX_NEIGHBORS);
    CHECK(nb_find(2) == NULL);    // oldest last_seen evicted
    CHECK(nb_find(3) != NULL);
    CHECK(nb_find(4) != NULL);
    CHECK(nb_find(5) != NULL);
}

TEST(nb_pdr_with_every_other_packet_lost) {
    nb_init();
    // Sender transmits seq 0,2,4,... every 100 ms; receiver gets every second one only
    // (so we feed seq 0 then seq 2k for k=1..100, i.e. 1 packet per 2 sent).
    // Window closes at t = 10000 ms: rx = 101, expected = 1 + 100*2 = 201 -> 502 permille.
    v2v_packet_t p = make(PKT_BSM, 2, 0);
    nb_update(&p, -80, 0, 0);
    for (uint32_t k = 1; k <= 100; k++) {
        p = make(PKT_BSM, 2, (uint8_t)(2 * k));
        nb_update(&p, -80, 0, k * 100);
    }
    const neighbor_t *n = nb_find(2);
    CHECK(n != NULL);
    if (n) {
        CHECK_NEAR(n->pdr_permille, 500, 15);
        CHECK(n->pdr_permille >= 490 && n->pdr_permille <= 510);
        CHECK_EQ(n->rx_count, 0);   // counters reset after the window closed
    }
}

TEST(nb_pdr_no_loss_is_1000) {
    nb_init();
    v2v_packet_t p;
    for (uint32_t k = 0; k <= 100; k++) {
        p = make(PKT_BSM, 2, (uint8_t)k);
        nb_update(&p, -80, 0, k * 100);
    }
    const neighbor_t *n = nb_find(2);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->pdr_permille, 1000);
}
