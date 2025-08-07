#include "mpu.h"

void MPU::init(OLED* oled_ptr, Mpu_settings* settings) {
    oled = oled_ptr;
    mpu.initialize();
    delay(5);
    mpu.dmpInitialize();
    delay(5);
    mpu.setDMPEnabled(true);
    mpu.setIntDMPEnabled(true);  // Включение прерываний DMP
    mpu.SetOLED(oled_ptr);
    mpu_settings = settings;
}

void MPU::calibration(uint8_t strenght) {
    mpu.CalibrateAccel(strenght, "Accel");
    mpu.CalibrateGyro(strenght, "Gyro");
}

void MPU::calculate_angles() {
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    } else {
        mpu.resetFIFO();
    }
}

float MPU::get_angle_radians(AXIS axis) {   
    return ypr[axis] - ypr_zero[axis];
}

float MPU::get_angle_degrees(AXIS axis) {
    return degrees(ypr[axis] - ypr_zero[axis]);
}

void MPU::set_zero() {
    ypr_zero[0] = ypr[0];
    ypr_zero[1] = ypr[1];
    ypr_zero[2] = ypr[2];
}

void MPU::reset() {
    mpu.resetDMP();
    delay(5);
    mpu.reset();
}

// Новый метод
void MPU::set_oled(OLED* oled_ptr) {
    oled = oled_ptr;
    mpu.SetOLED(oled_ptr);
}