#pragma once
// SX1281 (E28-2G4M20SX) radio wrapper, spec S7 and S10.4. Uses RadioLib 8.0.0.
#include <Arduino.h>
#include <stddef.h>
#include "config.h"
#include "v2v_packet.h"

typedef struct {
    uint8_t data[RADIO_PAYLOAD_LEN];
    size_t  len;
    int16_t rssi_dbm;
    int16_t snr_cdb;   // SNR x 10
} radio_rx_t;

bool radio_init();                          // RadioLib begin() + DIO1 ISR + startReceive()
bool radio_ok();
void radio_isr();                           // DIO1 ISR: only sets a flag
bool radio_send(const v2v_packet_t *pkt);   // blocking TX (~12 ms), then startReceive()
void radio_queue_event(const v2v_packet_t *pkt, uint32_t now_ms); // EVENT_COPIES copies, S7.2
bool radio_service(uint32_t now_ms, radio_rx_t *rx);  // sends due event copies; reads one RX packet
void radio_set_enabled(bool en);            // false below VBAT_CUTOFF: TX blocked, RX continues
uint32_t radio_tx_total();
uint32_t radio_crc_errors();
void radio_count_crc_error();               // caller: pkt_validate returned PKT_ERR_CRC
uint32_t radio_time_on_air_us();           // for RADIO_PAYLOAD_LEN bytes
