#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "GyverOLED.h"
#include "settings.h"
#include "icons.h"

class OLED {
public:
    OLED() = default;
    virtual ~OLED() = default;

    void init(Display_settings* disp_settings);
    void print_start_page();

    // Рисует иконку батареи. blink_visible=false используется для фазы "погашено"
    // при мигании в состоянии LOW_BATTERY (см. вызывающий код в main.cpp).
    void update_battery(bool blink_visible = true);
    void set_battery_percent(uint8_t percent) { battery_percent = percent; }
    void set_battery_state(BatteryState state) { battery_state = state; }

    virtual void print_base_page() = 0;
    virtual void update_angle_value(float angle) = 0;
    virtual int alignment(float angle_f) = 0;
    void print_progress_bar(uint8_t actual_section, uint8_t full_bar, const char* text);
    void change_revers();
    bool get_revers() { return revers; }

    template<typename T>
    void print_debug(T value, int x, int y, bool clr) {
        if (clr) oled.clear();
        oled.setScale(1);
        oled.setCursor(x, y);
        oled.print(value);
        oled.update();
    }

protected:
    void print_base_page_common();
    void send_num_buffer(const String& text);

    GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
    Display_settings* oled_settings{nullptr};
    bool revers{false};

    uint8_t      battery_percent{0};
    BatteryState battery_state{BatteryState::NORMAL};
};

class OLED_Degrees_360 : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
};

class OLED_Degrees_90 : public OLED {
public:
    OLED_Degrees_90() = default;
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
};

class OLED_Radians : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
    int alignment(float angle_f) override;
};
