#include <Arduino.h>
#include <GyverButton.h>
#include <new>
#include <string.h>
#include "settings.h"
#include "oled.h"
#include "mpu.h"
#include "battery.h"
#include "sleep_manager.h"

// Раскомментируй, чтобы видеть напряжение/процент/состояние батареи в Serial.
// #define BATTERY_DEBUG

// Раскомментируй, чтобы видеть в Serial отладочные "Free RAM"/"OLED: creating..."
// #define SETUP_DEBUG

// ─── Статический буфер для OLED ───────────────────────────────────────────────
union OledStorage {
    OLED_Degrees_90        d90;
    OLED_Degrees_90_Target d90_target;
    OLED_Degrees_360       d360;
    OLED_Radians           rad;
    OledStorage() {}
    ~OledStorage() {}
};

// ─── Глобальные объекты ────────────────────────────────────────────────────
Settings settings;
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

// ─── Цикл режимов отображения ──────────────────────────────────────────────
static constexpr DisplayMode MODE_CYCLE[] = { MODE_90, MODE_90_TARGET, MODE_360, MODE_RADIANS };
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

// ─── Меню настроек ──────────────────────────────────────────────────────
enum class MenuItem : uint8_t {
    CALIBRATE = 0,
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

    switch ((MenuItem)menu_cursor) {
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

    strcpy(menu_line_buf[(uint8_t)MenuItem::CALIBRATE], "Calibrate");

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

void render_menu() {
    format_menu_lines();

    // Если мы в режиме редактирования значения, оборачиваем текущее значение в <...>
    if (menu_editing_value) {
        // Для текущего пункта (кроме CALIBRATE и EXIT) добавим скобки к значению
        MenuItem item = (MenuItem)menu_cursor;
        if (item == MenuItem::SOUND || item == MenuItem::BRIGHTNESS || item == MenuItem::SLEEP_TIMEOUT) {
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

    oled->print_menu_page(menu_line_ptrs, MENU_ITEM_COUNT, menu_cursor);
}

void open_menu() {
    menu_open           = true;
    menu_editing_value  = false;
    menu_cursor         = 0;
    render_menu();
}

void close_menu() {
    settings.save_persisted();
    menu_open          = false;
    menu_editing_value = false;
    oled->print_base_page();
    apply_battery_to_oled();
}

void run_calibration_flow() {
    for (uint8_t i = 5; i > 0; i--) {
        oled->print_calibration_countdown(i);
        delay(1000);
    }
    oled->print_calibration_message("CALIBRATING...");
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
        case MODE_RADIANS:
            oled = new (&get_storage().rad) OLED_Radians();
            break;
    }

    current_mode  = mode;
    editing_target = false;
    target_reached = false;

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
    settings.load_persisted();

    pinMode(D5, OUTPUT);
    digitalWrite(D5, LOW);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

#if defined(SETUP_DEBUG) || defined(BATTERY_DEBUG)
    Serial.begin(115200);
#endif
#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM at start: "));
    Serial.println(memoryFree());
    Serial.println(F("Start setup"));
#endif

    Wire.begin();
    blink_led(1);

    oled = new (&get_storage().d90) OLED_Degrees_90();
    delay(500);
#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM after OLED create: "));
    Serial.println(memoryFree());
#endif

    oled->init(settings.get_display_settings());
    oled->apply_brightness(settings.get_display_settings()->brightness);
#ifdef SETUP_DEBUG
    Serial.println(F("OLED: init OK"));
#endif
    blink_led(2);

    mpu.init(oled, settings.get_mpu_settings());
#ifdef SETUP_DEBUG
    Serial.println(F("MPU: init OK"));
#endif
    blink_led(3);

    attachInterrupt(0, dmp_ready, RISING);

    oled->print_base_page();

    pinMode(D7, OUTPUT);
    digitalWrite(D7, HIGH);

    battery.init(settings.get_battery_settings());
    apply_battery_to_oled();

    sleep_manager.init(settings.get_sleep_settings());

#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM at end of setup: "));
    Serial.println(memoryFree());
    Serial.println(F("Setup done"));
#endif
}

// ─── LOOP ──────────────────────────────────────────────────────────────────
void loop() {
    menu_btn.tick();
    mode_btn.tick();
    zero_btn.tick();

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
        if (menu_btn.isHold()) {
            close_menu();
        }
    } else {
        if (mode_btn.isClick()) {
            change_mode_next();
        }
        if (mode_btn.isHold()) {
            change_mode_prev();
        }
        if (zero_btn.isClick()) {
            mpu.request_zero();
        }
        if (menu_btn.isHold() && current_mode == MODE_90_TARGET) {
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
            float displayed_angle = -mpu.get_angle_degrees(AXIS::X);
            Target_settings* t = settings.get_target_settings();
            bool target_reached_now = fabs(displayed_angle - t->target_angle_deg) <= t->tolerance_deg;

            if (target_reached_now && !target_reached) {
                start_beep();
            }
            target_reached = target_reached_now;
        } else {
            target_reached = false;
        }

        float angle{ mpu.get_angle_radians(AXIS::X) };
        if (!editing_target && !menu_open) {
            oled->update_angle_value(angle);
        }

        if (!editing_target && !menu_open && sleep_manager.update(mpu.get_angle_degrees(AXIS::X))) {
            mpu.sleep();
            oled->set_power(false);

            sleep_manager.enter_sleep();

            oled->set_power(true);
            mpu.wake();
            sleep_manager.reset_activity();
        }

        mpu_flag = false;
    }
}