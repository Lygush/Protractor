#pragma once

#include <Arduino.h>
#include "settings.h"

// BatteryState определён в settings.h (общий тип для Battery и OLED).

// Модуль измерения заряда батареи через резистивный делитель напряжения.
//
// Схема: Vbat --[ R1 ]-- (точка измерения, ADC) --[ R2 ]-- GND
// Vbat = Vadc * (R1 + R2) / R2
//
// Отдельного пина статуса зарядки (STAT/CHRG) нет, поэтому CHARGING определяется
// эвристически: если сглаженное напряжение устойчиво растёт несколько измерительных
// окон подряд — считаем, что идёт зарядка. Это менее надёжно, чем аппаратный сигнал
// и может ошибаться на резких скачках нагрузки — пороги ниже можно донастроить,
// либо потом завести реальный CHRG-пин, если он появится на плате.
class Battery {
public:
    Battery() = default;

    void init(Battery_settings* settings);

    // Вызывать регулярно из loop(). Сама решает, когда пора мерить (см. MEASURE_INTERVAL_MS).
    // Возвращает true, если процент или состояние изменились с прошлого раза — по этому
    // флагу удобно решать, перерисовывать ли что-то на экране.
    bool update();

    BatteryState get_state() const   { return state; }
    float        get_voltage() const { return voltage; }
    uint8_t      get_percent() const { return percent; }

private:
    uint16_t read_adc_averaged();
    float    adc_to_voltage(uint16_t adc_raw) const;
    uint8_t  voltage_to_percent(float v) const;

    void update_low_state();        // гистерезис NORMAL <-> LOW_BATTERY по проценту
    void update_charging_state();   // эвристика CHARGING по тренду напряжения

    Battery_settings* settings{nullptr};

    float        voltage{0.0f};
    uint8_t      percent{0};
    BatteryState state{BatteryState::NORMAL};

    // ── Фильтрация сырых показаний АЦП ──────────────────────────────
    float voltage_ema{0.0f};
    bool  ema_initialized{false};
    static constexpr float EMA_ALPHA = 0.2f;

    // ── Оценка тренда напряжения для эвристики зарядки ──────────────
    float    voltage_ema_prev_window{0.0f};
    uint32_t last_trend_check_ms{0};
    uint8_t  rising_windows{0};
    uint8_t  falling_windows{0};
    static constexpr uint32_t TREND_WINDOW_MS = 15000; // окно оценки тренда, мс

    uint32_t last_measure_ms{0};
    static constexpr uint32_t MEASURE_INTERVAL_MS = 2000; // как часто реально дёргаем АЦП, мс

    // ── Что уже "показали наружу" в прошлый раз (для return-значения update()) ──
    uint8_t      last_reported_percent{255}; // заведомо невалидное -> первый update() всегда "изменился"
    BatteryState last_reported_state{BatteryState::NORMAL};

    static constexpr uint8_t  ADC_SAMPLES = 8;    // усреднение по 8 сэмплам за одно измерение
    static constexpr uint16_t ADC_MAX     = 1023; // 10-бит АЦП
};
