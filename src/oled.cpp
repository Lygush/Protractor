#include "oled.h"
#include "flash_map.h"
#include "w25q32_read.h"

//////////////// OLED BASE CLASS /////////////////////

// extFlash объявлен и инициализирован (.begin()) в main.cpp/setup(), задолго
// до первой отрисовки иконки — см. Protractor_ExtFlash_PLAN.md п.4.3.
extern W25Q32Read extFlash;

// Общий буфер под самую большую иконку (mode_360, 68 байт) — иконки рисуются
// не каждый кадр (см. update_battery() — ранний return, пока % не сменился;
// print_base_page() — только при входе в режим), так что задержка SPI-чтения
// перед drawBitmapRAM() не чувствуется на глаз.
static uint8_t icon_buf[ICON_MAX_SIZE];

static void drawIconFromFlash(GyverOLED<SSD1306_128x64, OLED_NO_BUFFER>& disp,
                               int x, int y, uint16_t offset, uint16_t size,
                               int width, int height) {
    // offset/size раньше были uint32_t "на всякий случай", хотя реальные
    // значения (макс. 1256, см. icons.h/SIZE_ICONS_TOTAL) с запасом
    // умещаются в uint16_t — а readBlock() ниже и так принимает len как
    // uint16_t. Функция вызывается 14 раз по всему файлу, на каждый вызов
    // uint32_t аргумент стоит вдвое дороже по коду, чем uint16_t — при
    // критичном остатке флеша сужение здесь даёт заметную экономию сразу
    // на всех вызовах. ADDR_ICONS сам по себе uint32_t, так что
    // "ADDR_ICONS + offset" по обычным правилам приведения типов всё равно
    // считается как 32-битное сложение — корректный адрес для readBlock().
    extFlash.readBlock(ADDR_ICONS + offset, icon_buf, size);
    disp.drawBitmapRAM(x, y, icon_buf, width, height);
}

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

void OLED::update_battery(bool blink_visible, bool force_redraw)
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
    // именно видимое число сегментов. force_redraw — исключение: вызывается
    // из мест, где перед этим прошёл полный oled.clear() экрана (выход из
    // меню/редактирования уставки), и физически на экране иконки уже нет,
    // хотя кэш "последнего нарисованного" ещё думает, что она там есть.
    if (!force_redraw && segments_lit == last_drawn_battery_segments) return;
    last_drawn_battery_segments = segments_lit;

    oled.clear(x, y, x + w, y + h);

    // Контур рисуется всегда — он же определяет "пустые" (незажжённые) слоты.
    drawIconFromFlash(oled, x, y, ICON_OFF_BATTERY_OUTLINE, ICON_SIZE_BATTERY_OUTLINE, w, h);

    static constexpr uint8_t SEG_W    = 3;
    static constexpr uint8_t SEG_H    = 12; // одна страница — см. комментарий у battery_segment_3x8
    static constexpr uint8_t SEG_X0   = 2; // первый слот, с 1px отступом от рамки
    static constexpr uint8_t SEG_STEP = SEG_W + 1; // ширина сегмента + 1px зазор

    for (uint8_t i = 0; i < segments_lit; i++) {
        int seg_x = x + SEG_X0 + i * SEG_STEP;
        drawIconFromFlash(oled, seg_x, y, ICON_OFF_BATTERY_SEGMENT, ICON_SIZE_BATTERY_SEGMENT, SEG_W, SEG_H);
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

void OLED::print_base_page_common(bool menu_hold_enters_edit)
{
    update_battery(true, true);
    oled.setScale(1);

    // Верхняя строка футера (page 6, y=48..55) — левая часть: подпись
    // КЛИКА zero_btn ("hold"/"HOLD", динамически, см. update_hold_indicator()
    // ниже). Страница свободна на всех базовых экранах — номер угла
    // занимает только y=16..47 (страницы 2..5, см. send_split_num_buffer).
    // Слот mode_btn здесь убран (было "prev" — не поместилось нормально по
    // месту и не так важно, как для zero/menu).
    // menu_hold_enters_edit: удержание menu_btn переводит в редактирование
    // уставки, но только в режиме MODE_90_TARGET (см. main.cpp) — в
    // остальных режимах удержание menu_btn ничего не делает, подпись не
    // рисуем, чтобы не обещать несуществующее действие.
    oled.setCursor(0, 6);
    oled.print(F("hold"));
    if (menu_hold_enters_edit) {
        oled.setCursor(100, 6);
        oled.print(F("edit")); // menu_btn, удержание — редактировать уставку
    }

    // Нижняя строка (page 7, футер) — под "hold" на page 6 теперь видна
    // подпись УДЕРЖАНИЯ zero_btn ("zero" — зануление угла), статичная (в
    // отличие от верхней строки, не переключается). mode/menu — как раньше,
    // подписи клика.
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

void OLED::update_hold_indicator(bool held)
{
    // Раньше писала в page 7 — теперь подпись клика ("hold"/"HOLD") сверху
    // (page 6), под ней статичная подпись удержания ("zero", page 7, см.
    // print_base_page_common()).
    oled.setCursor(0, 6);
    oled.print(held ? F("HOLD") : F("hold"));
    oled.update();
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

// Список пунктов меню с прокруткой: рисуем не более MENU_VISIBLE_ITEMS пунктов.
// scroll_offset — какой пункт массива отобразить первым,
// visible_cursor — индекс выделенного пункта в видимой области (0..MENU_VISIBLE_ITEMS-1).
//
// Прямая отрисовка (без RAM-буфера), но каждый пункт — на СВОЕЙ странице,
// с одной пустой страницей-зазором после (кроме последнего): row = i*2 в
// терминах setCursor() (постраничные координаты). См. комментарий у
// MENU_VISIBLE_ITEMS в oled.h — почему зазор сделан целой страницей, а не
// в пикселях.
void OLED::draw_updown_footer_icons()
{
    drawIconFromFlash(oled, 0,  56, ICON_OFF_ARROW_UP,   ICON_SIZE_ARROW_UP,   7, 8);
    drawIconFromFlash(oled, 52, 56, ICON_OFF_ARROW_DOWN, ICON_SIZE_ARROW_DOWN, 7, 8);
}

void OLED::draw_ok_label()
{
    oled.setCursor(100, 7);
    oled.print(F("OK"));
}

void OLED::print_menu_page(const char* const lines[], uint8_t line_count,
                            uint8_t scroll_offset, uint8_t visible_cursor,
                            bool editing_value)
{
    oled.clear();

    // Шрифт scale=1 (8px в высоту). Показываем MENU_VISIBLE_ITEMS (3) пункта
    // на страницах 0,2,4 с пустыми страницами-зазорами между ними (1,3);
    // страницы 5,6 остаются пустыми перед футером (страница 7) — см.
    // комментарий у MENU_VISIBLE_ITEMS в oled.h.
    oled.setScale(1);

    uint8_t visible_count = (line_count < MENU_VISIBLE_ITEMS) ? line_count : MENU_VISIBLE_ITEMS;

    for (uint8_t i = 0; i < visible_count; i++) {
        uint8_t actual_idx = scroll_offset + i;
        const char* text = lines[actual_idx];

        if (i == visible_cursor) {
            // Подложка выделения с отступом 1px с каждой стороны от текста —
            // без него инверсия шла строго по границам символов, и буквы
            // визуально "обрезались" о рамку. Текст занимает ровно одну
            // страницу по высоте (8px, y0..y0+7); 1px сверху/снизу уходит в
            // соседнюю пустую страницу-зазор (см. MENU_VISIBLE_ITEMS выше),
            // кроме последнего видимого пункта — там снизу футер (page 7),
            // поэтому там низ подложки прижат к 55px, в футер не залезаем.
            int text_y0 = i * 16;                    // пиксельная Y (страница i*2 * 8px)
            int text_w  = (int)strlen(text) * 6;      // 6px на символ (5px глиф + 1px spacing)
            int rect_x0 = MENU_ITEM_X0 - 1;
            int rect_x1 = MENU_ITEM_X0 + text_w;      // +1px справа от текста
            int rect_y0 = (text_y0 > 0) ? text_y0 - 1 : 0;
            int rect_y1 = text_y0 + 8;                // +1px снизу от текста
            if (rect_y1 > 55) rect_y1 = 55;            // не залезаем в футер (страница 7)

            oled.rect(rect_x0, rect_y0, rect_x1, rect_y1, OLED_FILL);

            oled.setCursor(MENU_ITEM_X0, i * 2); // page-units: страница i*2, пустая i*2+1 — зазор
            oled.invertText(true);
            oled.print(text);
            oled.invertText(false);
        } else {
            oled.setCursor(MENU_ITEM_X0, i * 2); // page-units: страница i*2, пустая i*2+1 — зазор
            oled.print(text);
        }
    }

    // Подписи кнопок внизу — те же иконки стрелок, что и индикатор
    // направления на основном экране (см. OLED_Degrees_90_Target), вместо
    // прежних текстовых "^"/"v": при пролистывании списка (editing_value
    // == false) — иконки вверх/вниз, при редактировании значения текущего
    // пункта (editing_value == true) — иконки влево/вправо. Третья кнопка
    // (menu -> OK) остаётся текстом, у неё нет парного направления.
    if (editing_value) {
        drawIconFromFlash(oled, 0,  56, ICON_OFF_ARROW_LEFT,  ICON_SIZE_ARROW_LEFT,  8, 8);
        drawIconFromFlash(oled, 52, 56, ICON_OFF_ARROW_RIGHT, ICON_SIZE_ARROW_RIGHT, 8, 8);
    } else {
        draw_updown_footer_icons();
    }
    draw_ok_label();
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

void OLED::send_split_num_buffer(const char* left, const char* right, int dot_x) {
    static constexpr int bufX0 = 0, bufY0 = 16, bufX1 = 127, bufY1 = 47;
    static constexpr int vOffset = 3;
    static constexpr int CHAR_W = 6 * 3;

    bool ok = oled.createBuffer(bufX0, bufY0, bufX1, bufY1, 0);
    if (!ok) return;

    oled.setScale(3);
    int y = bufY0 + (32 - 8 * 3) / 2 + vOffset;

    oled.setCursorXY(dot_x - (int)strlen(left) * CHAR_W, y);
    oled.print(left);

    oled.setCursorXY(dot_x, y);
    oled.print(F("."));

    oled.setCursorXY(dot_x + CHAR_W, y);
    oled.print(right);

    oled.sendBuffer();
    oled.setWindow(0, 0, 127, 7);

    // ВАЖНО: setScale(3) выше иначе остаётся висеть до следующего явного
    // setScale() где-то ещё в коде. Найденный баг: draw_target_edit_footer()
    // печатает "OK" сразу после этой функции, ничего не переустанавливая —
    // без сброса тут "OK" рисовался масштабом x3 (глиф 24px высотой) и либо
    // вылезал за пределы видимой области, либо перекрывался следующей
    // отрисовкой, что выглядело как "стрелка просто пропадает, OK не
    // появляется". (В send_num_buffer() ту же защиту не ставим — оба её
    // вызывающих ничего не печатают следом в том же кадре, там это было бы
    // мёртвым кодом.)
    oled.setScale(1);
}

//////////////// OLED DEGREE 360 MODE /////////////////////

void OLED_Degrees_360::print_base_page()
{
    oled.clear();
    print_base_page_common();
    drawIconFromFlash(oled, 47, 0, ICON_OFF_MODE_360, ICON_SIZE_MODE_360, 34, 14);
    drawIconFromFlash(oled, 110, 0, ICON_OFF_CIRCLE_15, ICON_SIZE_CIRCLE_15, 15, 15);
    oled.update();
}

void OLED_Degrees_360::update_angle_value(float angle)
{
    angle = degrees(angle);
    // Полноценный wrap в [0,360) через fmod — одиночного "+360" недостаточно:
    // yaw копится без ограничений, при длительной работе/дрейфе угол может
    // уйти за пределы одного оборота (например, ниже -360), и один "+360"
    // не компенсирует это. fmodf корректно сворачивает любое значение.
    angle = fmodf(angle, 360.0f);
    if (angle < 0) angle += 360.0f;

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
    drawIconFromFlash(oled, 52, 0, ICON_OFF_MODE_90, ICON_SIZE_MODE_90, 24, 14);
    drawIconFromFlash(oled, 110, 0, ICON_OFF_CIRCLE_15, ICON_SIZE_CIRCLE_15, 15, 15);
    oled.update();
}

// value приходит уже в конечных единицах текущего Unit (см.
// main.cpp: compute_display_value()) — здесь только форматирование.
void OLED_Degrees_90::update_angle_value(float value)
{
    switch (oled_settings ? oled_settings->unit : AngleUnit::DEGREES) {
        case AngleUnit::RADIANS:
            print_unit_value(value, 1, 9.9f);
            break;
        case AngleUnit::PERCENT:
        case AngleUnit::MM_PER_M:
            print_unit_value(value, 3, 999.9f);
            break;
        case AngleUnit::DEGREES:
        default:
            print_unit_value(value, 2, 99.9f);
            break;
    }
}

// int_digits — сколько целых разрядов в формате (1=Rad "±N.N", 2=Deg
// "±NN.N", 3=%/мм-на-метр "±NNN.N"), max_abs — граница клэмпа, согласованная
// с int_digits (напр. 2 разряда -> максимум 99.9). Ведущие нули в целой
// части гасятся пробелами, младший разряд печатается всегда.
void OLED_Degrees_90::print_unit_value(float value, uint8_t int_digits, float max_abs)
{
    value = constrain(value, -max_abs, max_abs);

    bool negative = value < 0;
    float a = fabs(value);

    int whole = (int)a;
    int dec = (int)((a - whole) * 10.0f + 0.5f);
    if (dec >= 10) { dec = 0; whole++; }

    if (whole == 0 && dec == 0) negative = false;

    char left[5];
    uint8_t pos = 0;
    left[pos++] = negative ? '-' : ' ';

    bool leading_zero = true;
    if (int_digits >= 3) {
        int d = (whole / 100) % 10;
        if (d != 0) leading_zero = false;
        left[pos++] = leading_zero ? ' ' : char('0' + d);
    }
    if (int_digits >= 2) {
        int d = (whole / 10) % 10;
        if (d != 0) leading_zero = false;
        left[pos++] = leading_zero ? ' ' : char('0' + d);
    }
    left[pos++] = char('0' + whole % 10);
    left[pos] = '\0';

    char right[2];
    right[0] = char('0' + dec);
    right[1] = '\0';

    // 3 целых разряда (%, мм/м) не влезают в историческую позицию точки
    // (SPLIT_DOT_X_DEFAULT рассчитана под "±99.9") — сдвигаем левее, чтобы
    // "-999.9" целиком помещалось на 128px экране.
    int dot_x = (int_digits >= 3) ? 74 : SPLIT_DOT_X_DEFAULT;

    send_split_num_buffer(left, right, dot_x);
}

//////////////// OLED DEGREE 90 + TARGET MODE /////////////////////

void OLED_Degrees_90_Target::print_base_page()
{
    oled.clear();
    print_base_page_common(true); // удержание menu_btn здесь входит в редактирование уставки
    oled.setScale(1);
    oled.setCursor(46, 1);
    oled.print(F("90 TGT")); //показывать целевой угол.
    drawIconFromFlash(oled, 110, 0, ICON_OFF_CIRCLE_15, ICON_SIZE_CIRCLE_15, 15, 15);
    oled.update();
}

void OLED_Degrees_90_Target::print_target_edit_page()
{
    oled.clear();
    update_battery(true, true);
    oled.setScale(1);
    last_edit_cursor = 0xFF; // сброс — гарантируем отрисовку футера на входе
    oled.update();
}

void OLED_Degrees_90_Target::draw_target_edit_footer(uint8_t cursor_pos)
{
    oled.clear(0, 56, 127, 63);
    draw_updown_footer_icons();
    if (cursor_pos < 3) {
        drawIconFromFlash(oled, 100, 56, ICON_OFF_ARROW_RIGHT, ICON_SIZE_ARROW_RIGHT, 8, 8);
    } else {
        draw_ok_label();
    }
    oled.update(0, 56, 127, 63);
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

    send_split_num_buffer(left, right, SPLIT_DOT_X_DEFAULT);

    // Футер зависит только от cursor_pos (какой разряд редактируется), а
    // эта функция вызывается ещё и на каждый тик мигания (digit_visible
    // меняется, cursor_pos — нет) — перерисовываем футер только при смене
    // разряда, не на каждый блинк.
    if (cursor_pos != last_edit_cursor) {
        draw_target_edit_footer(cursor_pos);
        last_edit_cursor = cursor_pos;
    }
}

// Число (send_split_num_buffer) занимает всю полосу y=16..47 через
// createBuffer(...,fill=0) и на каждый кадр стирает всё, что было в этой
// полосе, включая область стрелки — поэтому стрелку перерисовываем здесь же,
// сразу после базового форматирования числа (см. update_angle_value ниже).
void OLED_Degrees_90_Target::update_angle_value(float angle)
{
    OLED_Degrees_90::update_angle_value(angle);
    draw_direction_arrow_now();
}

void OLED_Degrees_90_Target::draw_direction_arrow_now()
{
    oled.clear(ARROW_X, ARROW_Y, ARROW_X + ARROW_W, ARROW_Y + ARROW_H);
    if (last_direction != 0) {
        // Раньше здесь было 4 отдельных вызова drawIconFromFlash (по одному
        // на каждую комбинацию ось×знак) — на AVR каждый вызов с 6
        // аргументами компилируется отдельно. Выбираем нужные offset/size/
        // ширину заранее и вызываем drawIconFromFlash один раз — тот же
        // результат, меньше кода (при критичном остатке флеша важно).
        uint16_t off, size;
        uint8_t  w;
        if (last_axis == MeasureAxis::X) {
            // Ось X (крен влево/вправо) — стрелка влево/вправо.
            w = ARROW_LR_W;
            if (last_direction > 0) { off = ICON_OFF_ARROW_RIGHT; size = ICON_SIZE_ARROW_RIGHT; }
            else                    { off = ICON_OFF_ARROW_LEFT;  size = ICON_SIZE_ARROW_LEFT;  }
        } else {
            // Ось Y (тангаж вперёд/назад) — стрелка вверх/вниз. Маппинг
            // подобран эмпирически (был инвертирован при первой реализации,
            // см. Задачи.txt) — с осью X знак совпал сразу, с Y оказался
            // зеркальным, поэтому здесь направление противоположно ветке X.
            w = ARROW_UD_W;
            if (last_direction > 0) { off = ICON_OFF_ARROW_DOWN; size = ICON_SIZE_ARROW_DOWN; }
            else                    { off = ICON_OFF_ARROW_UP;   size = ICON_SIZE_ARROW_UP;   }
        }
        drawIconFromFlash(oled, ARROW_X, ARROW_Y, off, size, w, ARROW_H);
    }
    oled.update(ARROW_X, ARROW_Y, ARROW_X + ARROW_W, ARROW_Y + ARROW_H);
}

void OLED_Degrees_90_Target::update_direction_arrow(int8_t direction, MeasureAxis axis)
{
    // Раньше здесь сразу вызывался draw_direction_arrow_now() — но эта
    // перерисовка тут же стиралась следующим вызовом update_angle_value()
    // (он обязан перерисовать стрелку заново, т.к. полоса y=16..47 целиком
    // стирается печатью числа — см. комментарий у update_angle_value ниже).
    // Получалось два clear()+drawIcon()+I2C update() за один DMP-кадр вместо
    // одного — отсюда видимое мерцание стрелки. Теперь здесь только
    // запоминаем направление и ось, а рисуем один раз — из update_angle_value().
    last_direction = direction;
    last_axis = axis;
}
