#include "mpu.h"


void MPU::init(OLED* oled_ptr)
{
    oled = oled_ptr;
    mpu.initialize();
   // mpu.dmpInitialize();
    mpu.setDMPEnabled(true);
}

void MPU::calibration(uint8_t strenght)
{
    long offsets[6];
    long offsetsOld[6];
    int16_t mpuGet[6];
    // используем стандартную точность
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
    // обнуляем оффсеты
    mpu.setXAccelOffset(0);
    mpu.setYAccelOffset(0);
    mpu.setZAccelOffset(0);
    mpu.setXGyroOffset(0);
    mpu.setYGyroOffset(0);
    mpu.setZGyroOffset(0);
    delay(10);
    //Serial.println("Calibration start. It will take about 5 seconds");
    for (byte n = 0; n < strenght; n++) {     // 10 итераций калибровки
        for (byte j = 0; j < 6; j++) {    // обнуляем калибровочный массив
            offsets[j] = 0;
        }
        for (byte i = 0; i < 100 + BUFFER_SIZE; i++) { // делаем BUFFER_SIZE измерений для усреднения
            mpu.getMotion6(&mpuGet[0], &mpuGet[1], &mpuGet[2], &mpuGet[3], &mpuGet[4], &mpuGet[5]);
            if (i >= 99) {                         // пропускаем первые 99 измерений
            for (byte j = 0; j < 6; j++) {
                offsets[j] += (long)mpuGet[j];   // записываем в калибровочный массив
            }
            }
        }
        for (byte i = 0; i < 6; i++) {
            offsets[i] = offsetsOld[i] - ((long)offsets[i] / BUFFER_SIZE); // учитываем предыдущую калибровку
            if (i == 2) offsets[i] += 16384;                               // если ось Z, калибруем в 16384
            offsetsOld[i] = offsets[i];
        }
        // ставим новые оффсеты
        mpu.setXAccelOffset(offsets[0] / 8);
        mpu.setYAccelOffset(offsets[1] / 8);
        mpu.setZAccelOffset(offsets[2] / 8);
        mpu.setXGyroOffset(offsets[3] / 4);
        mpu.setYGyroOffset(offsets[4] / 4);
        mpu.setZGyroOffset(offsets[5] / 4);
        delay(2);
        oled->print_progress_bar(n, strenght);
    }
}