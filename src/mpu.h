#pragma once

#define BUFFER_SIZE 100

#include "Arduino.h"

#include "MPU6050.h"
#include "I2Cdev.h"

#include "oled.h"
#include "settings.h"

enum AXIS {
    X = 2,
    Y = 1,
    Z = 0
};

class MPU {
public:
    MPU() = default;
    ~MPU() = default;

    void init(OLED* oled_ptr, Mpu_settings* settings);
    void calibration(uint8_t strenght);
    void calculate_angles();

    float get_angle_radians(AXIS axis);
    float get_angle_degrees(AXIS axis);

    void set_zero();
    void reset();
    void set_oled(OLED* oled_ptr);  // Новый метод

private:
    MPU6050 mpu;

    uint8_t fifoBuffer[64];
    Quaternion q;
    VectorFloat gravity;

    float ypr[3]{};
    float ypr_zero[3]{};

    Mpu_settings* mpu_settings;
    OLED* oled;
};