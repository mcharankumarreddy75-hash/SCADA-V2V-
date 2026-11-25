#include "neighbors.h"
#include "config.h"
#include <string.h>

static neighbor_t s_nb[MAX_NEIGHBORS];

static bool is_data_type(uint8_t type) {
    return type == PKT_BSM || type == PKT_EEBL || type == PKT_CRASH || type == PKT_HAZARD;
}

static neighbor_t *nb_find_mut(uint8_t id) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (s_nb[i].used && s_nb[i].id == id) return &s_nb[i];
    }
    return NULL;
}

// Free slot, or the entry with the oldest last_seen_ms.
static neighbor_t *nb_alloc(uint32_t now_ms) {
    neighbor_t *victim = NULL;
    uint32_t oldest_age = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        neighbor_t *n = &s_nb[i];
        if (!n->used) return n;
        uint32_t age = (uint32_t)(now_ms - n->last_seen_ms);
        if (victim == NULL || age > oldest_age) {
            victim = n;
            oldest_age = age;
        }
    }
    return victim;
}

void nb_init(void) {
    memset(s_nb, 0, sizeof s_nb);
}

void nb_update(const v2v_packet_t *pkt, int16_t rssi_dbm, int16_t snr_cdb, uint32_t now_ms) {
    neighbor_t *n = nb_find_mut(pkt->vehicle_id);
    if (n == NULL) {
        // First packet from this neighbour: counts as one expected packet.
        n = nb_alloc(now_ms);
        memset(n, 0, sizeof *n);
        n->used = true;
        n->id = pkt->vehicle_id;
        n->last_seq = pkt->seq;
        n->last_seen_ms = now_ms;
        n->pdr_permille = 1000;
        n->window_start_ms = now_ms;
        n->expected_count = 1;
    } else {
        bool rebooted = (uint32_t)(now_ms - n->last_seen_ms) > (uint32_t)SENDER_REBOOT_MS;
        uint8_t d = rebooted ? 1 : (uint8_t)(pkt->seq - n->last_seq);
        n->expected_count = (uint16_t)(n->expected_count + d);
        n->last_seq = pkt->seq;
        n->last_seen_ms = now_ms;
    }

    n->rx_count = (uint16_t)(n->rx_count + 1);
    n->rssi_dbm = rssi_dbm;
    n->snr_cdb = snr_cdb;
    if (is_data_type(pkt->type)) n->last = *pkt;

    if ((uint32_t)(now_ms - n->window_start_ms) >= PDR_WINDOW_MS) {
        uint32_t pdr = 1000;
        if (n->expected_count > 0) {
            pdr = ((uint32_t)n->rx_count * 1000u) / n->expected_count;
        }
        if (pdr > 1000) pdr = 1000;
        n->pdr_permille = (uint16_t)pdr;
        n->rx_count = 0;
        n->expected_count = 0;
        n->window_start_ms = now_ms;
    }
}

void nb_expire(uint32_t now_ms) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (s_nb[i].used && (uint32_t)(now_ms - s_nb[i].last_seen_ms) > (uint32_t)NEIGHBOR_TIMEOUT_MS) {
            s_nb[i].used = false;
        }
    }
}

const neighbor_t *nb_get(uint8_t i) {
    uint8_t k = 0;
    for (int j = 0; j < MAX_NEIGHBORS; j++) {
        if (!s_nb[j].used) continue;
        if (k == i) return &s_nb[j];
        k++;
    }
    return NULL;
}

const neighbor_t *nb_find(uint8_t id) {
    return nb_find_mut(id);
}

uint8_t nb_count(void) {
    uint8_t c = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (s_nb[i].used) c++;
    }
    return c;
}
