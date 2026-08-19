#include <Arduino.h>
#include <GyverButton.h>
#include <new>
#include <string.h>
#include "settings.h"
#include "oled.h"
#include "mpu.h"
#include "battery.h"
#include "sleep_manager.h"
#include "w25q32_read.h"   // внешняя SPI flash — DMP-блоб MPU6050 (и позже шрифт/иконки)
#include "flash_map.h"

// Раскомментируй, чтобы видеть напряжение/процент/состояние батареи в Serial.
// #define BATTERY_DEBUG

// Раскомментируй, чтобы видеть в Serial отладочные "Free RAM"/"OLED: creating..."
// #define SETUP_DEBUG

// ─── Статический буфер для OLED ───────────────────────────────────────────────
union OledStorage {
    OLED_Degrees_90        d90;
    OLED_Degrees_90_Target d90_target;
    OLED_Degrees_360       d360;
    OledStorage() {}
    ~OledStorage() {}
};

// ─── Глобальные объекты ────────────────────────────────────────────────────
Settings settings;
// Внешняя SPI flash W25Q32: CS=D10, SCK=D13, MISO=D12, MOSI=D11 (аппаратный
// SPI, те же пины, что и в ProtractorFlashWriter). DMP-блоб MPU6050 читается
// отсюда вместо PROGMEM — см. Protractor_ExtFlash_PLAN.md.
W25Q32Read extFlash(10);

MPU      mpu;
Battery  battery;
SleepManager sleep_manager;
GButton  menu_btn(6);
GButton  mode_btn(7);
GButton  zero_btn(8);

OLED* oled = nullptr;

// Указатель на текущий OLED-объект в его "родном" типе
OLED_Degrees_90_Target* target_oled = nullptr;

volatile bool mpu_flag = false;

// Выбор оси (см. MeasureAxis в settings.h) конвертируется в индекс AXIS,
// с которым уже работает mpu.cpp — DMP считает все три угла каждый кадр,
// переключение оси не требует доп. вычислений, только смены индекса.
AXIS current_axis() {
    return (settings.get_mpu_settings()->axis == MeasureAxis::Y) ? AXIS::Y : AXIS::X;
}

// Переводит текущий угол в единицы, выбранные в меню (Unit), и применяет тот
// же знак, что и остальной интерфейс (см. displayed_angle в target-режиме
// ниже). Формат/клэмп под конкретный Unit решает уже OLED (print_unit_value).
float compute_display_value(AngleUnit unit, AXIS axis) {
    switch (unit) {
        case AngleUnit::RADIANS:
            return -mpu.get_angle_radians(axis);
        case AngleUnit::PERCENT:
            return -mpu.get_slope_ratio(axis) * 100.0f;
        case AngleUnit::MM_PER_M:
            return -mpu.get_slope_ratio(axis) * 1000.0f;
        case AngleUnit::DEGREES:
        default:
            return -mpu.get_angle_degrees(axis);
    }
}

// ─── Цикл режимов отображения ──────────────────────────────────────────────
static constexpr DisplayMode MODE_CYCLE[] = { MODE_90, MODE_90_TARGET, MODE_360 };
static constexpr uint8_t     MODE_COUNT   = sizeof(MODE_CYCLE) / sizeof(MODE_CYCLE[0]);

DisplayMode current_mode = MODE_90;
uint8_t     mode_index   = 0;

// ─── Ввод уставки угла (режим MODE_90_TARGET) ──────────────────────────────
bool    editing_target   = false;
uint8_t edit_cursor       = 0;
bool    edit_negative     = false;
uint8_t edit_tens         = 0;
uint8_t edit_ones         = 0;
uint8_t edit_dec          = 0;

bool     edit_blink_visible = true;
uint32_t edit_blink_timer   = 0;
static constexpr uint32_t EDIT_BLINK_INTERVAL_MS = 400;

bool target_reached = false;

// ─── HOLD (заморозка показания) ────────────────────────────────────────────
// Клик zero — переключить заморозку показания (HOLD); удержание zero —
// зануление (была наоборот: клик=zero, удержание отсутствовало).
bool angle_held = false;

// ─── Автопереворот экрана ───────────────────────────────────────────────────
uint32_t orientation_change_since = 0;
static constexpr uint32_t ORIENTATION_HYSTERESIS_MS = 500;

// ─── Меню настроек ──────────────────────────────────────────────────────
enum class MenuItem : uint8_t {
    CALIBRATE = 0,
    AXIS,
    UNIT,
    SOUND,
    BRIGHTNESS,
    SLEEP_TIMEOUT,
    EXIT,
    COUNT
};
static constexpr uint8_t MENU_ITEM_COUNT = (uint8_t)MenuItem::COUNT;

bool    menu_open          = false;
bool    menu_editing_value = false;
uint8_t menu_cursor        = 0;
uint8_t menu_scroll_offset = 0;

static constexpr uint32_t SLEEP_TIMEOUT_OPTIONS_MS[] = { 15000UL, 30000UL, 60000UL, 120000UL };
static constexpr uint8_t  SLEEP_TIMEOUT_OPTIONS_COUNT =
    sizeof(SLEEP_TIMEOUT_OPTIONS_MS) / sizeof(SLEEP_TIMEOUT_OPTIONS_MS[0]);
static const char* const SLEEP_TIMEOUT_LABELS[] = { "15s", "30s", "60s", "120s" };

char        menu_line_buf[MENU_ITEM_COUNT][20];
const char* menu_line_ptrs[MENU_ITEM_COUNT];

// ─── Зуммер (D3) ──────────────────────────────────────────────────────────
static constexpr uint8_t  BUZZER_PIN                = D3;
static constexpr uint16_t BUZZER_FREQ_HZ            = 2000;
static constexpr uint16_t BUZZER_DURATION_NORMAL_MS = 150;
static constexpr uint16_t BUZZER_DURATION_QUIET_MS  = 60;

volatile uint16_t buzzer_toggle_count  = 0;
volatile uint16_t buzzer_toggle_target = 0;

ISR(TIMER2_COMPA_vect) {
    PORTD ^= (1 << PD3);
    if (++buzzer_toggle_count >= buzzer_toggle_target) {
        TIMSK2 &= ~(1 << OCIE2A);
        PORTD  &= ~(1 << PD3);
    }
}

void start_beep() {
    SoundLevel level = settings.get_sound_settings()->level;
    if (level == SoundLevel::OFF) return;

    uint16_t duration_ms = (level == SoundLevel::QUIET) ? BUZZER_DURATION_QUIET_MS
                                                          : BUZZER_DURATION_NORMAL_MS;

    TCCR2A = (1 << WGM21);
    TCCR2B = (1 << CS22);
    OCR2A  = (uint8_t)(F_CPU / 64UL / (2UL * BUZZER_FREQ_HZ)) - 1;

    buzzer_toggle_count  = 0;
    buzzer_toggle_target = (uint16_t)((2UL * (uint32_t)BUZZER_FREQ_HZ * duration_ms) / 1000UL);

    TCNT2  = 0;
    TIFR2 |= (1 << OCF2A);
    TIMSK2 |= (1 << OCIE2A);
}

// ─── ISR ───────────────────────────────────────────────────────────────────
void dmp_ready() {
    mpu_flag = true;
}

// ─── RAM (только для диагностики) ────────────────────────────────────────
#ifdef SETUP_DEBUG
extern int   __bss_end;
extern void* __brkval;

int memoryFree() {
    int freeValue;
    if ((int)__brkval == 0)
        freeValue = ((int)&freeValue) - ((int)&__bss_end);
    else
        freeValue = ((int)&freeValue) - ((int)__brkval);
    return freeValue;
}
#endif

// ─── Функция-геттер для статического хранилища OLED ──────────────────────
OledStorage& get_storage() {
    static OledStorage oled_storage;
    return oled_storage;
}

// ─── Battery -> OLED ─────────────────────────────────────────────────────
bool blink_visible = true;

void apply_battery_to_oled() {
    oled->set_battery_percent(battery.get_percent());
    oled->set_battery_state(battery.get_state());
    oled->update_battery(blink_visible);

#ifdef BATTERY_DEBUG
    Serial.print(battery.get_voltage());
    Serial.print(F(" V, "));
    Serial.print(battery.get_percent());
    Serial.print(F("%, state="));
    switch (battery.get_state()) {
        case BatteryState::NORMAL:      Serial.println(F("NORMAL"));      break;
        case BatteryState::LOW_BATTERY: Serial.println(F("LOW_BATTERY")); break;
        case BatteryState::CHARGING:    Serial.println(F("CHARGING"));    break;
    }
#endif
}

// ─── Уставка угла: разбор float <-> разряды ──────────────────────────────
void decompose_angle_90(float angle, bool& negative, uint8_t& tens, uint8_t& ones, uint8_t& dec) {
    negative = angle < 0;
    float a = fabs(angle);
    if (a > 99.9f) a = 99.9f;

    int whole = (int)a;
    int d = (int)((a - whole) * 10.0f + 0.5f);
    if (d >= 10) { d = 0; whole++; }
    if (whole > 99) whole = 99;

    tens = whole / 10;
    ones = whole % 10;
    dec  = (uint8_t)d;
}

float recompose_angle_90(bool negative, uint8_t tens, uint8_t ones, uint8_t dec) {
    float value = tens * 10 + ones + dec * 0.1f;
    return negative ? -value : value;
}

void adjust_edit_digit(int8_t delta) {
    switch (edit_cursor) {
        case 0:
            edit_negative = !edit_negative;
            break;
        case 1:
            edit_tens = (uint8_t)((edit_tens + 10 + delta) % 10);
            break;
        case 2:
            edit_ones = (uint8_t)((edit_ones + 10 + delta) % 10);
            break;
        case 3:
            edit_dec = (uint8_t)((edit_dec + 10 + delta) % 10);
            break;
    }
}

void enter_target_edit() {
    Target_settings* t = settings.get_target_settings();
    decompose_angle_90(t->target_angle_deg, edit_negative, edit_tens, edit_ones, edit_dec);

    edit_cursor        = 0;
    edit_blink_visible  = true;
    edit_blink_timer    = millis();
    editing_target      = true;
    target_reached      = false;
    angle_held          = false;

    target_oled->print_target_edit_page();
    target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                     edit_cursor, edit_blink_visible);
}

void exit_target_edit_and_save() {
    settings.get_target_settings()->target_angle_deg =
        recompose_angle_90(edit_negative, edit_tens, edit_ones, edit_dec);

    editing_target = false;
    sleep_manager.reset_activity();
    oled->print_base_page();
    apply_battery_to_oled();
}

// ─── Меню настроек: навигация и значения ──────────────────────────────────
uint8_t find_sleep_timeout_index() {
    uint32_t v = settings.get_sleep_settings()->inactivity_timeout_ms;
    for (uint8_t i = 0; i < SLEEP_TIMEOUT_OPTIONS_COUNT; i++) {
        if (SLEEP_TIMEOUT_OPTIONS_MS[i] == v) return i;
    }
    return 1;
}

void adjust_current_menu_item(int8_t delta) {
    Sound_settings*   snd  = settings.get_sound_settings();
    Display_settings* disp = settings.get_display_settings();
    Sleep_settings*   slp  = settings.get_sleep_settings();
    Mpu_settings*     mpuS = settings.get_mpu_settings();

    switch ((MenuItem)menu_cursor) {
        case MenuItem::AXIS: {
            mpuS->axis = (mpuS->axis == MeasureAxis::X) ? MeasureAxis::Y : MeasureAxis::X;
            break;
        }
        case MenuItem::UNIT: {
            int v = ((int)disp->unit + 4 + delta) % 4;
            disp->unit = (AngleUnit)v;
            break;
        }
        case MenuItem::SOUND: {
            int v = ((int)snd->level + 3 + delta) % 3;
            snd->level = (SoundLevel)v;
            break;
        }
        case MenuItem::BRIGHTNESS: {
            int v = ((int)disp->brightness + 3 + delta) % 3;
            disp->brightness = (BrightnessLevel)v;
            oled->apply_brightness(disp->brightness);
            break;
        }
        case MenuItem::SLEEP_TIMEOUT: {
            int idx = ((int)find_sleep_timeout_index() + SLEEP_TIMEOUT_OPTIONS_COUNT + delta)
                      % SLEEP_TIMEOUT_OPTIONS_COUNT;
            slp->inactivity_timeout_ms = SLEEP_TIMEOUT_OPTIONS_MS[idx];
            break;
        }
        default:
            break;
    }
}

void format_menu_lines() {
    Sound_settings*   snd  = settings.get_sound_settings();
    Display_settings* disp = settings.get_display_settings();
    Mpu_settings*     mpuS = settings.get_mpu_settings();

    strcpy(menu_line_buf[(uint8_t)MenuItem::CALIBRATE], "Calibrate");

    {
        const char* val = (mpuS->axis == MeasureAxis::Y) ? "Y" : "X";
        char* line = menu_line_buf[(uint8_t)MenuItem::AXIS];
        strcpy(line, "Axis: ");
        strcat(line, val);
    }
    {
        const char* val = "Deg";
        if (disp->unit == AngleUnit::RADIANS)        val = "Rad";
        else if (disp->unit == AngleUnit::PERCENT)   val = "%";
        else if (disp->unit == AngleUnit::MM_PER_M)  val = "mm/m";
        char* line = menu_line_buf[(uint8_t)MenuItem::UNIT];
        strcpy(line, "Unit: ");
        strcat(line, val);
    }
    {
        const char* val = "Normal";
        if (snd->level == SoundLevel::OFF)        val = "Off";
        else if (snd->level == SoundLevel::QUIET) val = "Quiet";
        char* line = menu_line_buf[(uint8_t)MenuItem::SOUND];
        strcpy(line, "Sound: ");
        strcat(line, val);
    }
    {
        const char* val = "Bright";
        if (disp->brightness == BrightnessLevel::DIM)         val = "Dim";
        else if (disp->brightness == BrightnessLevel::MEDIUM) val = "Medium";
        char* line = menu_line_buf[(uint8_t)MenuItem::BRIGHTNESS];
        strcpy(line, "Bright: ");
        strcat(line, val);
    }
    {
        char* line = menu_line_buf[(uint8_t)MenuItem::SLEEP_TIMEOUT];
        strcpy(line, "Sleep: ");
        strcat(line, SLEEP_TIMEOUT_LABELS[find_sleep_timeout_index()]);
    }
    strcpy(menu_line_buf[(uint8_t)MenuItem::EXIT], "Exit");

    for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) menu_line_ptrs[i] = menu_line_buf[i];
}

void update_menu_scroll() {
    if (MENU_ITEM_COUNT <= MENU_VISIBLE_ITEMS) {
        menu_scroll_offset = 0;
        return;
    }

    if (menu_cursor < menu_scroll_offset) {
        menu_scroll_offset = menu_cursor;
    } else if (menu_cursor >= menu_scroll_offset + MENU_VISIBLE_ITEMS) {
        menu_scroll_offset = menu_cursor - MENU_VISIBLE_ITEMS + 1;
    }
}

void render_menu() {
    format_menu_lines();

    // Если мы в режиме редактирования значения, оборачиваем текущее значение в <...>
    if (menu_editing_value) {
        // Для текущего пункта (кроме CALIBRATE и EXIT) добавим скобки к значению
        MenuItem item = (MenuItem)menu_cursor;
        if (item == MenuItem::AXIS || item == MenuItem::UNIT ||
            item == MenuItem::SOUND || item == MenuItem::BRIGHTNESS || item == MenuItem::SLEEP_TIMEOUT) {
            // Временно модифицируем строку в буфере, добавив '<' и '>'
            char* line = menu_line_buf[menu_cursor];
            // Найдём позицию после ": "
            char* val_start = strchr(line, ' ');
            if (val_start) {
                val_start++; // пропускаем пробел
                // Сдвинем остаток вправо на 2, чтобы вставить '<' и '>'
                size_t len = strlen(val_start);
                // Проверим, хватает места в буфере
                if (strlen(line) + 2 < sizeof(menu_line_buf[menu_cursor])) {
                    // Сдвигаем вправо на 1 для '>'
                    memmove(val_start + 1, val_start, len + 1);
                    val_start[0] = '<';
                    // Найдём конец значения (до пробела или конца)
                    char* end = val_start + 1;
                    while (*end && *end != ' ') end++;
                    // Вставим '>' перед пробелом или в конец
                    memmove(end + 1, end, strlen(end) + 1);
                    *end = '>';
                }
            }
        }
    }

    update_menu_scroll();

    uint8_t visible_cursor = (menu_cursor >= menu_scroll_offset)
                                ? (uint8_t)(menu_cursor - menu_scroll_offset)
                                : 0;

    oled->print_menu_page(menu_line_ptrs, MENU_ITEM_COUNT, menu_scroll_offset, visible_cursor, menu_editing_value);
}

void open_menu() {
    menu_open           = true;
    menu_editing_value  = false;
    menu_cursor         = 0;
    menu_scroll_offset  = 0;
    angle_held          = false;
    render_menu();
}

void close_menu() {
    settings.save_persisted();
    menu_open          = false;
    menu_editing_value = false;
    // Пока меню открыто, sleep_manager.update() не вызывается вообще (см.
    // условие "!menu_open" в loop()) — таймер бездействия всё это время не
    // сдвигается. Без явного сброса здесь первый же update() после закрытия
    // меню видел бы старую метку времени и мог увести в сон почти сразу
    // после выхода — на глаз выглядело бы как "уснул прямо в меню".
    sleep_manager.reset_activity();
    oled->print_base_page();
    apply_battery_to_oled();
}

void run_calibration_flow() {
    for (uint8_t i = 5; i > 0; i--) {
        oled->print_calibration_countdown(i);
        delay(1000);
    }
    oled->print_calibration_message("CALIBRATING...");
    // Самая "тяжёлая" по флешу функция в прошивке (~1.7КБ) — вызывается
    // только по явному запросу из меню, не при каждом старте.
    mpu.calibration(settings.get_mpu_settings()->calibration_strength);
    oled->print_calibration_message("DONE");
    delay(800);
}

void handle_menu_button_click() {
    MenuItem item = (MenuItem)menu_cursor;

    if (item == MenuItem::EXIT) {
        close_menu();
        return;
    }
    if (item == MenuItem::CALIBRATE) {
        run_calibration_flow();
        close_menu();
        return;
    }

    // Остальные пункты — со значением
    menu_editing_value = true;
    render_menu();
}

// ─── Смена режима отображения ──────────────────────────────────────────────
void construct_oled_for_mode(DisplayMode mode) {
    if (oled) oled->~OLED();
    target_oled = nullptr;

    switch (mode) {
        case MODE_90:
            oled = new (&get_storage().d90) OLED_Degrees_90();
            break;
        case MODE_90_TARGET:
            target_oled = new (&get_storage().d90_target) OLED_Degrees_90_Target();
            oled = target_oled;
            break;
        case MODE_360:
            oled = new (&get_storage().d360) OLED_Degrees_360();
            break;
    }

    current_mode  = mode;
    editing_target = false;
    target_reached = false;
    angle_held     = false;

    oled->init(settings.get_display_settings());
    oled->apply_brightness(settings.get_display_settings()->brightness);
    oled->print_base_page();
    apply_battery_to_oled();
    mpu.set_oled(oled);
#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM at start: "));
    Serial.println(memoryFree());
#endif
}

void change_mode_next() {
    mode_index = (mode_index + 1) % MODE_COUNT;
    construct_oled_for_mode(MODE_CYCLE[mode_index]);
}

void change_mode_prev() {
    mode_index = (mode_index + MODE_COUNT - 1) % MODE_COUNT;
    construct_oled_for_mode(MODE_CYCLE[mode_index]);
}

// ─── LED-моргалка ──────────────────────────────────────────────────────────
void blink_led(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        digitalWrite(D5, HIGH);
        delay(100);
        digitalWrite(D5, LOW);
        delay(200);
    }
}

// ─── SETUP ─────────────────────────────────────────────────────────────────
void setup() {
    // ВАЖНО: не полагаемся на дефолт ядра lgt8fx для analogRead(). Чип
    // физически умеет 12 бит, но что вернёт analogRead() без явного вызова
    // analogReadResolution() — зависит от версии установленного board
    // package: в части версий ядра дефолтом было 12 бит (0..4095), в текущих
    // релизах dbuezas/lgt8fx дефолт сделали 10 бит (0..1023) ради
    // совместимости с классическим Arduino Uno. battery.cpp (ADC_MAX=1023)
    // писан под 10 бит — фиксируем это явно, чтобы поведение не зависело от
    // того, какая версия ядра сейчас установлена в PlatformIO.
    analogReadResolution(10);

    settings.init_storage();
    settings.load_persisted();

    pinMode(D5, OUTPUT);
    digitalWrite(D5, LOW);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    Wire.begin();
    blink_led(1);

    oled = new (&get_storage().d90) OLED_Degrees_90();
    delay(500);

    oled->init(settings.get_display_settings());
    oled->apply_brightness(settings.get_display_settings()->brightness);
    blink_led(2);

    // Внешняя flash с DMP-блобом MPU6050 (и шрифтом/иконками — следующие
    // шаги переноса). Если чип не отвечает как Winbond — дальше идти нет
    // смысла: DMP не загрузится, лучше понятный стоп, чем зависание/мусор.
    extFlash.begin();
    if (!extFlash.present()) {
        oled->print_calibration_message("EXT FLASH FAIL");
        while (1) {}
    }

    mpu.init(oled, settings.get_mpu_settings());
    blink_led(3);

    attachInterrupt(0, dmp_ready, RISING);

    oled->print_base_page();

    // ВНИМАНИЕ: раньше здесь было pinMode(D7, OUTPUT); digitalWrite(D7, HIGH);
    // — но D7 занят mode_btn (GButton(7), INPUT_PULLUP) и используется PCINT
    // для пробуждения (см. sleep_manager.cpp). Принудительный перевод в
    // OUTPUT HIGH ломал саму кнопку (digitalRead читал состояние выхода, а
    // не нажатие) и создавал риск короткого замыкания VCC->GND через кнопку
    // при нажатии. Судя по всему, это был мусор от старой ревизии платы —
    // удалено при аудите 2026-08-17.

    battery.init(settings.get_battery_settings());
    apply_battery_to_oled();

    sleep_manager.init(settings.get_sleep_settings());
}

// ─── LOOP ──────────────────────────────────────────────────────────────────
void loop() {
    menu_btn.tick();
    mode_btn.tick();
    zero_btn.tick();

    // Таймер бездействия (сна) раньше сбрасывался только реальным движением
    // (см. SleepManager::update()) — нажатия кнопок его не трогали вообще.
    // isPress() больше нигде в проекте не используется (не путать с
    // isClick()/isHold(), которые НУЖНЫ для действий кнопок и которые нельзя
    // трогать второй раз без последствий — это consuming-флаги), поэтому его
    // безопасно читать здесь просто как признак "было любое нажатие" —
    // никакая другая часть кода этот флаг не потребляет и не полагается на
    // его состояние.
    if (zero_btn.isPress() || mode_btn.isPress() || menu_btn.isPress()) {
        sleep_manager.reset_activity();
    }

    if (editing_target) {
        if (zero_btn.isClick()) {
            adjust_edit_digit(+1);
            target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                             edit_cursor, edit_blink_visible);
        }
        if (mode_btn.isClick()) {
            adjust_edit_digit(-1);
            target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                             edit_cursor, edit_blink_visible);
        }
        if (menu_btn.isClick()) {
            edit_cursor++;
            if (edit_cursor > 3) {
                exit_target_edit_and_save();
            } else {
                edit_blink_visible = true;
                edit_blink_timer   = millis();
                target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                                 edit_cursor, edit_blink_visible);
            }
        }

        if (editing_target && millis() - edit_blink_timer >= EDIT_BLINK_INTERVAL_MS) {
            edit_blink_timer = millis();
            edit_blink_visible = !edit_blink_visible;
            target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                             edit_cursor, edit_blink_visible);
        }
    } else if (menu_open) {
        if (menu_editing_value) {
            if (zero_btn.isClick()) { adjust_current_menu_item(-1); render_menu(); }
            if (mode_btn.isClick())  { adjust_current_menu_item(+1); render_menu(); }
            if (menu_btn.isClick())  { menu_editing_value = false; render_menu(); }
        } else {
            if (zero_btn.isClick()) {
                menu_cursor = (uint8_t)((menu_cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
                render_menu();
            }
            if (mode_btn.isClick()) {
                menu_cursor = (uint8_t)((menu_cursor + 1) % MENU_ITEM_COUNT);
                render_menu();
            }
            if (menu_btn.isClick()) {
                handle_menu_button_click();
            }
        }
        // isHold() у GyverButton — НЕ одноразовый: возвращает true на каждый
        // tick(), пока кнопка физически удерживается дольше таймаута, а не
        // один раз в момент срабатывания. При коротком удержании разница не
        // заметна, но чуть более долгое — и close_menu()/save_persisted()
        // вызывались бы по нескольку раз подряд. isHolded() — одноразовый
        // аналог (сбрасывает свой флаг после чтения), нужен именно он.
        if (menu_btn.isHolded()) {
            close_menu();
        }
    } else {
        if (mode_btn.isClick()) {
            change_mode_next();
        }
        // См. комментарий выше про isHold() — с ним долгое удержание mode_btn
        // прокручивало бы через НЕСКОЛЬКО предыдущих режимов за одно нажатие
        // (change_mode_prev() вызывается на каждый tick, пока не отпустишь),
        // а не переключало на один назад, как задумано.
        if (mode_btn.isHolded()) {
            change_mode_prev();
        }
        if (zero_btn.isClick()) {
            angle_held = !angle_held;
            oled->update_hold_indicator(angle_held);
        }
        // Аналогично — без isHolded() request_zero() запускался бы заново
        // на каждый tick все время удержания, а не один раз.
        if (zero_btn.isHolded()) {
            mpu.request_zero();
        }
        if (menu_btn.isHolded() && current_mode == MODE_90_TARGET) {
            enter_target_edit();
        }
        if (menu_btn.isClick()) {
            open_menu();
        }
    }

    digitalWrite(D5, (mpu.is_zeroing() || target_reached) ? HIGH : LOW);

    bool battery_changed = battery.update();

    bool blink_toggled = false;
    static uint32_t blink_timer = 0;
    static constexpr uint32_t BLINK_INTERVAL_MS = 400;

    if (battery.get_state() == BatteryState::LOW_BATTERY) {
        if (millis() - blink_timer >= BLINK_INTERVAL_MS) {
            blink_timer = millis();
            blink_visible = !blink_visible;
            blink_toggled = true;
        }
    } else if (!blink_visible) {
        blink_visible = true;
    }

    // Обновляем батарею на экране ТОЛЬКО если меню и редактирование уставки закрыты
    if (!menu_open && !editing_target) {
        if (battery_changed || blink_toggled) {
            apply_battery_to_oled();
        }
    }

    if (mpu_flag) {
        mpu.calculate_angles();

        if (current_mode == MODE_90_TARGET && !editing_target && !menu_open) {
            float displayed_angle = -mpu.get_angle_degrees(current_axis());
            Target_settings* t = settings.get_target_settings();
            bool target_reached_now = fabs(displayed_angle - t->target_angle_deg) <= t->tolerance_deg;

            if (target_reached_now && !target_reached) {
                start_beep();
            }
            target_reached = target_reached_now;

            if (!angle_held) {
                int8_t direction = target_reached_now ? 0
                                   : (displayed_angle < t->target_angle_deg ? +1 : -1);
                target_oled->update_direction_arrow(direction, settings.get_mpu_settings()->axis);
            }
        } else {
            target_reached = false;
        }

        if (!editing_target && !menu_open && !angle_held) {
            // MODE_360 концептуально не поддерживает выбор Unit (Radians/%/мм-на-м)
            // из настроек — это относится только к MODE_90/MODE_90_TARGET (см.
            // комментарий у AngleUnit::unit в settings.h). OLED_Degrees_360::
            // update_angle_value() сам конвертирует радианы в градусы внутри —
            // если отдать ему уже готовое значение в единицах Unit (по умолчанию
            // DEGREES), получится двойная конвертация и вздор на экране. Поэтому
            // для 360 всегда отдаём угол в радианах напрямую, в обход Unit.
            if (current_mode == MODE_360) {
                oled->update_angle_value(-mpu.get_angle_radians(current_axis()));
            } else {
                AngleUnit unit = settings.get_display_settings()->unit;
                oled->update_angle_value(compute_display_value(unit, current_axis()));
            }
        }

        if (!editing_target && !menu_open && sleep_manager.update(mpu.get_angle_degrees(current_axis()))) {
            mpu.sleep();
            oled->set_power(false);
            extFlash.powerDown();

            sleep_manager.enter_sleep();

            extFlash.wakeUp();
            oled->set_power(true);
            mpu.wake();
            sleep_manager.reset_activity();

            // Старый баг: нажатие, разбудившее МК, ФИЗИЧЕСКИ ещё удерживается
            // (или как раз отпускается) уже после выхода из sleep_cpu() —
            // вопреки комментарию в sleep_manager.h про "GButton не тикает и
            // ничего не видит", как только tick() возобновляется в обычном
            // loop(), библиотека честно ловит это как настоящий press->release
            // и на следующем шаге выдаёт isClick()==true для той же самой
            // кнопки — то есть клик срабатывает ещё раз, хотя пользователь
            // всего лишь будил устройство. Дожидаемся физического отпускания
            // всех трёх кнопок и сливаем накопленные флаги. resetStates()
            // чистит их разом одним вызовом на кнопку (click/holded/press/
            // release/счётчики) — дешевле по коду, чем isClick()+isHolded()
            // по отдельности.
            do {
                menu_btn.tick();
                mode_btn.tick();
                zero_btn.tick();
            } while (menu_btn.state() || mode_btn.state() || zero_btn.state());

            menu_btn.resetStates();
            mode_btn.resetStates();
            zero_btn.resetStates();
        }

        mpu_flag = false;
    }
}