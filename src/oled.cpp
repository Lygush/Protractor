#include "oled.h"

//////////////// OLED BASE CLASS /////////////////////

void OLED::init(Display_settings *disp_settings)
{
    oled.init();
    delay(100);
    this->change_revers();
    oled.clear();
    oled.setCursor(10, 4);
    oled.setContrast(0);
    oled.update();
    delay(500);
    oled.setContrast(100);
    oled.clear();
    oled.update();
    oled_settings = disp_settings;
}

void OLED::update_battery(bool blink_visible)
{
    const int x = 0, y = 0;
    const int w = 25, h = 12;

    if (battery_state == BatteryState::LOW_BATTERY && !blink_visible) {
        oled.clear(x, y, x + w, y + h);
        oled.update(x, y, x + w, y + h);
        // При следующей фазе "включено" нужно гарантированно перерисовать
        // иконку, даже если число сегментов не изменилось.
        last_drawn_battery_segments = -1;
        return;
    }

    // 0..5 сегментов, пропорционально проценту заряда (каждый сегмент = 20%),
    // с округлением к ближайшему: 0-9%->0, 10-29%->1, ..., 90-100%->5.
    uint8_t segments_lit = (uint16_t(battery_percent) * 5 + 50) / 100;
    if (segments_lit > 5) segments_lit = 5;

    // Экран не трогаем, если картинка не изменится — так апдейт (вызываемый
    // на любое изменение процента) не дёргает дисплей, пока не изменится
    // именно видимое число сегментов.
    if (segments_lit == last_drawn_battery_segments) return;
    last_drawn_battery_segments = segments_lit;

    oled.clear(x, y, x + w, y + h);

    // Контур рисуется всегда — он же определяет "пустые" (незажжённые) слоты.
    oled.drawBitmap(x, y, battery_outline_25x12, w, h);

    static constexpr uint8_t SEG_W    = 3;
    static constexpr uint8_t SEG_H    = 12; // одна страница — см. комментарий у battery_segment_3x8
    static constexpr uint8_t SEG_X0   = 2; // первый слот, с 1px отступом от рамки
    static constexpr uint8_t SEG_STEP = SEG_W + 1; // ширина сегмента + 1px зазор

    for (uint8_t i = 0; i < segments_lit; i++) {
        int seg_x = x + SEG_X0 + i * SEG_STEP;
        oled.drawBitmap(seg_x, y, battery_segment_3x12, SEG_W, SEG_H);
    }

    oled.update(x, y, x + w, y + h);
}

// Общие подписи внизу экрана (x=0/52/100, строка 7) — раньше три одинаковых
// setCursor+print были продублированы в print_base_page_common(),
// print_menu_page() и print_target_edit_page().
void OLED::print_footer_labels(const __FlashStringHelper* left,
                                const __FlashStringHelper* mid,
                                const __FlashStringHelper* right)
{
    oled.setCursor(0, 7);
    oled.print(left);
    oled.setCursor(52, 7);
    oled.print(mid);
    oled.setCursor(100, 7);
    oled.print(right);
}

void OLED::print_base_page_common()
{
    update_battery();
    oled.setScale(1);
    print_footer_labels(F("zero"), F("mode"), F("menu"));
    oled.update();
}

void OLED::print_progress_bar(uint8_t actual_section, uint8_t full_bar, const char* text)
{
    static constexpr int text_x0 = 0, text_y0 = 8, text_x1 = 127, text_y1 = 15;

    if (actual_section == 0) {
        oled.clear();
    } else {
        oled.clear(text_x0, text_y0, text_x1, text_y1);
    }
    oled.setCursor(0, 1);
    oled.setScale(1);
    oled.print(F("Calibrate "));
    oled.print(text);
    oled.update();

    if (actual_section == 0) {
        oled.rect(3, 25, 125, 45, OLED_STROKE);
        oled.update();
    } else {
        int right_edge = 4 + (120 * (actual_section + 1)) / full_bar;
        oled.rect(4, 25, right_edge, 45);
        oled.update();
    }
}

void OLED::change_revers()
{
    revers = !revers;
    oled.flipV(revers);
    oled.flipH(revers);
}

void OLED::apply_brightness(BrightnessLevel level)
{
    uint8_t contrast;
    switch (level) {
        case BrightnessLevel::DIM:    contrast = 20;  break;
        case BrightnessLevel::MEDIUM: contrast = 120; break;
        case BrightnessLevel::BRIGHT:
        default:                      contrast = 255; break;
    }
    oled.setContrast(contrast);
}

// Исправленная функция меню с инверсией и подписями кнопок
void OLED::print_menu_page(const char* const lines[], uint8_t line_count, uint8_t cursor_index)
{
    oled.clear();
    oled.setScale(1);

    for (uint8_t i = 0; i < line_count; i++) {
        oled.setCursor(0, i);
        if (i == cursor_index) {
            oled.invertText(true);
            oled.print(lines[i]);
            oled.invertText(false);
        } else {
            oled.print(lines[i]);
        }
    }

    // Подписи кнопок внизу (строка 7)
    print_footer_labels(F("^"), F("v"), F("OK"));
    oled.update();
}

void OLED::print_calibration_countdown(uint8_t number)
{
    oled.clear();
    oled.setScale(1);
    oled.setCursor(7, 1);
    oled.print(F("Place flat & still"));
    oled.update();

    char buf[2];
    buf[0] = char('0' + number);
    buf[1] = '\0';
    send_num_buffer(buf);
}

void OLED::print_calibration_message(const char* msg)
{
    oled.clear();
    oled.setScale(1);
    // Просто печатаем по центру (грубо, без strlen)
    oled.setCursor(20, 3);
    oled.print(msg);
    oled.update();
}

void OLED::send_num_buffer(const char* text) {
    static constexpr int bufX0 = 0, bufY0 = 16, bufX1 = 127, bufY1 = 47;
    static constexpr int vOffset = 3;

    bool ok = oled.createBuffer(bufX0, bufY0, bufX1, bufY1, 0);
    if (!ok) return;

    oled.setScale(3);
    int len = strlen(text);
    int x = (128 - len * 6 * 3) / 2;
    int y = bufY0 + (32 - 8 * 3) / 2 + vOffset;
    oled.setCursorXY(x, y);
    oled.print(text);

    oled.sendBuffer();
    oled.setWindow(0, 0, 127, 7);
}

void OLED::send_split_num_buffer(const char* left, const char* right) {
    static constexpr int bufX0 = 0, bufY0 = 16, bufX1 = 127, bufY1 = 47;
    static constexpr int vOffset = 3;
    static constexpr int CHAR_W = 6 * 3;
    static constexpr int DOT_X  = (128 - CHAR_W) / 2;

    bool ok = oled.createBuffer(bufX0, bufY0, bufX1, bufY1, 0);
    if (!ok) return;

    oled.setScale(3);
    int y = bufY0 + (32 - 8 * 3) / 2 + vOffset;

    oled.setCursorXY(DOT_X - (int)strlen(left) * CHAR_W, y);
    oled.print(left);

    oled.setCursorXY(DOT_X, y);
    oled.print(F("."));

    oled.setCursorXY(DOT_X + CHAR_W, y);
    oled.print(right);

    oled.sendBuffer();
    oled.setWindow(0, 0, 127, 7);
}

//////////////// OLED DEGREE 360 MODE /////////////////////

void OLED_Degrees_360::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.drawBitmap(47, 0, mode_360_34x14, 34, 14);
    oled.drawBitmap(110, 0, circle_15x15, 15, 15);
    oled.update();
}

void OLED_Degrees_360::update_angle_value(float angle)
{
    angle = degrees(angle);
    if (angle < 0) angle += 360;

    int deg = (int)(angle + 0.5f);
    if (deg >= 360) deg = 0;

    char buf[4];
    buf[0] = (deg >= 100) ? char('0' + deg / 100)       : ' ';
    buf[1] = (deg >= 10)  ? char('0' + (deg / 10) % 10) : ' ';
    buf[2] = char('0' + deg % 10);
    buf[3] = '\0';
    send_num_buffer(buf);
}

//////////////// OLED DEGREE 90 MODE /////////////////////

void OLED_Degrees_90::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.drawBitmap(52, 0, mode_90_24x14, 24, 14);
    oled.drawBitmap(110, 0, circle_15x15, 15, 15);
    oled.update();
}

void OLED_Degrees_90::update_angle_value(float angle)
{
    angle = degrees(angle);
    angle = -angle;
    angle = constrain(angle, -99.9f, 99.9f);

    bool negative = angle < 0;
    float a = fabs(angle);

    int whole = (int)a;
    int dec = (int)((a - whole) * 10.0f + 0.5f);
    if (dec >= 10) { dec = 0; whole++; }

    if (whole == 0 && dec == 0) negative = false;

    int tens = whole / 10;
    int ones = whole % 10;

    char left[4];
    left[0] = negative ? '-' : ' ';
    left[1] = (tens > 0) ? char('0' + tens) : ' ';
    left[2] = char('0' + ones);
    left[3] = '\0';

    char right[2];
    right[0] = char('0' + dec);
    right[1] = '\0';

    send_split_num_buffer(left, right);
}

//////////////// OLED RADIANS MODE /////////////////////

void OLED_Radians::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.setCursor(113, 1);
    oled.setScale(1);
    oled.print(F("PI"));
    oled.update();
}

void OLED_Radians::update_angle_value(float angle)
{
    bool negative = angle < 0;
    float a = fabs(angle);

    int whole = (int)a;
    int dec = (int)((a - whole) * 10.0f + 0.5f);
    if (dec >= 10) { dec = 0; whole++; }
    if (whole > 9) whole = 9;

    if (whole == 0 && dec == 0) negative = false;

    char left[3];
    left[0] = negative ? '-' : ' ';
    left[1] = char('0' + whole);
    left[2] = '\0';

    char right[2];
    right[0] = char('0' + dec);
    right[1] = '\0';

    send_split_num_buffer(left, right);
}

//////////////// OLED DEGREE 90 + TARGET MODE /////////////////////

void OLED_Degrees_90_Target::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.setScale(1);
    oled.setCursor(46, 1);
    oled.print(F("90 TGT")); //показывать целевой угол.
    oled.drawBitmap(110, 0, circle_15x15, 15, 15);
    oled.update();
}

void OLED_Degrees_90_Target::print_target_edit_page()
{
    oled.clear();
    update_battery();
    oled.setScale(1);
    print_footer_labels(F("^"), F("v"), F("OK"));
    oled.update();
}

void OLED_Degrees_90_Target::update_target_edit(bool negative, uint8_t tens, uint8_t ones, uint8_t dec,
                                                 uint8_t cursor_pos, bool digit_visible)
{
    char sign_ch = negative ? '-' : '+';
    char tens_ch = char('0' + tens);
    char ones_ch = char('0' + ones);
    char dec_ch  = char('0' + dec);

    if (!digit_visible) {
        switch (cursor_pos) {
            case 0: sign_ch = ' '; break;
            case 1: tens_ch = ' '; break;
            case 2: ones_ch = ' '; break;
            case 3: dec_ch  = ' '; break;
        }
    }

    char left[4]  = { sign_ch, tens_ch, ones_ch, '\0' };
    char right[2] = { dec_ch, '\0' };

    send_split_num_buffer(left, right);
}