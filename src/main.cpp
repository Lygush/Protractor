#include <Arduino.h>

#include "settings.h"
#include "oled.h"
#include "mpu.h"

Settings settings;

OLED oled(&settings);
//MPU mpu;

volatile bool mpu_flag = false;

void dmp_ready()
{
    mpu_flag = true;
}

void setup() {
  pinMode(D3,INPUT_PULLUP);

  Serial.begin(9600);
  Wire.begin();

  oled.init();
  oled.print_start_page();

 // mpu.init(&oled);

 // attachInterrupt(0, dmp_ready, RISING);

 // mpu.calibration(10);
}

void loop() {
  // if (mpuFlag && mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
  //   Quaternion q;
  //   VectorFloat gravity;
  //   float ypr[3];

  //   mpu.dmpGetQuaternion(&q, fifoBuffer);
  //   mpu.dmpGetGravity(&gravity, &q);
  //   mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

  //   float angle_f = atan2(-ypr[2], ypr[1]) * RAD_TO_DEG;
  //   if (angle_f < 0)
  //   angle_f += 360;

  //   mpuFlag = false;
  // }
}
