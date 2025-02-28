#pragma once

#include <Arduino.h>

struct Display_settings
{
    enum MODE
    {
        DEGREES = 0,
        RADIANS = 1
    };
    MODE mode{MODE::DEGREES};
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

public:
    Display_settings oled_settings;
    Mpu_settings mpu_settings;
};

