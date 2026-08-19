#pragma once

#include <Arduino.h>
#include "settings.h"

// Отслеживает бездействие устройства (угол не выходит за ±stillness_threshold_deg
// от "якорной" точки — так дрейф не мешает уйти в сон, но реальное движение
// исправно сбрасывает таймер) и организует глубокий сон МК.
//
// Пробуждение — по Pin Change Interrupt на кнопках D6/D7 (порт D, PCINT2) и
// D8 (порт B, PCINT0). Так вышло из-за расположения кнопок: attachInterrupt()
// штатно работает только с INT0/INT1 (обычно D2/D3), поэтому для D6/D7/D8
// используется PCINT — он тоже официально пробуждает из POWERDOWN_SLEEP.
//
// SleepManager не трогает MPU и OLED напрямую (нет ссылок на эти объекты) —
// подготовку и восстановление периферии перед/после сна делает main.cpp,
// как и с Battery (см. apply_battery_to_oled()). Так модули остаются
// независимыми друг от друга.
class SleepManager {
public:
    SleepManager() = default;

    void init(Sleep_settings* settings);

    // Вызывать из loop() с актуальным углом (в градусах) при каждом новом
    // измерении MPU. Возвращает true, когда пора уходить в сон — таймер
    // бездействия истёк. Дальше вызывающий код сам готовит MPU/OLED и
    // вызывает enter_sleep().
    bool update(float angle_degrees);

    // Настраивает PCINT на кнопках и уводит МК в POWERDOWN_SLEEP до
    // пробуждающего нажатия. Возвращается уже после пробуждения. САМО
    // нажатие в этот момент всё ещё физически активно (или как раз
    // отпускается) — GButton, как только возобновит tick() в обычном
    // loop(), честно засчитает это как реальный клик. Чтобы это нажатие не
    // срабатывало как обычный клик, main.cpp после wakeUp()/reset_activity()
    // дожидается отпускания кнопок и сливает накопленные флаги ДО того, как
    // управление уходит в обычную обработку кнопок (см. комментарий в
    // main.cpp у sleep_manager.update()).
    void enter_sleep();

    void reset_activity();

private:
    Sleep_settings* settings{nullptr};

    float    reference_angle{0.0f};   // угол в момент последнего сброса таймера
    uint32_t last_activity_ms{0};
    bool     initialized{false};      // первый update() всегда просто запоминает угол, не решает
};
