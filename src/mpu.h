#pragma once

#define BUFFER_SIZE 100

#include "Arduino.h"

#include "MPU6050_6Axis_MotionApps20.h" 
#include "I2Cdev.h"

#include "oled.h"

class MPU 
{
public:
    MPU() = default;
    ~MPU() = default;

    void init(OLED* oled_ptr);
    void calibration(uint8_t strenght);
    
private:
    MPU6050 mpu;
    OLED* oled;
};