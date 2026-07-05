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

void OLED::print_start_page()
{
    // Область "глаза" на морде лягушки, которым моргаем (rect = открыт, line = закрыт).
    static constexpr int eye_x0 = 80, eye_y0 = 16, eye_x1 = 82, eye_y1 = 18;

    oled.clear();
    oled.update();
    oled.drawBitmap(32, 10, frog_64x45, 64, 45);
    oled.rect(eye_x0, eye_y0, eye_x1, eye_y1);
    oled.update();
    delay(400);

    // Полная область глаза очищается перед каждой перерисовкой (а не только
    // часть с линией, как было раньше) — иначе не совпадающая по размеру
    // область clear() могла оставлять лишние пиксели поверх реально
    // перерисовываемой зоны.
    oled.clear(eye_x0, eye_y0, eye_x1, eye_y1);
    oled.line(eye_x0, 17, eye_x1, 17);
    oled.update(eye_x0, eye_y0, eye_x1, eye_y1);
    delay(400);

    oled.clear(eye_x0, eye_y0, eye_x1, eye_y1);
    oled.rect(eye_x0, eye_y0, eye_x1, eye_y1);
    oled.update(eye_x0, eye_y0, eye_x1, eye_y1);
    delay(400);
}

// Отрисовка батареи готовыми иконками (8x8, см. icons.h) вместо ручной
// геометрии — сильно легче по коду, чем прежний вариант через rect()/line():
// один drawBitmap() вместо цикла отрисовки сегментов + отдельная логика молнии.
// Иконка высотой 12px реально пересекает границу страниц дисплея (страница 0:
// y0-7, страница 1: y8-11) — GyverOLED сам делает два прохода записи, это не
// требует отдельной обработки с нашей стороны.
//
// blink_visible=false — фаза "погашено" при мигании в LOW_BATTERY: область
// просто очищается, ничего не рисуем.
//
// ПРИМЕЧАНИЕ: отдельной иконки для CHARGING сейчас нет — используется обычная
// иконка по проценту. Если понадобится визуально отличать зарядку, можно
// нарисовать ещё одну 8x8 иконку (например, с "+") и добавить сюда ветку —
// это тот же drawBitmap(), просто с другим указателем.
void OLED::update_battery(bool blink_visible)
{
    const int x = 0, y = 0;                 // ровно страница 0 — без сдвигов
    const int w = 27, h = 12;

    oled.clear(x, y, x + w, y + h);

    if (battery_state == BatteryState::LOW_BATTERY && !blink_visible) {
        oled.update(x, y, x + w, y + h);
        return;                              // фаза "погашено" при мигании — пусто
    }

    const uint8_t* icon;
    if      (battery_percent >= 90) icon = battery_100_27x12;
    else if (battery_percent >= 65) icon = battery_75_27x12;
    else if (battery_percent >= 40) icon = battery_50_27x12;
    else if (battery_percent >= 15) icon = battery_25_27x12;
    else                             icon = battery_0_27x12;

    oled.drawBitmap(x, y, icon, w, h);
    oled.update(x, y, x + w, y + h);
}

void OLED::print_base_page_common()
{
    update_battery();                       // рисуем батарею с актуальным процентом
    oled.setScale(1);
    oled.setCursor(0, 7);
    oled.print(F("zero"));
    oled.setCursor(100, 7);
    oled.print(F("menu"));
    oled.update();
}

void OLED::print_progress_bar(uint8_t actual_section, uint8_t full_bar, const char* text)
{
    // Строка текста "Calibrate ..." — page 1 (y 8-15 в пикселях).
    static constexpr int text_x0 = 0, text_y0 = 8, text_x1 = 127, text_y1 = 15;

    if (actual_section == 0) {
        oled.clear();
    } else {
        // Очищаем строку с текстом перед каждой перерисовкой — иначе если новый
        // текст короче предыдущего (например, "Gyro" после "Accel"), хвост
        // старой надписи оставался бы виден.
        oled.clear(text_x0, text_y0, text_x1, text_y1);
    }
    oled.setCursor(0, 1);
    oled.setScale(1);
    oled.print(F("Calibrate "));
    oled.print(text);
    oled.update();

    if (actual_section == 0) {
        // Было три вложенных roundRect со сдвигом в 1px (имитация толстой рамки) —
        // скруглённые углы трёх разных по размеру прямоугольников не совпадали
        // друг с другом и давали на стыках непонятные "чёрточки". Обычный rect()
        // без скругления рисует ровную рамку без этого артефакта.
        oled.rect(3, 25, 125, 45, OLED_STROKE);
        oled.update();
    } else {
        // Правый край считаем пропорционально от общей ширины (120px) на
        // каждый вызов, а не накоплением через целочисленный
        // segment_lenght = 120/full_bar. Целочисленное деление округляет
        // вниз, и если full_bar не делит 120 нацело, бар на последнем шаге
        // не дотягивался до правой границы (124) — визуально "обрезан".
        int right_edge = 4 + (120 * (actual_section + 1)) / full_bar;

        // rect() вместо roundRect(): на небольшой высоте (20px), особенно
        // при маленькой ширине в начале калибровки, скругление угла у
        // roundRect() съедало пиксели прямо у края заливки — бар выглядел
        // так, будто не доходит ни до начала, ни до конца. Обычный
        // прямоугольник без скругления заливает ровно от x=4 до right_edge.
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

// Отправка текста в зону чисел (страницы 2-5, высота 32 пикселя)
void OLED::send_num_buffer(const String& text) {
    static constexpr int bufX0 = 0, bufY0 = 16, bufX1 = 127, bufY1 = 47;
    static constexpr int vOffset = 3;

    bool ok = oled.createBuffer(bufX0, bufY0, bufX1, bufY1, 0);
    if (!ok) return;

    oled.setScale(3);
    int len = text.length();
    int x = (128 - len * 6 * 3) / 2;
    int y = bufY0 + (32 - 8 * 3) / 2 + vOffset;
    oled.setCursorXY(x, y);
    oled.print(text);

    oled.sendBuffer();
    oled.setWindow(0, 0, 127, 7);
}

//////////////// OLED DEGREE 360 MODE /////////////////////

void OLED_Degrees_360::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.drawBitmap(47, 0, mode_360_34x14, 34, 14);
    oled.update();
}

void OLED_Degrees_360::update_angle_value(float angle)
{
    angle = degrees(angle);
    if (angle < 0) angle += 360;
    String str = String(angle, 1);
    send_num_buffer(str);
}

int OLED_Degrees_360::alignment(float angle_f) { return 0; }

//////////////// OLED DEGREE 90 MODE /////////////////////

void OLED_Degrees_90::print_base_page()
{
    oled.clear();
    print_base_page_common();
    oled.drawBitmap(47, 0, mode_left_90_24x14, 24, 14);
    oled.update();
}

void OLED_Degrees_90::update_angle_value(float angle)
{
    angle = degrees(angle);
    angle = -angle;
    angle = constrain(angle, -99, 99);

    String str;
    if (angle < 0) {
        str = "-" + String(-angle, 1);
    } else {
        str = String(angle, 1);
    }
    send_num_buffer(str);
}

int OLED_Degrees_90::alignment(float angle_f) { return 0; }

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
    String str;
    if (angle < 0) {
        str = "-" + String(-angle, 1);
    } else {
        str = String(angle, 1);
    }
    send_num_buffer(str);
}

int OLED_Radians::alignment(float angle_f) { return 0; }
