#pragma once
// Shared application state: the contract between v2v_obu.ino (Phase 5, owner/writer)
// and gateway.cpp (Phase 6, reader). Written by the lead; change only by agreement.
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "v2v_packet.h"
#include "neighbors.h"
#include "safety.h"

enum { OBU_BOOT = 0, OBU_NORMAL, OBU_WARNING, OBU_SOS };   // §S4.5

typedef struct {
    uint8_t  id;            // neighbour vehicle id (0 = slot unused)
    uint8_t  level;         // FCW level toward this neighbour (after hysteresis is NOT applied; raw level)
    double   ttc_s;         // INFINITY if none
    double   distance_m;    // INFINITY if unknown (no GPS)
} app_fcw_entry_t;

typedef struct {
    uint8_t      obu_state;          // OBU_*
    own_state_t  own;                // position/speed/heading/gps_valid (GPS or SIM)
    v2v_packet_t own_pkt;            // last own BSM as built (accel, roll, pitch, flags, battery, gps_quality ...)
    uint8_t      warn_level;         // overall FCW level after hysteresis (0/1/2)
    double       min_ttc_s;          // overall lowest TTC, INFINITY if none
    app_fcw_entry_t fcw[MAX_NEIGHBORS];  // per-neighbour FCW results, refreshed at 20 Hz
    bool         radio_ok;
    bool         imu_ok;
    uint32_t     rx_valid_total;     // packets accepted by pkt_validate
    uint32_t     crc_errors;         // HW CRC errors + PKT_ERR_CRC
} app_state_t;

extern app_state_t g_app;            // defined in v2v_obu.ino

// Implemented in v2v_obu.ino, used by gateway.cpp:
uint8_t app_next_seq(void);                                   // own sequence counter (+1 per new packet)
void    app_fill_header(v2v_packet_t *p);                      // zero p, set vehicle_id, timestamp, own position fields
void    app_execute_command(uint8_t code, uint16_t param, uint32_t now_ms); // §S11.4 "Vehicle on COMMAND" for own vehicle (no ACK sent)

#if IS_GATEWAY
// Implemented in gateway.cpp (§S11), called from v2v_obu.ino:
void gw_init(void);                                            // Modbus RTU slave on USB Serial
void gw_poll(uint32_t now_ms);                                 // every loop
void gw_update_registers(uint32_t now_ms);                     // every 100 ms
void gw_run_command(uint8_t code, uint16_t param, uint8_t target, uint32_t now_ms); // §S11.4 trigger handling
void gw_on_packet(const v2v_packet_t *pkt, uint32_t now_ms);   // every accepted packet: counts ACKs, PONG → RTT
#endif
