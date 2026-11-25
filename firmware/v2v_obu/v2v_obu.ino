// Phase 4 placeholder: links and compiles all drivers. Phase 5 replaces this file.
#include "config.h"
#include "v2v_packet.h"
#include "safety.h"
#include "radio.h"
#include "gps.h"
#include "imu.h"
#include "power.h"
#include "alerts.h"
#include "button.h"

void setup() {
    Serial.begin(115200);
    power_init();
    alerts_init();
    alert_lamp_test();
    button_init();
    gps_init();
    imu_init();
    imu_calibrate();
    radio_init();
}

void loop() {
    uint32_t now = millis();
    static radio_rx_t rx;
    gps_poll(now);
    imu_update();
    radio_service(now, &rx);
    button_update(now);
    alert_update(now);
}
