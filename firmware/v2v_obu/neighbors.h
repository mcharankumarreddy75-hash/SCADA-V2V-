#pragma once
// Neighbour table and packet-delivery-ratio tracking (spec S8.7). No Arduino dependencies.
#include <stdint.h>
#include <stdbool.h>
#include "v2v_packet.h"

#define MAX_NEIGHBORS 3
#define PDR_WINDOW_MS 10000

typedef struct {
    bool         used;
    uint8_t      id;
    v2v_packet_t last;
    uint32_t     last_seen_ms;
    int16_t      rssi_dbm;
    int16_t      snr_cdb;        // SNR x 10
    uint8_t      last_seq;
    uint16_t     rx_count;
    uint16_t     expected_count;
    uint16_t     pdr_permille;   // x 1000
    uint32_t     window_start_ms;
} neighbor_t;

void nb_init(void);
// Call for every accepted packet from another vehicle.
void nb_update(const v2v_packet_t *pkt, int16_t rssi_dbm, int16_t snr_cdb, uint32_t now_ms);
// Free entries silent for more than NEIGHBOR_TIMEOUT_MS.
void nb_expire(uint32_t now_ms);
// i-th used entry (0 .. nb_count()-1), NULL otherwise.
const neighbor_t *nb_get(uint8_t i);
const neighbor_t *nb_find(uint8_t id);
uint8_t nb_count(void);
