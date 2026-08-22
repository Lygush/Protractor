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

// Иконки единиц измерения (см. Задачи.txt п.4) — рисуются в одном и том же
// слоте базового экрана (x=104..125, y=0), какая именно — зависит от
// Display_settings::unit (см. OLED_Degrees_90::draw_current_unit_icon() в
// oled.cpp). Все, кроме мм/м, одной ширины (14px) — это позволяет рисовать
// их одним общим вызовом drawIconFromFlash() без веток под каждую иконку.
#define ICON_OFF_CIRCLE_14         172UL
#define ICON_SIZE_CIRCLE_14        28UL   // 14x14 -> 14*2 (единица: градусы)

#define ICON_OFF_PI                200UL
#define ICON_SIZE_PI               28UL   // 14x14 -> 14*2 (единица: радианы)

#define ICON_OFF_PERCENT           228UL
#define ICON_SIZE_PERCENT          28UL   // 14x14 -> 14*2 (единица: %)

#define ICON_OFF_MM_PER_M          256UL
#define ICON_SIZE_MM_PER_M        44UL   // 22x14 -> 22*2 (единица: мм/м)

// Новый рисунок стрелок (заменил старые 8x8 up/down, добавились left/right,
// которые раньше были только зарезервированы без данных).
#define ICON_OFF_ARROW_UP          300UL
#define ICON_SIZE_ARROW_UP         7UL    // 7x8 -> 7*1

#define ICON_OFF_ARROW_DOWN        307UL
#define ICON_SIZE_ARROW_DOWN       7UL    // 7x8 -> 7*1

#define ICON_OFF_ARROW_LEFT        314UL
#define ICON_SIZE_ARROW_LEFT       8UL    // 8x7 (округляется до 8 строк) -> 8*1

#define ICON_OFF_ARROW_RIGHT       322UL
#define ICON_SIZE_ARROW_RIGHT      8UL    // 8x7 (округляется до 8 строк) -> 8*1

// Иконка режима MODE_90_TARGET (Задачи.txt п.1) — заменяет текстовую метку
// "90 TGT" на базовом экране, тот же слот и размер (x=52,y=0, 24x14), что
// у mode_360/mode_90 в других режимах.
#define ICON_OFF_TGT               330UL
#define ICON_SIZE_TGT              48UL   // 24x14 -> 24*2

// Индикация текущей оси измерения (Задачи.txt п.2) — рисуется в базовом
// футере во всех режимах (см. OLED::print_base_page_common() в oled.cpp).
#define ICON_OFF_X_AXE             378UL
#define ICON_SIZE_X_AXE            28UL   // 14x14 -> 14*2

#define ICON_OFF_Y_AXE             406UL
#define ICON_SIZE_Y_AXE            30UL   // 15x14 -> 15*2

// Лого-заставка (лягушка), 128x64 -> 128 * (64/8) = 1024 байта. Данные
// записаны на внешнюю флеш (ProtractorFlashWriter), но отрисовка пока НЕ
// подключена (см. Задачи.txt п.3) — 1024 байта не влезут в текущий
// RAM-буфер иконок (icon_buf, см. ICON_MAX_SIZE), под сплэш-скрин
// потребуется отдельный механизм чтения/отрисовки по частям.
#define ICON_OFF_FROG_LOGO         436UL
#define ICON_SIZE_FROG_LOGO        1024UL // 128x64 -> 128*8

// Самая большая ИЗ РИСУЕМЫХ иконок (mode_360, 68 байт) — под неё общий
// RAM-буфер в oled.cpp. Лого (1024 байта) сюда не входит, т.к. не рисуется.
#define ICON_MAX_SIZE               68UL
