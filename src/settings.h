#pragma once

#include <Arduino.h>

enum DisplayMode {
    MODE_90,
    MODE_90_TARGET,   // режим 90° + уставка целевого угла (звук/светодиод при достижении)
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
    uint8_t calibration_strength{10}; //Максимум 10
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

struct Sleep_settings
{
    // Через сколько мс бездействия (угол не меняется, см. SleepManager) уходим в сон
    uint32_t inactivity_timeout_ms{30000};

    // Допустимый дрейф угла (±градусы) от "якорной" точки, не считающийся
    // движением — гасит болтанку у целочисленной границы (напр. 1.9<->2.0),
    // из-за которой таймер бездействия мог сбрасываться на дрейфе, а не на
    // реальном движении.
    float stillness_threshold_deg{0.5f};
};

// Настройки режима MODE_90_TARGET (уставка целевого угла + звук/светодиод
// при достижении). Диапазон значения — тот же, что и в обычном режиме 90°
// (±99.9°), т.к. использует тот же экран отображения угла.
struct Target_settings
{
    float target_angle_deg{0.0f};   // уставка, вводится пользователем на экране
    float tolerance_deg{1.0f};      // |текущий угол - уставка| <= допуск -> "достигнуто"
};

class Settings 
{
public:
    Settings() = default;
    ~Settings() = default;

    Mpu_settings*     get_mpu_settings();
    Display_settings* get_display_settings();
    Battery_settings* get_battery_settings();
    Sleep_settings*   get_sleep_settings();
    Target_settings*  get_target_settings();

private:
    Display_settings oled_settings;
    Mpu_settings mpu_settings;
    Battery_settings battery_settings;
    Sleep_settings sleep_settings;
    Target_settings target_settings;
};
