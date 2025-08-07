#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "GyverOLED.h"
#include "settings.h"
#include "icons.h"

class OLED {
public:
    OLED() = default;
    ~OLED() = default;

    void init(Display_settings* disp_settings);
    void print_start_page();
    void update_battery_value(String battery);
    virtual void print_base_page() = 0;
    virtual void update_angle_value(float angle) = 0;
    virtual int alignment(float angle_f) = 0;
    void print_low_battary_warning(String& battery);
    void print_charging_page();
    void print_progress_bar(uint8_t actual_section, uint8_t full_bar, char* text);
    void change_revers();
    bool get_revers() { return revers; };

    template<typename T>
    void print_debug(T value, int x, int y, bool clear) {
        if (clear) oled.clear();
        oled.setScale(1);
        oled.setCursor(x, y);
        oled.print(value);
        oled.update();
    }

protected:
    void print_base_page_common();
    GyverOLED<SSD1306_128x64, OLED_BUFFER> oled;
    Display_settings* oled_settings;
    bool revers{false};
};

class OLED_Degrees_360 : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
};

class OLED_Degrees_90 : public OLED {
public:
    OLED_Degrees_90(DisplayMode _mode);
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
private:
    DisplayMode mode{};
};

class OLED_Radians : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
};