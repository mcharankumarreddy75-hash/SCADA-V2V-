#pragma once
// ===== Identity =====
#ifndef MY_VEHICLE_ID
#define MY_VEHICLE_ID          1        // overridden per board by tools/flash.py
#endif
#ifndef IS_GATEWAY
#define IS_GATEWAY             0
#endif
#ifndef SIM_MODE
#define SIM_MODE               0
#endif
#define FW_VERSION             100      // 1.00

// ===== Pins =====
#define PIN_RF_NSS    PA4
#define PIN_RF_DIO1   PB1
#define PIN_RF_NRST   PA1
#define PIN_RF_BUSY   PB0
#define PIN_RF_TXEN   PB12
#define PIN_RF_RXEN   PB13
#define PIN_GPS_RX    PA3
#define PIN_GPS_TX    PA2
#define PIN_I2C_SCL   PB6
#define PIN_I2C_SDA   PB7
#define PIN_IMU_INT   PB5
#define PIN_BUZZER    PB8
#define PIN_LED_RED   PB14
#define PIN_LED_YEL   PB15
#define PIN_LED_GRN   PA8
#define PIN_BUTTON    PB9
#define PIN_VBAT      PA0
#define PIN_LED_BOARD PC13

// ===== Radio =====
#define RF_FREQ_MHZ        2445.0
#define RF_BW_KHZ          812.5
#define RF_SF              7
#define RF_CR              5        // 4/5 – check RadioLib's convention
#define RF_PREAMBLE        12
#define RF_CHIP_POWER_DBM  0        // before the E28 amplifier – see S7.1
#define BSM_PERIOD_MS      200
#define BSM_JITTER_MS      40
#define EVENT_COPIES       3
#define EVENT_GAP_MIN_MS   20
#define EVENT_GAP_MAX_MS   50
#define SOS_PERIOD_MS      1000

// ===== GPS =====
#define GPS_BAUD_DEFAULT   9600
#define GPS_BAUD           38400
#define GPS_RATE_MS        200
#define GPS_MIN_SATS       4

// ===== IMU =====
#define IMU_ADDR            0x68
#define IMU_RATE_HZ         100
#define IMU_ACC_LSB_PER_G   4096.0
#define IMU_GYR_LSB_PER_DPS 65.5
// {raw axis index, sign} for forward, left, up
#define IMU_AXIS_MAP  { {0, +1}, {1, +1}, {2, +1} }

// ===== Safety =====
#define TTC_CAUTION_S        4.0
#define TTC_CRITICAL_S       2.0
#define HEADWAY_MIN_S        1.0
#define AHEAD_ANGLE_DEG      20.0
#define SAME_DIR_ANGLE_DEG   30.0
#define MIN_SPEED_FCW        1.0
#define WARN_HOLD_MS         1000
#define HARD_BRAKE_MS2      -3.9
#define HARD_BRAKE_TIME_MS   200
#define EEBL_AHEAD_DEG       45.0
#define EEBL_RANGE_M         300
#define CRASH_G              4.0
#define CRASH_CONFIRM_MS     3000
#define ROLLOVER_DEG         60.0
#define ROLLOVER_TIME_MS     1000
#define POTHOLE_G            1.5
#define HAZARD_AHEAD_DEG     30.0
#define HAZARD_RANGE_M       200
#define NEIGHBOR_TIMEOUT_MS  1500
#define SENDER_REBOOT_MS     5000
#define PACKET_MAX_AGE_MS    500
#define SOS_CANCEL_HOLD_MS   3000

// ===== Battery =====
#define VBAT_DIVIDER  2.0
#define VBAT_FULL     4.20
#define VBAT_EMPTY    3.30
#define VBAT_LOW      3.40
#define VBAT_CUTOFF   3.20

// ===== Simulation =====
#define SIM_LAT0_E7   0       // set to your college coordinates × 1e7
#define SIM_LON0_E7   0

// ===== Modbus =====
#define MODBUS_SLAVE_ID  1
#define MODBUS_BAUD      115200

// ===== Added in Phase 4 =====
// Radio
#define RADIO_PAYLOAD_LEN       32       // = sizeof(v2v_packet_t)
#define RADIO_EVENT_QUEUE_SIZE  4        // event TX queue entries (oldest replaced when full)
// GPS
#define GPS_BOOT_NMEA_TIMEOUT_MS  2000   // no valid NMEA at 38400 within this -> try 9600
#define GPS_FIX_TIMEOUT_MS        2000   // fix considered lost after this long without a fix
#define GPS_BAUD_SWITCH_WAIT_MS   100    // after UBX CFG-PRT, before reopening the port
#define GPS_UBX_GAP_MS            50     // pause between UBX commands (boot only)
#define GPS_UBX_MAX_PAYLOAD       36     // largest payload sent (CFG-NAV5)
#define GPS_DAY_MS                86400000UL
#define UBX_SYNC1                 0xB5
#define UBX_SYNC2                 0x62
#define UBX_CLASS_CFG             0x06
#define UBX_ID_CFG_MSG            0x01
#define UBX_ID_CFG_RATE           0x08
#define UBX_ID_CFG_PRT            0x00
#define UBX_ID_CFG_NAV5           0x24
#define UBX_ID_CFG_CFG            0x09
// UBX payloads (spec S10.4 table)
#define UBX_PAYLOAD_MSG_OFF_GSV   {0xF0, 0x03, 0x00}
#define UBX_PAYLOAD_MSG_OFF_GSA   {0xF0, 0x02, 0x00}
#define UBX_PAYLOAD_MSG_OFF_GLL   {0xF0, 0x01, 0x00}
#define UBX_PAYLOAD_MSG_OFF_VTG   {0xF0, 0x05, 0x00}
#define UBX_PAYLOAD_NAV5_AUTO     {0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define UBX_PAYLOAD_RATE_5HZ      {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00}
#define UBX_PAYLOAD_PRT_38400     {0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x00, 0x96, \
                                   0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}
#define UBX_PAYLOAD_CFG_SAVE      {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, \
                                   0x00, 0x00, 0x17}
// IMU
#define IMU_I2C_CLOCK_HZ          400000
#define IMU_WAKE_DELAY_MS         50     // after PWR_MGMT_1 wake (boot only)
#define IMU_WHO_AM_I_VALUE        0x68
#define IMU_REG_SMPLRT_DIV        0x19
#define IMU_REG_CONFIG            0x1A
#define IMU_REG_GYRO_CONFIG       0x1B
#define IMU_REG_ACCEL_CONFIG      0x1C
#define IMU_REG_ACCEL_XOUT_H      0x3B
#define IMU_REG_PWR_MGMT_1        0x6B
#define IMU_REG_WHO_AM_I          0x75
#define IMU_VAL_PWR_MGMT_1        0x01   // wake, gyro PLL
#define IMU_VAL_SMPLRT_DIV        0x09   // 100 Hz
#define IMU_VAL_CONFIG            0x03   // DLPF ~44 Hz
#define IMU_VAL_GYRO_CONFIG       0x08   // +-500 deg/s
#define IMU_VAL_ACCEL_CONFIG      0x10   // +-8 g
#define IMU_BURST_LEN             14
#define IMU_CAL_SAMPLES           200
#define IMU_CAL_TIME_MS           2000
#define IMU_DT_S                  0.01   // 100 Hz
#define IMU_CF_ALPHA              0.98   // complementary filter gyro weight
#define IMU_ALONG_ALPHA           0.2    // a_long low-pass
#define IMU_AZ_LP_ALPHA           0.02   // az low-pass (az_hp = az - az_lp)
#define GRAVITY_MS2               9.81
#define IMU_RAD2DEG               57.29577951308232
// Power
#define ADC_VREF_V                3.3
#define ADC_MAX_COUNT             4095
#define VBAT_SAMPLES              8
// Button
#define BUTTON_DEBOUNCE_MS        30
// Alerts (buzzer and LEDs are active HIGH: transistor / 330 ohm to LED)
#define ALERT_OUT_ON              HIGH
#define ALERT_OUT_OFF             LOW
#define ALERT_SOS_LOCAL_PERIOD_MS 2000   // buzzer 1 s on / 1 s off
#define ALERT_SOS_LOCAL_LED_MS    100    // half-period: red 5 Hz (100 on / 100 off)
#define ALERT_SOS_REMOTE_BEEP_MS  80
#define ALERT_SOS_REMOTE_COUNT    3      // 3 short beeps each second
#define ALERT_SOS_REMOTE_PERIOD_MS 1000
#define ALERT_SOS_REMOTE_LED_MS   250    // half-period: red 2 Hz
#define ALERT_EEBL_PERIOD_MS      200    // 100/100 ms buzzer, red 5 Hz
#define ALERT_CAUTION_PERIOD_MS   500    // 100 ms beep every 500 ms
#define ALERT_CAUTION_BEEP_MS     100
#define ALERT_HAZ_BEEP_MS         100    // 2 beeps, 100 ms apart, then yellow 2 Hz
#define ALERT_HAZ_GAP_MS          100
#define ALERT_HAZ_COUNT           2
#define ALERT_HAZ_LED_PERIOD_MS   500    // yellow 2 Hz
#define ALERT_NORMAL_BLINK_MS     500    // half-period: green 1 Hz without GPS fix
#define ALERT_LAMP_TEST_MS        100    // boot lamp test beep (S10.5)
#define ALERT_LAMP_TEST_LED_MS    500    // boot lamp test: all LEDs on 0.5 s (S10.5)
#define ALERT_NUM_PATTERNS        7

// ===== Added in Phase 6 =====
// Modbus RTU slave (modbus_rtu.h, §S11.1). Slave ID and baud: MODBUS_SLAVE_ID / MODBUS_BAUD above.
#define MODBUS_FC_READ_HOLDING      0x03
#define MODBUS_FC_WRITE_SINGLE      0x06
#define MODBUS_FC_WRITE_MULTI       0x10
#define MODBUS_EX_ILLEGAL_FUNCTION  0x01
#define MODBUS_EX_ILLEGAL_ADDRESS   0x02
#define MODBUS_EX_ILLEGAL_VALUE     0x03
#define MODBUS_BROADCAST_ID         0
#define MODBUS_REG_COUNT            110    // holding registers 0-109
#define MODBUS_MAX_READ_QTY         125
#define MODBUS_MAX_WRITE_QTY        123
#define MODBUS_MAX_FRAME            256    // largest RTU frame buffer (ADU)
#define MODBUS_FRAME_TIMEOUT_US     50000  // silence this long resets a partial frame (USB CDC delivers in bursts)
// Gateway register map (§S11.2)
#define GW_REG_FW_VERSION           0
#define GW_REG_STATUS               1
#define GW_REG_NEIGHBOR_COUNT       2
#define GW_REG_LAST_RTT_MS          3
#define GW_REG_CMD_ACK_COUNT        4
#define GW_REG_RX_TOTAL             5
#define GW_REG_TX_TOTAL             6
#define GW_REG_CRC_ERRORS           7
#define GW_REG_GATEWAY_ID           8
#define GW_REG_UPTIME_S             9
#define GW_STATUS_RADIO_OK          (1u << 0)
#define GW_STATUS_GPS_FIX           (1u << 1)
#define GW_STATUS_IMU_OK            (1u << 2)
#define GW_STATUS_SIM_MODE          (1u << 3)
// Vehicle slots (§S11.3): base = GW_SLOT_BASE + GW_SLOT_SIZE * slot
#define GW_SLOT_BASE                10
#define GW_SLOT_SIZE                20
#define GW_NUM_SLOTS                4      // slot 0 = own vehicle, slots 1-3 = neighbours
#define GW_NB_SLOTS                 3
#define GW_RESERVED_LO_FIRST        90     // registers 90-99 reserved (read 0)
#define GW_RESERVED_LO_LAST         99
#define GW_RESERVED_HI_FIRST        104    // registers 104-109 reserved (read 0)
#define GW_RESERVED_HI_LAST         109
#define GW_OFF_ID                   0
#define GW_OFF_LAT_HI               1
#define GW_OFF_LAT_LO               2
#define GW_OFF_LON_HI               3
#define GW_OFF_LON_LO               4
#define GW_OFF_SPEED                5
#define GW_OFF_HEADING              6
#define GW_OFF_ACCEL                7
#define GW_OFF_ROLL                 8
#define GW_OFF_PITCH                9
#define GW_OFF_FLAGS                10
#define GW_OFF_BATTERY              11
#define GW_OFF_RSSI                 12
#define GW_OFF_SNR                  13
#define GW_OFF_PDR                  14
#define GW_OFF_DISTANCE             15
#define GW_OFF_TTC                  16
#define GW_OFF_WARN                 17
#define GW_OFF_GPS_QUALITY          18
#define GW_OFF_AGE_MS              19
// Commands (§S11.4)
#define GW_REG_CMD_CODE             100
#define GW_REG_CMD_PARAM            101
#define GW_REG_CMD_TARGET           102
#define GW_REG_CMD_TRIGGER          103
#define GW_CMD_HAZARD_ALERT         1
#define GW_CMD_SPEED_ADVISORY       2
#define GW_CMD_CLEAR_SOS            3
#define GW_CMD_BUZZER_TEST          4
#define GW_CMD_PING                 5
#define GW_CMD_MAX_LOCAL            4      // codes 1-4 also run on the gateway's own vehicle
#define GW_TRIGGER_VALUE            1
#define GW_NONE                     65535  // "no value" for distance / TTC / unknown
#define GW_PDR_FULL                 1000   // 100.0 % in 0.1 % units
#define GW_UPDATE_PERIOD_MS         100

// ===== Added in Phase 5 =====
// Build options (set by tools/flash.py; defaults here)
#ifndef LOG_MODE
#define LOG_MODE                  0        // reserved for Phase 8 (flash.py --log)
#endif

// Simulation (SIM_MODE). SIM_LAT0_E7 / SIM_LON0_E7 above stay 0 until the college
// coordinates are known; sim.cpp then uses the PLACEHOLDER values below.
// PLACEHOLDER: replace with the real college latitude / longitude x 1e7.
#define SIM_LAT0_PLACEHOLDER_E7   173850000
#define SIM_LON0_PLACEHOLDER_E7   784867000
#define SIM_START_WEST_M          120.0    // vehicle 2 starts this far west of vehicle 1
#define SIM_SPEED_MS              8.0      // vehicle 2 approach speed
#define SIM_STOP_BEHIND_M         3.0      // vehicle 2 stops this far behind vehicle 1
#define SIM_WAIT_MS               10000UL  // vehicle 2 waits at the stop point, then repeats

// Main loop scheduler (S10.5)
#define LOOP_IMU_PERIOD_MS        10       // imu_update + detectors (100 Hz)
#define LOOP_FCW_PERIOD_MS        50       // nb_expire + FCW (20 Hz)
#define LOOP_GW_REG_PERIOD_MS     100      // gw_update_registers (gateway only)
#define LOOP_SEC_PERIOD_MS        1000     // power_read, radio enable, debug line
#define WATCHDOG_TIMEOUT_US       2000000UL
#define LED_FAIL_BLINK_MS         100      // radio failure: red LED 5 Hz (100 on / 100 off)
#define DEBUG_BAUD                115200
#define DEBUG_LINE_MAX           256

// Alert durations for received events and commands
#define ALERT_EEBL_RX_MS          2000     // S9.4 receiver: EEBL pattern
#define ALERT_SOS_REMOTE_RX_MS    5000     // S9.5 receiver: SOS_REMOTE pattern
#define ALERT_HAZARD_RX_MS        2000     // S9.7 receiver: HAZARD pattern
#define ALERT_CMD_HAZARD_MS       3000     // S11.4 code 1
#define ALERT_CMD_FCW_MS          3000     // S11.4 code 2 (over the speed limit)
#define ALERT_CMD_BUZZER_MS       2000     // S11.4 code 4

// Command codes (S11.4 CMD_CODE)
#define CMD_CODE_HAZARD_ALERT     1
#define CMD_CODE_SPEED_ADVISORY   2
#define CMD_CODE_CLEAR_SOS        3
#define CMD_CODE_BUZZER_TEST      4
#define CMD_CODE_PING             5        // not a vehicle COMMAND: PING packet instead
#define KMH_PER_MS                3.6      // km/h per m/s (S11.4 code 2)

// ===== Added in Phase 8 =====
// Data logging (LOG_MODE == 1, tools/flash.py --log). One CSV row per FCW tick (every
// LOOP_FCW_PERIOD_MS = 50 ms, i.e. 20 Hz) on Serial1 and, in OBU builds, on USB Serial.
// The 1 Hz debug line is off in this build. Values are fixed point: integer = value x LOG_CSV_SCALE
// (no float printf on newlib-nano). Columns: ms, a_long (m/s^2), a_total (g), az_hp (m/s^2),
// roll (deg), pitch (deg), speed (m/s), level (0/1/2), ttc (s).
#define LOG_CSV_SCALE             100      // fixed-point factor for every value column
#define LOG_CSV_LINE_MAX          128      // buffer for one row (the header is sent in chunks)
#define LOG_CSV_TTC_NONE          -1       // ttc column when there is no TTC (infinite)
#define LOG_CSV_LIMIT             2000000000L  // clamp for the fixed-point integers
