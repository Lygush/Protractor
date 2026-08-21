#pragma once

#include <Arduino.h>
#if defined(USE_MICRO_WIRE)
#include <microWire.h>
#else
#include <Wire.h>
#endif
#include <string.h>
#include "GyverOLED.h"
#include "settings.h"
#include "icons.h"

// Сколько пунктов меню видно одновременно.
// Каждый пункт занимает ЦЕЛУЮ страницу дисплея (8px) под текст + ЦЕЛУЮ
// страницу под зазор — итого 2 страницы на пункт. Экран даёт 7 страниц до
// футера (0..6, футер — страница 7), это ровно 4*2-1=7: item0(стр.0),
// gap(стр.1), item1(стр.2), gap(стр.3), item2(стр.4), gap(стр.5), item3(стр.6).
// Постранично — намеренно: прямая (небуферизованная) отрисовка текста в этой
// библиотеке (GyverOLED, режим OLED_NO_BUFFER) при Y, не кратном 8, на
// реальном экране рисует криво/обрезанно — надёжно работает только Y кратный
// 8. Так зазор получается не "честные" 3-4px, а целая пустая страница (8px),
// зато без риска снова получить кривые строки.
// Было 4 видимых пункта (страницы 0,2,4,6) — последний упирался вплотную в
// футер (страница 7, y=56), выделение почти касалось подписей кнопок.
// 3 пункта (страницы 0,2,4) оставляют два пустых ряда (5,6) перед футером.
static constexpr uint8_t MENU_VISIBLE_ITEMS = 3;
static constexpr uint8_t MENU_ITEM_X0 = 2; // отступ текста от левого края экрана, px

class OLED {
public:
    OLED() = default;
    virtual ~OLED() = default;

    void init(Display_settings* disp_settings);

    // Рисует иконку батареи. blink_visible=false используется для фазы "погашено"
    // при мигании в состоянии LOW_BATTERY (см. вызывающий код в main.cpp).
    void update_battery(bool blink_visible = true, bool force_redraw = false);
    void set_battery_percent(uint8_t percent) { battery_percent = percent; }
    void set_battery_state(BatteryState state) { battery_state = state; }

    virtual void print_base_page() = 0;
    virtual void update_angle_value(float angle) = 0;
    void print_progress_bar(uint8_t actual_section, uint8_t full_bar, const char* text);
    void change_revers();
    bool get_revers() { return revers; }

    // Перерисовывает только левую подпись футера (zero/HOLD) — используется
    // при переключении режима HOLD, чтобы не перерисовывать весь экран.
    void update_hold_indicator(bool held);

    // Включить/выключить сам дисплей (команда SSD1306 DISPLAYON/OFF) —
    // в отличие от clear(), реально гасит матрицу и её питание.
    void set_power(bool on) { oled.setPower(on); }

    // Яркость через контраст SSD1306 — три фиксированные ступени, см. .cpp.
    void apply_brightness(BrightnessLevel level);

    // Общий рендер списка пунктов меню с прокруткой — рисует не более
    // MENU_VISIBLE_ITEMS (3) пунктов при scale=1, каждый на своей странице
    // с пустой страницей-зазором между ними (см. комментарий у
    // MENU_VISIBLE_ITEMS выше). scroll_offset указывает, какой пункт массива
    // отобразить первым, visible_cursor — индекс выделенного пункта в
    // видимой области (0..MENU_VISIBLE_ITEMS-1). editing_value — режим
    // футера: false — листаем список (иконки вверх/вниз), true — меняем
    // значение текущего пункта (иконки влево/вправо) — те же битмапы,
    // что и в индикаторе направления на основном экране.
    void print_menu_page(const char* const lines[], uint8_t line_count,
                          uint8_t scroll_offset, uint8_t visible_cursor,
                          bool editing_value);

    // Экран калибровки: обратный отсчёт (0-9) большими цифрами + инструкция.
    void print_calibration_countdown(uint8_t number);

    // Короткое центрированное сообщение (например "CALIBRATING..."/"DONE").
    void print_calibration_message(const char* msg);

    template<typename T>
    void print_debug(T value, int x, int y, bool clr) {
        if (clr) oled.clear();
        oled.setScale(1);
        oled.setCursor(x, y);
        oled.print(value);
        oled.update();
    }

protected:
    // menu_hold_enters_edit=true — показывать в верхней строке футера
    // (page 6) подпись "edit" для menu_btn (только там, где удержание
    // menu_btn реально что-то делает — MODE_90_TARGET, см. вызов в
    // OLED_Degrees_90_Target::print_base_page()).
    void print_base_page_common(bool menu_hold_enters_edit = false);
    void send_num_buffer(const char* text);

    // Общие кусочки футера, продублированные в print_menu_page() (при
    // scroll-режиме) и в OLED_Degrees_90_Target::draw_target_edit_footer() —
    // вынесены сюда, чтобы не компилировать один и тот же паттерн дважды
    // (при критичном остатке флеша каждый дублированный вызов на счету).
    void draw_updown_footer_icons(); // иконки вверх/вниз в слотах x=0/52, y=56
    void draw_ok_label();            // текст "OK" в слоте x=100, page 7

    // Печатает число как "left" + "." + "right" с точкой на позиции dot_x
    // (в пикселях экрана) — левая часть выравнивается по правому краю строго
    // до этой точки, а не через общий паддинг всей строки. Так минус и все
    // разряды остаются в своих колонках независимо от того, сколько значащих
    // цифр в числе (напр. переход 9.9 -> 10.0 не сдвигает ничего), пока
    // dot_x не меняется. dot_x меняется только при смене раскладки (напр.
    // смена Unit на формат с другим числом целых разрядов), не на каждый кадр.
    void send_split_num_buffer(const char* left, const char* right, int dot_x);

    // dot_x для 2 (Deg) и 1 (Rad) целого разряда — историческое положение,
    // подобранное под ширину "±99.9". Для 3 разрядов (%, мм/м) он не влезает
    // (см. print_unit_value) и используется отдельная, более левая позиция.
    static constexpr int SPLIT_DOT_X_DEFAULT = 55;

    // Общий рендер трёх подписей внизу экрана (позиции x=0/52/100, строка 7).
    // Используется и для базового экрана (zero/mode/menu), и для меню и
    // экрана ввода уставки (^/v/OK) — раньше это был дублированный код в
    // трёх местах с одинаковыми F()-литералами "^"/"v"/"OK".
    void print_footer_labels(const __FlashStringHelper* left,
                              const __FlashStringHelper* mid,
                              const __FlashStringHelper* right);

    GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
    Display_settings* oled_settings{nullptr};
    bool revers{false};

    uint8_t      battery_percent{0};
    BatteryState battery_state{BatteryState::NORMAL};

    // Кэш последней отрисовки батареи — чтобы не дёргать экран, если
    // изменился только процент внутри той же "пятой доли" (0-5 сегментов),
    // а видимая картинка не изменилась бы. -1 = ещё ни разу не рисовали.
    int8_t last_drawn_battery_segments{-1};
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

private:
    // Форматирование под текущий Unit (см. update_angle_value): int_digits —
    // сколько целых разрядов у формата (1=Rad, 2=Deg, 3=%/мм-на-метр),
    // max_abs — граница отсечения (клэмп) для этого формата.
    void print_unit_value(float value, uint8_t int_digits, float max_abs);
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

    // Оверрайд только чтобы после каждой перерисовки числа (send_split_num_buffer
    // очищает всю строку 16..47 через createBuffer) перерисовать поверх нужную
    // стрелку направления — иначе она стиралась бы на следующий кадр. Само
    // форматирование не меняется, вызывается базовая версия.
    void update_angle_value(float angle) override;

    // Вызывается один раз при входе в редактирование — очищает экран и
    // рисует батарею. Футер (подписи кнопок) рисуется отдельно, из
    // update_target_edit() — см. draw_target_edit_footer() ниже, там же
    // почему.
    void print_target_edit_page();

    // Перерисовывает вводимое значение уставки.
    // cursor_pos: 0 = знак, 1 = десятки, 2 = единицы, 3 = десятая доля.
    // digit_visible=false — фаза "погашено" при мигании текущего
    // (редактируемого) разряда — так реализован курсор ввода.
    void update_target_edit(bool negative, uint8_t tens, uint8_t ones, uint8_t dec,
                             uint8_t cursor_pos, bool digit_visible);

    // Стрелка направления к уставке: +1 = наклонить в одну сторону (текущий
    // угол меньше уставки), -1 = в другую, 0 = в допуске (без стрелки).
    // axis — какая ось сейчас измеряется: для X показываем влево/вправо,
    // для Y — вверх/вниз (см. draw_direction_arrow_now()).
    void update_direction_arrow(int8_t direction, MeasureAxis axis);

private:
    // Стрелка направления — справа от числа (число печатается в полосе
    // y=16..47, см. send_split_num_buffer). ARROW_Y обязан быть кратен 8
    // (постранично выровнен) — стрелка рисуется напрямую (drawBitmapRAM без
    // буфера), а прямая отрисовка при Y не кратном 8 в этой библиотеке даёт
    // "мигающую"/битую картинку (та же причина, по которой пункты меню
    // тоже сделаны строго постранично — см. MENU_VISIBLE_ITEMS в oled.h).
    // x=100 подобран так, чтобы не пересекаться с самым частым форматом
    // (Deg, dot_x=55, число заканчивается около x=92); для трёхразрядных
    // форматов (%, мм/м, dot_x=74, до x=110) стрелка перерисовывается поверх
    // каждый кадр в update_angle_value(), так что перекрытия не остаётся.
    // ARROW_W/ARROW_H — размер области clear()+update() под стрелку, взят
    // как максимум из обеих пар иконок (up/down 7x8, left/right 8x7-round-8),
    // чтобы очищать с запасом независимо от того, какая пара сейчас активна.
    static constexpr int ARROW_X = 100, ARROW_Y = 24, ARROW_W = 8, ARROW_H = 8;
    static constexpr int ARROW_UD_W = 7; // фактическая ширина иконок вверх/вниз
    static constexpr int ARROW_LR_W = 8; // фактическая ширина иконок влево/вправо

    int8_t last_direction{0};
    MeasureAxis last_axis{MeasureAxis::X};
    void draw_direction_arrow_now();

    // Футер экрана ввода уставки: те же иконки вверх/вниз (zero/mode -
    // инкремент/декремент разряда), что и везде, а справа (menu_btn) —
    // стрелка вправо (переход к следующему разряду) везде, КРОМЕ последнего
    // разряда (cursor_pos == 3, десятая доля) — там дальше двигаться
    // некуда, следующий клик menu_btn уже сохраняет и выходит, поэтому
    // вместо стрелки показываем текст "OK". last_edit_cursor — чтобы не
    // перерисовывать футер (и не дёргать SPI за иконками) на каждый тик
    // мигания цифры, а только когда реально сменился разряд; 0xFF —
    // сентинел, гарантирующий отрисовку на первом вызове.
    uint8_t last_edit_cursor{0xFF};
    void draw_target_edit_footer(uint8_t cursor_pos);
};
