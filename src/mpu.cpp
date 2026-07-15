#include "mpu.h"

void MPU::init(OLED* oled_ptr, Mpu_settings* settings) {
    oled = oled_ptr;
    mpu.initialize();
    delay(5);

    // Калибровка ДО инициализации DMP
    mpu.setXAccelOffset(4050);
    mpu.setYAccelOffset(2365);
    mpu.setZAccelOffset(5518);
    mpu.setXGyroOffset(61);
    mpu.setYGyroOffset(-29);
    mpu.setZGyroOffset(83);

    mpu.SetOLED(oled_ptr);
    //calibration(settings->calibration_strength);

    mpu.dmpInitialize();
    delay(5);
    mpu.setDMPEnabled(true);
    mpu.setIntDMPEnabled(true);

    mpu_settings = settings;
}

void MPU::calibration(uint8_t strength) {
    mpu.CalibrateAccel(strength, "Accel");
    mpu.CalibrateGyro(strength, "Gyro");
}

void MPU::calculate_angles() {
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        update_zero_stability();
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

// См. комментарий в mpu.h. Roll (AXIS::X) = atan2(gravity.y, gravity.z) ->
// tan(roll) = gravity.y/gravity.z. Pitch (AXIS::Y) = atan2(gravity.x,
// sqrt(gravity.y²+gravity.z²)) -> tan(pitch) = gravity.x/sqrt(...) — sqrt()
// тут не новая цена, dmpGetYawPitchRoll() и так его считает для того же pitch.
// Не учитывает "ноль" (zero_offset) — %/мм-на-метр показывают физический
// уклон площадки, а не относительно занулённой точки, что и требуется для
// строительной задачи (в отличие от режима градусов, который меряет
// относительно zero).
float MPU::get_slope_ratio(AXIS axis) const {
    if (axis == AXIS::X) {
        return (gravity.z != 0.0f) ? (gravity.y / gravity.z) : 0.0f;
    }
    float denom = sqrt(gravity.y * gravity.y + gravity.z * gravity.z);
    return (denom != 0.0f) ? (gravity.x / denom) : 0.0f;
}

void MPU::set_zero() {
    ypr_zero[0] = ypr[0];
    ypr_zero[1] = ypr[1];
    ypr_zero[2] = ypr[2];
}

// Запрос "умного" зануления — само зануление произойдёт позже,
// когда угол перестанет меняться (устройство отпустили и оно не шевелится)
void MPU::request_zero() {
    zero_pending = true;
    zero_stable_since = 0;   // отсчёт стабильности начнётся с первого же кадра
}

// Вызывается из calculate_angles() на каждый новый кадр DMP.
// Следит только за той осью, которая реально выводится на экран (см.
// mpu_settings->axis / current_axis()), т.к. yaw (ypr[0]) без магнитометра
// всегда немного "плывёт" и не подходит как критерий устойчивости.
void MPU::update_zero_stability() {
    if (!zero_pending) return;

    float current = ypr[current_axis()];
    uint32_t now = millis();

    if (fabs(current - zero_last_angle) > ZERO_TOLERANCE) {
        zero_stable_since = now;              // было шевеление — сбрасываем отсчёт
    } else if (zero_stable_since == 0) {
        zero_stable_since = now;              // первый стабильный кадр — стартуем отсчёт
    } else if (now - zero_stable_since >= ZERO_STABLE_MS) {
        set_zero();                           // достаточно долго стабильно — фиксируем ноль
        zero_pending = false;
    }

    zero_last_angle = current;
}

void MPU::reset() {
    mpu.resetDMP();
    delay(5);
    mpu.reset();
}

void MPU::set_oled(OLED* oled_ptr) {
    oled = oled_ptr;
    mpu.SetOLED(oled_ptr);
}

void MPU::sleep() {
    // Явно глушим DMP и его прерывание ДО того, как усыпить сам чип —
    // чистое, предсказуемое состояние, симметричное wake() ниже.
    mpu.setIntDMPEnabled(false);
    mpu.setDMPEnabled(false);
    mpu.setSleepEnabled(true);
}

void MPU::wake() {
    mpu.setSleepEnabled(false);
    delay(50);   // датчику нужно время на стабилизацию генератора после сна
    mpu.resetFIFO();
    mpu.setDMPEnabled(true);
    mpu.setIntDMPEnabled(true);
}