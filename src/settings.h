#pragma once

#include <Arduino.h>

enum MODE
{
    DEGREES = 0,
    RADIANS = 1
};

enum DisplayMode {
    MODE_90_LEFT,
    MODE_360,
    MODE_RADIANS,
    MODE_90_RIGHT
};

struct Display_settings
{
    bool revers{false};
};

struct Mpu_settings
{
    uint8_t calibration_strenght{10}; //Максимум 10
};


class Settings 
{
public:
    Settings() = default;
    ~Settings() = default;

    void read_settings();
    void wright_settings();

    Mpu_settings* get_mpu_settings();
    Display_settings* get_display_settings();

private:
    Display_settings oled_settings;
    Mpu_settings mpu_settings;
    MODE mode{DEGREES};
};
