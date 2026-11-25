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
#define ALERT_SOS_LOCAL_LED_MS    200    // red 5 Hz
#define ALERT_SOS_REMOTE_BEEP_MS  80
#define ALERT_SOS_REMOTE_COUNT    3      // 3 short beeps each second
#define ALERT_SOS_REMOTE_PERIOD_MS 1000
#define ALERT_SOS_REMOTE_LED_MS   500    // red 2 Hz
#define ALERT_EEBL_PERIOD_MS      200    // 100/100 ms buzzer, red 5 Hz
#define ALERT_CAUTION_PERIOD_MS   500    // 100 ms beep every 500 ms
#define ALERT_CAUTION_BEEP_MS     100
#define ALERT_HAZ_BEEP_MS         100    // 2 beeps, 100 ms apart, then yellow 2 Hz
#define ALERT_HAZ_GAP_MS          100
#define ALERT_HAZ_COUNT           2
#define ALERT_HAZ_LED_PERIOD_MS   500    // yellow 2 Hz
#define ALERT_NORMAL_BLINK_MS     1000   // green 1 Hz without GPS fix
#define ALERT_LAMP_TEST_MS        100    // boot lamp test beep
#define ALERT_NUM_PATTERNS        7
