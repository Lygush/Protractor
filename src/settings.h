#pragma once

#include <Arduino.h>

enum DisplayMode {
    MODE_90,
    MODE_360,
    MODE_RADIANS
};

struct Display_settings
{
    bool revers{false};
};

// Состояние батареи. Лежит в settings.h (а не в battery.h), чтобы им мог
// пользоваться и OLED (для отрисовки), и Battery — без циклических инклюдов.
//
// ВАЖНО: не называть значение "LOW" или "HIGH" — Arduino.h определяет их как
// макросы (#define LOW 0x0 / #define HIGH 0x1), и препроцессор молча
// подставит числа прямо в enum, сломав компиляцию.
enum class BatteryState : uint8_t {
    NORMAL,        // обычный режим — просто показываем текущий процент
    LOW_BATTERY,   // батарея разряжена (с учётом гистерезиса)
    CHARGING       // похоже, что идёт зарядка (эвристика по тренду напряжения)
};

struct Mpu_settings
{
    uint8_t calibration_strenght{10}; //Максимум 10
};

struct Battery_settings
{
    uint8_t pin{A1};

    // Делитель напряжения: Vbat --[R1]-- (точка измерения) --[R2]-- GND
    float r1_kohm{3.315f};
    float r2_kohm{6.820f};
    float vref{5.0f};          // опорное напряжение АЦП

    // Диапазон напряжения Li-ion/LiPo (1S) для пересчёта в проценты
    float v_empty{3.30f};      // 0%
    float v_full{4.20f};       // 100%

    // Гистерезис для состояния LOW (в процентах), чтобы не дёргалось туда-сюда
    uint8_t low_enter_percent{10};   // при <= этого % входим в LOW
    uint8_t low_exit_percent{15};    // при >= этого % выходим обратно в NORMAL

    // Эвристика определения зарядки по тренду напряжения (нет отдельного STAT-пина)
    float   charge_trend_threshold_v{0.015f}; // мин. прирост EMA-напряжения за окно, чтобы заподозрить зарядку
    uint8_t charge_confirm_count{3};          // сколько подряд окон роста нужно для входа в CHARGING
    uint8_t discharge_confirm_count{2};       // сколько подряд окон без роста нужно для выхода из CHARGING
};


class Settings 
{
public:
    Settings() = default;
    ~Settings() = default;

    Mpu_settings*     get_mpu_settings();
    Display_settings*  get_display_settings();
    Battery_settings*  get_battery_settings();

private:
    Display_settings oled_settings;
    Mpu_settings mpu_settings;
    Battery_settings battery_settings;
};
