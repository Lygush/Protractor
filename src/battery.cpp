#include "battery.h"

void Battery::init(Battery_settings* s) {
    settings = s;
    pinMode(settings->pin, INPUT);

    // Сразу делаем первое измерение, чтобы не показывать 0%/мусор
    // до первого срабатывания таймера в update().
    voltage = adc_to_voltage(read_adc_averaged());
    voltage_ema = voltage;
    voltage_ema_prev_window = voltage;
    ema_initialized = true;
    percent = voltage_to_percent(voltage);

    state = (percent <= settings->low_enter_percent) ? BatteryState::LOW_BATTERY : BatteryState::NORMAL;

    uint32_t now = millis();
    last_measure_ms = now;
    last_trend_check_ms = now;
}

uint16_t Battery::read_adc_averaged() {
    uint32_t sum{0};
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(settings->pin);
    }
    return sum / ADC_SAMPLES;
}

float Battery::adc_to_voltage(uint16_t adc_raw) const {
    // Напряжение непосредственно на входе АЦП (после делителя)
    float v_adc = (adc_raw * settings->vref) / (float)ADC_MAX;

    // Пересчёт обратно к напряжению батареи: Vbat = Vadc * (R1+R2)/R2
    float divider_ratio = (settings->r1_kohm + settings->r2_kohm) / settings->r2_kohm;
    return v_adc * divider_ratio;
}

uint8_t Battery::voltage_to_percent(float v) const {
    // Кусочно-линейная аппроксимация разрядной кривой Li-ion/LiPo (1S, под небольшой нагрузкой).
    // При желании подгони точки под свой конкретный аккумулятор.
    //
    // static: без него компилятор на каждый вызов функции заново собирал бы
    // весь массив на стеке (копирование 11 структур), что заметно раздувало
    // флеш — эта функция дёргается на каждое измерение. Со static инициализация
    // происходит один раз (значения v_empty/v_full не константны на этапе
    // компиляции — они из settings), а дальше просто читаем готовый массив.
    // ВАЖНО: static-переменная общая для всех экземпляров Battery (в проекте
    // он один глобальный — не проблема). Если когда-нибудь понадобится
    // несколько батарей с разными v_empty/v_full — эту static-таблицу придётся
    // убрать обратно в обычный member или сделать per-instance.
    struct Point { float v; uint8_t pct; };
    static const Point curve[] = {
        {settings->v_empty,  0},
        {3.50f,  10},
        {3.60f,  20},
        {3.65f,  30},
        {3.70f,  40},
        {3.75f,  50},
        {3.80f,  60},
        {3.90f,  70},
        {3.95f,  80},
        {4.05f,  90},
        {settings->v_full,  100},
    };
    const uint8_t n = sizeof(curve) / sizeof(curve[0]);

    if (v <= curve[0].v)     return curve[0].pct;
    if (v >= curve[n - 1].v) return curve[n - 1].pct;

    for (uint8_t i = 0; i < n - 1; i++) {
        if (v >= curve[i].v && v <= curve[i + 1].v) {
            float t = (v - curve[i].v) / (curve[i + 1].v - curve[i].v);
            return curve[i].pct + (uint8_t)(t * (curve[i + 1].pct - curve[i].pct) + 0.5f);
        }
    }
    return 0; // недостижимо
}

// Гистерезис: входим в LOW при <= low_enter_percent, выходим только при >= low_exit_percent.
// Это не даёт индикатору дёргаться туда-сюда на границе, если процент колеблется на 1 пункт.
// Пока идёт CHARGING — LOW не трогаем, состояние зарядки приоритетнее.
void Battery::update_low_state() {
    if (state == BatteryState::CHARGING) return;

    if (state == BatteryState::NORMAL && percent <= settings->low_enter_percent) {
        state = BatteryState::LOW_BATTERY;
    } else if (state == BatteryState::LOW_BATTERY && percent >= settings->low_exit_percent) {
        state = BatteryState::NORMAL;
    }
}

// Эвристика зарядки: раз в TREND_WINDOW_MS сравниваем текущее сглаженное напряжение
// с тем, что было окно назад. Устойчивый рост несколько окон подряд -> CHARGING.
// Устойчивое отсутствие роста несколько окон подряд -> выходим из CHARGING обратно
// в NORMAL/LOW (по факту текущего процента).
void Battery::update_charging_state() {
    uint32_t now = millis();
    if (now - last_trend_check_ms < TREND_WINDOW_MS) return;
    last_trend_check_ms = now;

    float delta = voltage_ema - voltage_ema_prev_window;
    voltage_ema_prev_window = voltage_ema;

    if (delta >= settings->charge_trend_threshold_v) {
        if (rising_windows < 255) rising_windows++;
        falling_windows = 0;
    } else {
        if (falling_windows < 255) falling_windows++;
        rising_windows = 0;
    }

    if (state != BatteryState::CHARGING && rising_windows >= settings->charge_confirm_count) {
        state = BatteryState::CHARGING;
        falling_windows = 0;
    } else if (state == BatteryState::CHARGING && falling_windows >= settings->discharge_confirm_count) {
        state = (percent <= settings->low_enter_percent) ? BatteryState::LOW_BATTERY : BatteryState::NORMAL;
        rising_windows = 0;
    }
}

bool Battery::update() {
    uint32_t now = millis();
    if (now - last_measure_ms < MEASURE_INTERVAL_MS) return false;
    last_measure_ms = now;

    float v_raw = adc_to_voltage(read_adc_averaged());

    if (!ema_initialized) {
        voltage_ema = v_raw;
        ema_initialized = true;
    } else {
        voltage_ema += EMA_ALPHA * (v_raw - voltage_ema);
    }

    voltage = voltage_ema;
    percent = voltage_to_percent(voltage);

    update_charging_state();  // может выставить CHARGING
    update_low_state();       // гистерезис NORMAL/LOW (не трогает CHARGING)

    bool changed = (percent != last_reported_percent) || (state != last_reported_state);
    if (changed) {
        last_reported_percent = percent;
        last_reported_state = state;
    }
    return changed;
}
