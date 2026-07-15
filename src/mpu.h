#pragma once

#include "Arduino.h"

#include "MPU6050.h"
#include "I2Cdev.h"

#include "oled.h"
#include "settings.h"

enum AXIS {
    X = 2,  // ypr[2] = roll  (наклон влево/вправо) — нужная ось для угломера
    Y = 1,  // ypr[1] = pitch (наклон вперёд/назад)
    Z = 0   // ypr[0] = yaw   (вращение в горизонтальной плоскости)
};

class MPU {
public:
    MPU() = default;
    ~MPU() = default;

    void init(OLED* oled_ptr, Mpu_settings* settings);
    void calibration(uint8_t strength);
    void calculate_angles();

    float get_angle_radians(AXIS axis);
    float get_angle_degrees(AXIS axis);

    // Знак этой компоненты гравитации (уже посчитанной DMP каждый кадр в
    // calculate_angles()) говорит, какой стороной плата смотрит "вверх" —
    // используется для автопереворота экрана в main.cpp, без единого
    // лишнего чтения датчика.
    float get_gravity_z() const { return gravity.z; }

    // tan(текущий угол) для режимов %/мм-на-метр — БЕЗ вызова tan()/atan().
    // DMP сам получает угол как atan2(...) из компонентов гравитации (см.
    // dmpGetYawPitchRoll() в MPU6050_6Axis_MotionApps20.cpp) — а раз угол =
    // atan2(a, b), то tan(угол) тождественно равен a/b, той же паре чисел,
    // из которой библиотека и получила угол. Экономит ~700 байт flash
    // (tan()/atan() тянут свои экземпляры extended-precision float rutin,
    // которых иначе в прошивке вообще нет).
    float get_slope_ratio(AXIS axis) const;

    void set_zero();
    void request_zero();               // "умное" зануление: ждём стабильности перед фиксацией
    bool is_zeroing() const { return zero_pending; }
    void reset();
    void set_oled(OLED* oled_ptr);

    // Энергосбережение: усыпить/разбудить сам датчик (PWR_MGMT_1 sleep bit).
    // sleep() перед этим явно глушит DMP/прерывание, wake() их явно включает
    // обратно — DMP не гарантированно переживает цикл сна сам по себе, было
    // замечено, что после setSleepEnabled(false) прерывания dmp_ready могут
    // просто не возобновиться без явного re-enable.
    void sleep();
    void wake();

private:
    void update_zero_stability();      // проверяет, устаканилось ли значение, и коммитит zero
    AXIS current_axis() const { return (mpu_settings->axis == MeasureAxis::Y) ? AXIS::Y : AXIS::X; }

    MPU6050 mpu;

    uint8_t fifoBuffer[64]{};
    Quaternion q;
    VectorFloat gravity;

    float ypr[3]{};
    float ypr_zero[3]{};

    // "умное" зануление
    bool zero_pending{false};
    float zero_last_angle{0};
    uint32_t zero_stable_since{0};
    static constexpr float ZERO_TOLERANCE = 0.005f;   // рад, ~0.3° допуск между кадрами
    static constexpr uint32_t ZERO_STABLE_MS = 700;    // сколько держать стабильность перед фиксацией

    Mpu_settings* mpu_settings{nullptr};
    OLED* oled{nullptr};
};
