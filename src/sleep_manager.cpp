#include "sleep_manager.h"
#include <GyverPower.h>

// ISR ничего не делает — самого факта прерывания достаточно, чтобы
// sleep_cpu() внутри power.sleep() отпустил выполнение. Но без объявленного
// ISR любой из этих векторов при срабатывании увёл бы МК в перезагрузку —
// поэтому обработчики обязательны, даже пустые.
ISR(PCINT0_vect) {}  // D8            (порт B)
ISR(PCINT2_vect) {}  // D6, D7        (порт D)

void SleepManager::init(Sleep_settings* s) {
    settings = s;
    initialized = false;
}

bool SleepManager::update(float angle_degrees) {
    if (!initialized) {
        reference_angle   = angle_degrees;
        last_activity_ms  = millis();
        initialized       = true;
        return false;
    }

    // abs() тут работает и для float (это макрос-тернарник, не целочисленная
    // функция) — реальное движение за пределы порога сбрасывает и якорь,
    // и таймер; дрейф внутри порога — нет.
    if (abs(angle_degrees - reference_angle) > settings->stillness_threshold_deg) {
        reference_angle  = angle_degrees;
        last_activity_ms = millis();
        return false;
    }

    return (millis() - last_activity_ms >= settings->inactivity_timeout_ms);
}

void SleepManager::reset_activity() {
    last_activity_ms = millis();
}

void SleepManager::enter_sleep() {
    // PCINT на кнопках: D6/D7 — PCINT2 (порт D), D8 — PCINT0 (порт B).
    // Настраиваем перед каждым входом в сон — не требует отдельного флага
    // "уже настроено" и не мешает остальной работе GButton (она просто
    // поллит пины в tick(), с PCINT не конфликтует).
    PCICR  |= (1 << PCIE0) | (1 << PCIE2);
    PCMSK2 |= (1 << PCINT22) | (1 << PCINT23);  // D6, D7
    PCMSK0 |= (1 << PCINT0);                    // D8

    // Сбрасываем уже "залипшие" флаги прерываний — если на пине была
    // дребезжащая помеха до или в момент включения маски, PCIFR мог
    // выставиться заранее, и sleep_cpu() тогда вываливается из сна
    // мгновенно, будто прерывание уже произошло (хотя реального
    // пробуждающего нажатия ещё не было).
    PCIFR |= (1 << PCIF0) | (1 << PCIF2);

    power.setSleepMode(POWERDOWN_SLEEP);
    // SLEEP_FOREVER: внутри _sleep() при этом значении WDT вообще не
    // заводится (см. GyverPower.cpp) — спим чисто до прерывания, без
    // периодических подъёмов по таймеру. ISR-обработчики PCINT0/PCINT2
    // ничего не делают — им достаточно просто сработать, чтобы sleep_cpu()
    // отпустил выполнение обратно сюда.
    power.sleep(SLEEP_FOREVER);
}
