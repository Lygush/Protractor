#pragma once

#include <Arduino.h>

// Иконки перенесены на внешнюю W25Q32 (сектор 2, ADDR_ICONS, см. flash_map.h
// и Protractor_ExtFlash_PLAN.md п.4.3) — здесь остаются только их размеры и
// смещения внутри сектора. Сами данные больше не занимают PROGMEM.
//
// Иконка батареи: один контур (корпус + "носик") + отдельный сегмент-заливка,
// вместо 5 отдельных полных иконок под каждый уровень заряда.
//
// battery_outline_25x12 — 25x12px, 2 страницы дисплея:
// #######################
// #                     #
// #                     ###
// #                       #
// #                       #
// #                       #
// #                       #
// #                       #
// #                       #
// #                     ###
// #                     #
// #######################
// Внутри контура (x=1..21, 21px) помещаются 5 сегментов по 3px с зазорами
// 1px. Слоты (x0,x1) от левого края иконки: (2,4) (6,8) (10,12) (14,16) (18,20).

#define ICON_OFF_BATTERY_OUTLINE   0UL
#define ICON_SIZE_BATTERY_OUTLINE  50UL   // 25x12 -> 25 * ceil(12/8) = 25*2

#define ICON_OFF_BATTERY_SEGMENT   50UL
#define ICON_SIZE_BATTERY_SEGMENT  6UL    // 3x12 -> 3*2

#define ICON_OFF_MODE_360          56UL
#define ICON_SIZE_MODE_360         68UL   // 34x14 -> 34*2

#define ICON_OFF_MODE_90           124UL
#define ICON_SIZE_MODE_90          48UL   // 24x14 -> 24*2

#define ICON_OFF_CIRCLE_15         172UL
#define ICON_SIZE_CIRCLE_15        30UL   // 15x15 -> 15*2

#define ICON_OFF_ARROW_UP          202UL
#define ICON_SIZE_ARROW_UP         8UL    // 8x8 -> 8*1

#define ICON_OFF_ARROW_DOWN        210UL
#define ICON_SIZE_ARROW_DOWN       8UL    // 8x8 -> 8*1

#define ICON_OFF_ARROW_LEFT        218UL
#define ICON_SIZE_ARROW_LEFT       8UL    // 8x8 -> 8*1

#define ICON_OFF_ARROW_RIGHT       226UL
#define ICON_SIZE_ARROW_RIGHT      8UL    // 8x8 -> 8*1

// Самая большая иконка (mode_360, 68 байт) — под неё общий RAM-буфер
// в oled.cpp, чтобы не заводить пять разных статических массивов.
#define ICON_MAX_SIZE               68UL
