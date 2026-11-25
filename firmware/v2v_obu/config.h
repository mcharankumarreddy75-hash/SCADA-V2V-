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
