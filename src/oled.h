#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "GyverOLED.h"
#include "settings.h"
#include "icons.h"

class OLED
{
public:
    OLED(Settings* _settings) : oled_settings(_settings) {};
    ~OLED() = default;

    void init();

    void print_start_page();

    void print_base_page();

    void update_angle_value(String& angle);
    void update_battery_value(String& battery);

    void print_low_battary_warning(String& battery);

    void print_charging_page();

    void print_progress_bar(uint8_t actual_section, uint8_t full_bar);

    void change_revers();

    void change_mode(Display_settings::MODE new_mode);

private:
    GyverOLED<SSD1306_128x64, OLED_BUFFER> oled;

    Settings* oled_settings;
};