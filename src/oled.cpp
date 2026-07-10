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
   // oled.drawBitmap(32, 10, frog_64x45, 64, 45);
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
    oled.setCursor(52, 7);                  // (128 - 4 симв. * 6px) / 2 = 52 — центр строки
    oled.print(F("mode"));
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

// Печать "left.right" с точкой строго по центру экрана. left печатается
// заканчиваясь ровно в DOT_X (право-выравнивание до точки), right — сразу
// после неё. За счёт этого точка и минус всегда на одном и том же месте,
// независимо от того, сколько цифр реально показывается слева/справа —
// в отличие от send_num_buffer(), где паддинг общий на всю строку и при
// смене числа значащих цифр сдвигает даже те символы, которые не менялись.
//
// const char* вместо String — не тянем за собой класс String целиком
// (динамическая память/конкатенации), которого больше нигде в проекте нет.
void OLED::send_split_num_buffer(const char* left, const char* right) {
    static constexpr int bufX0 = 0, bufY0 = 16, bufX1 = 127, bufY1 = 47;
    static constexpr int vOffset = 3;
    static constexpr int CHAR_W = 6 * 3;              // ширина символа при setScale(3)
    static constexpr int DOT_X  = (128 - CHAR_W) / 2; // точка по центру экрана

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
    oled.update();
}

void OLED_Degrees_360::update_angle_value(float angle)
{
    angle = degrees(angle);
    if (angle < 0) angle += 360;

    // Целые градусы (десятые больше не нужны) + фиксированная ширина 3 символа
    // (dtostrf выравнивает по правому краю пробелами) — иначе при переходе
    // между 1/2/3-значными числами текст "прыгал" из-за перецентровки
    // в send_num_buffer() по длине строки.
    int deg = (int)(angle + 0.5f);
    if (deg >= 360) deg = 0;   // округление могло дать 360 у самой границы

    char buf[5];
    dtostrf((float)deg, 3, 0, buf);
    send_num_buffer(buf);
}

//////////////// OLED DEGREE 90 MODE /////////////////////

void OLED_Degrees_90::print_base_page()
{
    oled.clear();
    print_base_page_common();
    // Иконка 24px шириной: (128-24)/2 = 52. Раньше стояло 47 (значение,
    // верное для 34px-иконки режима 360) — из-за этого иконка была смещена влево.
    oled.drawBitmap(52, 0, mode_90_24x14, 24, 14);
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
    if (dec >= 10) { dec = 0; whole++; }   // перенос разряда из-за округления

    if (whole == 0 && dec == 0) negative = false;  // не показываем "-0.0"

    int tens = whole / 10;
    int ones = whole % 10;

    // Фиксированные колонки: [знак][десятки][единицы] — пробел вместо
    // отсутствующего знака/десятков, а не убранная позиция. Так минус и
    // все цифры всегда в одних и тех же местах на экране, в т.ч. когда
    // число переходит с одной значащей цифры на две (9.9 -> 10.0).
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

    int whole = (int)a;                    // диапазон ypr — доли пи, одна цифра до точки
    int dec = (int)((a - whole) * 10.0f + 0.5f);
    if (dec >= 10) { dec = 0; whole++; }   // перенос разряда из-за округления
    if (whole > 9) whole = 9;              // защитный клэмп на случай выхода за диапазон

    if (whole == 0 && dec == 0) negative = false;  // не показываем "-0.0"

    // Фиксированные колонки: [знак][цифра] — та же логика, что и в режиме 90:
    // минус и точка не двигаются независимо от значения.
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
    // Своей иконки для этого режима пока нет (сделаешь позже) — временно
    // текстовая метка по центру верхней области, там же, где иконка режима
    // в остальных вариантах. "90 TGT" — 6 символов * 6px (scale1) = 36px,
    // центр (128-36)/2 = 46.
    oled.setScale(1);
    oled.setCursor(46, 1);
    oled.print(F("90 TGT"));
    oled.update();
}

void OLED_Degrees_90_Target::print_target_edit_page()
{
    oled.clear();
    update_battery();
    oled.setScale(1);
    // Те же позиции, что у "zero"/"mode"/"menu" в print_base_page_common() —
    // над теми же физическими кнопками (zero_btn=▲, mode_btn=▼, menu_btn=OK).
    // Настоящих значков стрелок нет — "^"/"v" как временная замена.
    oled.setCursor(0, 7);
    oled.print(F("^"));
    oled.setCursor(52, 7);
    oled.print(F("v"));
    oled.setCursor(100, 7);
    oled.print(F("OK"));
    oled.update();
}

void OLED_Degrees_90_Target::update_target_edit(bool negative, uint8_t tens, uint8_t ones, uint8_t dec,
                                                 uint8_t cursor_pos, bool digit_visible)
{
    // В режиме ввода разряды показываются явно (в т.ч. знак "+" и ведущий
    // ноль десятков) — в отличие от обычного отображения угла, где пустая
    // позиция скрывается пробелом. Здесь это экран настройки, а не живые
    // показания, так что явные "+05.0" читаются понятнее.
    char sign_ch = negative ? '-' : '+';
    char tens_ch = char('0' + tens);
    char ones_ch = char('0' + ones);
    char dec_ch  = char('0' + dec);

    // Мигание текущего (редактируемого) разряда — блок гасится на "невидимой"
    // фазе, ровно как и остальные виды мигания в проекте (LOW_BATTERY и т.п.).
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
