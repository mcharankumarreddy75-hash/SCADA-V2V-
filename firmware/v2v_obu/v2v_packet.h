#pragma once
// Portable packet format (spec S8). No Arduino dependencies.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define V2V_START   0xA5
#define PKT_BSM     0x01
#define PKT_EEBL    0x02
#define PKT_CRASH   0x03
#define PKT_HAZARD  0x04
#define PKT_COMMAND 0x05
#define PKT_ACK     0x06
#define PKT_PING    0x07
#define PKT_PONG    0x08

#define FLAG_HARD_BRAKE    (1u << 0)
#define FLAG_CRASH         (1u << 1)
#define FLAG_ROLLOVER      (1u << 2)
#define FLAG_HAZARD        (1u << 3)
#define FLAG_WARN_CAUTION  (1u << 4)
#define FLAG_WARN_CRITICAL (1u << 5)
#define FLAG_GPS_VALID     (1u << 6)
#define FLAG_LOW_BATTERY   (1u << 7)
#define TIMESTAMP_NONE     0xFFFFFFFFUL

#pragma pack(push, 1)
typedef struct {
    uint8_t  start;
    uint8_t  type;
    uint8_t  vehicle_id;
    uint8_t  seq;
    uint32_t timestamp;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint16_t speed_cms;       // COMMAND: code
    uint16_t heading_cdeg;    // COMMAND: parameter
    int16_t  accel_long_cms2;
    int8_t   roll_deg;
    int8_t   pitch_deg;
    int16_t  yaw_rate_cdps;
    uint8_t  flags;
    uint8_t  battery_pct;
    uint8_t  gps_quality;
    uint8_t  target_id;
    uint16_t crc;
} v2v_packet_t;
#pragma pack(pop)

static_assert(sizeof(v2v_packet_t) == 32, "v2v_packet_t must be 32 bytes");

// CRC-16/CCITT-FALSE over data[0..len-1]. Test: "123456789" -> 0x29B1.
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

// Caller fills all payload fields first. Sets start, type, seq and crc.
void pkt_build(v2v_packet_t *p, uint8_t type, uint8_t seq);

typedef enum {
    PKT_OK = 0,
    PKT_ERR_LEN,
    PKT_ERR_START,
    PKT_ERR_CRC,
    PKT_ERR_OWN,
    PKT_ERR_DUP,
    PKT_ERR_STALE,
    PKT_ERR_TARGET
} pkt_result_t;

#define SEQ_TABLE_SIZE 8
typedef struct {
    bool     used;
    uint8_t  id;
    uint8_t  last_seq;
    uint32_t last_seen_ms;
} seq_entry_t;

typedef struct {
    seq_entry_t e[SEQ_TABLE_SIZE];
} seq_table_t;

void seq_table_init(seq_table_t *t);

typedef struct {
    uint8_t  own_id;
    bool     own_gps_valid;
    uint32_t local_utc_ms;   // own UTC time-of-day (ms, 0..86399999), only meaningful if own_gps_valid
    bool     sim_mode;
} rx_ctx_t;

// Implements S8.6 steps 1-7 in order, dropping at the first failure.
// Copies buf into *out on success.
pkt_result_t pkt_validate(const uint8_t *buf, size_t len, const rx_ctx_t *ctx,
                          seq_table_t *st, uint32_t now_ms, v2v_packet_t *out);
