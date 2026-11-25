// SX1281 radio driver. RadioLib 8.0.0 API (see Phase 4 notes):
//   begin(const ConfigLoRa_t&) with Hz/Hz units, CR given as 5 = 4/5,
//   setDio1Action(fn) for the IRQ, setRfSwitchPins(rxEn, txEn), readData() returns
//   RADIOLIB_ERR_CRC_MISMATCH on a hardware CRC error (data still filled in).
// RadioLib.h warns (#warning, -Wcpp) whenever USB CDC is enabled, which the required
// FQBN (usb=CDCgen) always does. Debug output is not used, so the warning is silenced here only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <RadioLib.h>
#pragma GCC diagnostic pop
#include <string.h>
#include <math.h>
#include "radio.h"

static Module radio_mod(PIN_RF_NSS, PIN_RF_DIO1, PIN_RF_NRST, PIN_RF_BUSY);
static SX1281 radio_chip(&radio_mod);

static volatile bool rf_flag = false;   // set by DIO1 ISR (RX done or TX done)
static bool ok = false;
static bool enabled = true;
static uint32_t tx_count = 0;
static uint32_t crc_count = 0;

typedef struct {
    bool            used;
    v2v_packet_t    pkt;
    uint8_t         copies_left;
    uint32_t        queued_ms;
    uint32_t        next_ms;
} event_entry_t;

static event_entry_t events[RADIO_EVENT_QUEUE_SIZE];

void radio_isr() {
    rf_flag = true;
}

bool radio_init() {
    ok = false;
    radio_chip.setRfSwitchPins(PIN_RF_RXEN, PIN_RF_TXEN);

    ConfigLoRa_t cfg;
    cfg.frequency = RADIOLIB_UNIT_MEGA_FLT(RF_FREQ_MHZ);   // Hz
    cfg.bandwidth = RADIOLIB_UNIT_KILO_FLT(RF_BW_KHZ);     // Hz (812.5 kHz)
    cfg.spreadingFactor = RF_SF;
    cfg.codingRate = RF_CR;                                 // 5 = 4/5
    cfg.syncWord = RADIOLIB_SX128X_SYNC_WORD_PRIVATE;
    cfg.power = RF_CHIP_POWER_DBM;
    cfg.preambleLength = RF_PREAMBLE;

    int16_t st = radio_chip.begin(cfg);
    if (st != RADIOLIB_ERR_NONE) return false;

    radio_chip.setDio1Action(radio_isr);
    randomSeed(micros());
    rf_flag = false;
    st = radio_chip.startReceive();
    if (st != RADIOLIB_ERR_NONE) return false;
    ok = true;
    return true;
}

bool radio_ok() {
    return ok;
}

// Reads the packet waiting in the chip. Caller must already have checked rf_flag.
static bool read_packet(radio_rx_t *rx) {
    size_t plen = radio_chip.getPacketLength();          // must precede readData()
    rx->rssi_dbm = (int16_t)lroundf(radio_chip.getRSSI());
    rx->snr_cdb = (int16_t)lroundf(radio_chip.getSNR() * 10.0f);
    uint8_t buf[RADIO_PAYLOAD_LEN];
    size_t want = plen;
    if (want > RADIO_PAYLOAD_LEN) want = RADIO_PAYLOAD_LEN;
    int16_t st = radio_chip.readData(buf, want);
    radio_chip.startReceive();
    if (st == RADIOLIB_ERR_CRC_MISMATCH) {
        crc_count++;
        return false;
    }
    if (st != RADIOLIB_ERR_NONE) return false;
    memcpy(rx->data, buf, want);
    rx->len = want;
    return true;
}

// A packet that arrived just before a transmit is read out first and kept here, so the
// TX-done flag clear below cannot discard it. radio_service() returns it on its next call.
static radio_rx_t pending_rx;
static bool pending_valid = false;

bool radio_send(const v2v_packet_t *pkt) {
    if (!ok || !enabled || pkt == NULL) return false;
    if (rf_flag) {                                         // unread RX waiting (§S7.3)
        rf_flag = false;
        if (!pending_valid) pending_valid = read_packet(&pending_rx);
        else radio_chip.startReceive();                    // one slot only; newer packet is lost
    }
    int16_t st = radio_chip.transmit((const uint8_t *)pkt, RADIO_PAYLOAD_LEN);
    // DIO1 also rose for TX-done: discard that edge so it is not read as a packet.
    rf_flag = false;
    radio_chip.startReceive();
    if (st != RADIOLIB_ERR_NONE) return false;
    tx_count++;
    return true;
}

void radio_queue_event(const v2v_packet_t *pkt, uint32_t now_ms) {
    int slot = -1;
    uint32_t oldest_age = 0;
    for (int i = 0; i < RADIO_EVENT_QUEUE_SIZE; i++) {
        if (!events[i].used) { slot = i; break; }
        uint32_t age = now_ms - events[i].queued_ms;      // unsigned: wrap-safe
        if (slot < 0 || age > oldest_age) { slot = i; oldest_age = age; }
    }
    if (slot < 0) return;
    events[slot].used = true;
    events[slot].pkt = *pkt;
    events[slot].copies_left = EVENT_COPIES;
    events[slot].queued_ms = now_ms;
    events[slot].next_ms = now_ms;                         // first copy due now
}

bool radio_service(uint32_t now_ms, radio_rx_t *rx) {
    bool got = false;
    if (!ok) return false;

    if (pending_valid && rx != NULL) {                     // packet saved by radio_send()
        *rx = pending_rx;
        pending_valid = false;
        got = true;
    } else if (rf_flag) {
        rf_flag = false;
        if (rx != NULL) got = read_packet(rx);
        else { radio_chip.startReceive(); }
    }

    // One due event copy per call; TX ~12 ms so the loop is not held long.
    for (int i = 0; i < RADIO_EVENT_QUEUE_SIZE; i++) {
        event_entry_t *e = &events[i];
        if (!e->used) continue;
        if ((int32_t)(now_ms - e->next_ms) < 0) continue;  // not due yet
        radio_send(&e->pkt);                                // copy consumed even if disabled
        e->copies_left--;
        if (e->copies_left == 0) {
            e->used = false;
        } else {
            uint32_t gap = (uint32_t)random(EVENT_GAP_MIN_MS, EVENT_GAP_MAX_MS + 1);
            e->next_ms = now_ms + gap;
        }
        break;
    }
    return got;
}

void radio_set_enabled(bool en) {
    enabled = en;
}

uint32_t radio_tx_total() {
    return tx_count;
}

uint32_t radio_crc_errors() {
    return crc_count;
}

void radio_count_crc_error() {
    crc_count++;
}

uint32_t radio_time_on_air_us() {
    return (uint32_t)radio_chip.getTimeOnAir(RADIO_PAYLOAD_LEN);
}
