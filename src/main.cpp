#include <Arduino.h>

#include <GyverButton.h>

#include "settings.h"
#include "oled.h"
#include "mpu.h"

Settings settings;
OLED* oled;
MPU mpu;
GButton mode_btn(3);

volatile bool mpu_flag = false;

uint32_t timer_sec{};
uint32_t time{};

uint32_t timer_reset{};

DisplayMode current_mode = MODE_90_LEFT;

void dmp_ready() {
    mpu_flag = true;
}

void change_mode() {
    delete oled;
    switch (current_mode) {
        case MODE_90_LEFT:
            oled = new OLED_Degrees_360;
            current_mode = MODE_360;
            break;
        case MODE_360:
            oled = new OLED_Radians;
            current_mode = MODE_RADIANS;
            break;
        case MODE_RADIANS:
            oled = new OLED_Degrees_90(MODE_90_RIGHT);
            current_mode = MODE_90_RIGHT;
            break;
        case MODE_90_RIGHT:
            oled = new OLED_Degrees_90(MODE_90_LEFT);
            current_mode = MODE_90_LEFT;
            break;
    }
    oled->init(settings.get_display_settings());
    oled->print_base_page();
    mpu.set_oled(oled);  // Обновляем указатель
}

void setup() {
    Serial.begin(9600);
    Wire.begin();

    oled = new OLED_Degrees_90(MODE_90_LEFT);

    oled->init(settings.get_display_settings());
    oled->print_start_page();

    mpu.init(oled, settings.get_mpu_settings());
    attachInterrupt(0, dmp_ready, RISING);
    mpu.calibration(10);

    oled->print_base_page();

    pinMode(A0, INPUT);
    pinMode(D7, OUTPUT);
    digitalWrite(D7, HIGH);

    timer_reset = millis();
}

extern int __bss_end;
extern void *__brkval;

int memoryFree() {
    int freeValue;
    if ((int)__brkval == 0)
        freeValue = ((int)&freeValue) - ((int)&__bss_end);
    else
        freeValue = ((int)&freeValue) - ((int)__brkval);
    return freeValue;
}

void loop() {
    uint16_t battery = analogRead(A0);
    float voltage{(battery * 5) / 1024};

    mode_btn.tick();
    if (mode_btn.isClick()) {
        change_mode();
    }
    if (mode_btn.isHold()) {
        mpu.set_zero();
    }
    if (mpu_flag) {
        mpu.calculate_angles();
        float angle{mpu.get_angle_radians(AXIS::Z)};
        oled->update_angle_value(angle);
        mpu_flag = false;
    }

    if (millis() - timer_sec >= 1000) {
        oled->print_debug<uint32_t>(time, 45, 7, false);
        time++;
        timer_sec = millis();
    }
}