#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include "GyverOLED.h"
#include "settings.h"
#include "icons.h"

class OLED {
public:
    OLED() = default;
    virtual ~OLED() = default;

    void init(Display_settings* disp_settings);
    void print_start_page();

    // Рисует иконку батареи. blink_visible=false используется для фазы "погашено"
    // при мигании в состоянии LOW_BATTERY (см. вызывающий код в main.cpp).
    void update_battery(bool blink_visible = true);
    void set_battery_percent(uint8_t percent) { battery_percent = percent; }
    void set_battery_state(BatteryState state) { battery_state = state; }

    virtual void print_base_page() = 0;
    virtual void update_angle_value(float angle) = 0;
    void print_progress_bar(uint8_t actual_section, uint8_t full_bar, const char* text);
    void change_revers();
    bool get_revers() { return revers; }

    // Включить/выключить сам дисплей (команда SSD1306 DISPLAYON/OFF) —
    // в отличие от clear(), реально гасит матрицу и её питание.
    void set_power(bool on) { oled.setPower(on); }

    template<typename T>
    void print_debug(T value, int x, int y, bool clr) {
        if (clr) oled.clear();
        oled.setScale(1);
        oled.setCursor(x, y);
        oled.print(value);
        oled.update();
    }

protected:
    void print_base_page_common();
    void send_num_buffer(const char* text);

    // Печатает число как "left" + "." + "right" с точкой на фиксированной
    // позиции экрана (см. .cpp) — левая часть выравнивается по правому краю
    // строго до этой точки, а не через общий паддинг всей строки. Так минус
    // и все разряды остаются в своих колонках независимо от того, сколько
    // значащих цифр в числе (напр. переход 9.9 -> 10.0 не сдвигает ничего).
    void send_split_num_buffer(const char* left, const char* right);

    GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
    Display_settings* oled_settings{nullptr};
    bool revers{false};

    uint8_t      battery_percent{0};
    BatteryState battery_state{BatteryState::NORMAL};
};

class OLED_Degrees_360 : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
};

class OLED_Degrees_90 : public OLED {
public:
    OLED_Degrees_90() = default;
    void print_base_page() override;
    void update_angle_value(float angle) override;
};

// Режим 90° + уставка целевого угла. update_angle_value() наследуется как
// есть (тот же экран отображения угла), добавляется:
// - своя print_base_page() — метка "90 TGT" вместо иконки режима;
// - экран ввода уставки (print_target_edit_page() + update_target_edit()),
//   в него переходят только из main.cpp по удержанию menu_btn, когда
//   current_mode == MODE_90_TARGET.
class OLED_Degrees_90_Target : public OLED_Degrees_90 {
public:
    void print_base_page() override;

    // Вызывается один раз при входе в редактирование — рисует подписи
    // кнопок (▲/▼/OK вместо zero/mode/menu). Иконок стрелок пока нет,
    // используются символы "^"/"v" как временная замена.
    void print_target_edit_page();

    // Перерисовывает вводимое значение уставки.
    // cursor_pos: 0 = знак, 1 = десятки, 2 = единицы, 3 = десятая доля.
    // digit_visible=false — фаза "погашено" при мигании текущего
    // (редактируемого) разряда — так реализован курсор ввода.
    void update_target_edit(bool negative, uint8_t tens, uint8_t ones, uint8_t dec,
                             uint8_t cursor_pos, bool digit_visible);
};

class OLED_Radians : public OLED {
public:
    void print_base_page() override;
    void update_angle_value(float angle) override;
};
