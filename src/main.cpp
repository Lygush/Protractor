#include <Arduino.h>
#include <GyverButton.h>
#include <new>
#include "settings.h"
#include "oled.h"
#include "mpu.h"
#include "battery.h"
#include "sleep_manager.h"

// Раскомментируй, чтобы видеть напряжение/процент/состояние батареи в Serial.
// По умолчанию выключено: #ifdef убирает код ещё на этапе препроцессора
// (не просто "не выполняется", а вообще не попадает в прошивку) — экономит флеш.
// #define BATTERY_DEBUG

// Раскомментируй, чтобы видеть в Serial отладочные "Free RAM"/"OLED: creating..."
// и т.п. — это диагностика с самых первых сессий bring-up платы, уже отслужившая
// своё в обычной работе. По умолчанию выключено — экономит флеш.
// #define SETUP_DEBUG

// ─── Статический буфер для OLED — никаких new/delete из кучи ───────────────
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

// Указатель на текущий OLED-объект в его "родном" типе — нужен только пока
// активен MODE_90_TARGET, чтобы вызывать специфичные для этого режима методы
// (print_target_edit_page()/update_target_edit()), которых нет в базовом OLED.
// nullptr во всех остальных режимах.
OLED_Degrees_90_Target* target_oled = nullptr;

volatile bool mpu_flag = false;

// ─── Цикл режимов отображения ──────────────────────────────────────────────
// Порядок переключения по клику mode_btn (вперёд) / удержанию mode_btn (назад).
static constexpr DisplayMode MODE_CYCLE[] = { MODE_90, MODE_90_TARGET, MODE_360, MODE_RADIANS };
static constexpr uint8_t     MODE_COUNT   = sizeof(MODE_CYCLE) / sizeof(MODE_CYCLE[0]);

DisplayMode current_mode = MODE_90;
uint8_t     mode_index   = 0;   // индекс current_mode в MODE_CYCLE

// ─── Ввод уставки угла (режим MODE_90_TARGET) ──────────────────────────────
bool    editing_target   = false;
uint8_t edit_cursor       = 0;      // 0=знак, 1=десятки, 2=единицы, 3=десятая доля
bool    edit_negative     = false;
uint8_t edit_tens         = 0;
uint8_t edit_ones         = 0;
uint8_t edit_dec          = 0;

bool     edit_blink_visible = true;
uint32_t edit_blink_timer   = 0;
static constexpr uint32_t EDIT_BLINK_INTERVAL_MS = 400;

// Достигнут ли сейчас целевой угол (актуально только для MODE_90_TARGET
// вне режима редактирования) — используется и для светодиода, и для бипа.
bool target_reached = false;

// ─── Зуммер (D3) ────────────────────────────────────────────────────────
// Своя простая генерация прямоугольного сигнала вместо Arduino tone()/
// noTone() — они тянут за собой немало флеша (таймер, таблицы регистров под
// несколько пинов), а нам нужна ровно одна частота на одном пине.
//
// Полностью неблокирующая версия: start_beep() только "взводит" зуммер,
// а update_buzzer() (вызывается каждую итерацию loop()) переключает пин по
// micros() и сам выключает зуммер по истечении длительности. millis() тут
// не подходит — период на 2кГц (500мкс) меньше его разрешения (1мс).
static constexpr uint16_t BUZZER_FREQ_HZ        = 2000;             // частота писка — подбери под свою пищалку
static constexpr uint32_t BUZZER_DURATION_US    = 150000UL;         // 150мс
static constexpr uint32_t BUZZER_HALF_PERIOD_US = 1000000UL / BUZZER_FREQ_HZ / 2;
static constexpr uint8_t  BUZZER_PIN = D3;

bool     buzzer_active         = false;
bool     buzzer_pin_high       = false;
uint32_t buzzer_start_us       = 0;
uint32_t buzzer_last_toggle_us = 0;

void start_beep() {
    buzzer_active         = true;
    buzzer_pin_high       = true;
    buzzer_start_us       = micros();
    buzzer_last_toggle_us = buzzer_start_us;
    digitalWrite(BUZZER_PIN, HIGH);
}

void update_buzzer() {
    if (!buzzer_active) return;

    uint32_t now = micros();
    if (now - buzzer_start_us >= BUZZER_DURATION_US) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzer_active = false;
        return;
    }

    if (now - buzzer_last_toggle_us >= BUZZER_HALF_PERIOD_US) {
        buzzer_last_toggle_us = now;
        buzzer_pin_high = !buzzer_pin_high;
        digitalWrite(BUZZER_PIN, buzzer_pin_high ? HIGH : LOW);
    }
}

// ─── ISR ───────────────────────────────────────────────────────────────────
void dmp_ready() {
    mpu_flag = true;
}

// ─── RAM (только для диагностики bring-up, см. SETUP_DEBUG) ───────────────
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
// Battery отдаёт только факты (процент + состояние). OLED умеет рисовать
// сегменты, значок молнии (CHARGING) и мигать контуром (LOW_BATTERY) —
// но какую фазу мигания показать прямо сейчас, решает main (blink_visible).
bool blink_visible = true;   // текущая фаза мигания для LOW_BATTERY

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

// ─── Уставка угла: разбор float <-> разряды (знак/десятки/единицы/десятая) ─
// Формат идентичен тому, что использует OLED_Degrees_90::update_angle_value()
// для живого отображения — так введённое значение и показания угла на экране
// визуально сопоставимы напрямую.
void decompose_angle_90(float angle, bool& negative, uint8_t& tens, uint8_t& ones, uint8_t& dec) {
    negative = angle < 0;
    float a = fabs(angle);
    if (a > 99.9f) a = 99.9f;

    int whole = (int)a;
    int d = (int)((a - whole) * 10.0f + 0.5f);
    if (d >= 10) { d = 0; whole++; }   // перенос разряда из-за округления
    if (whole > 99) whole = 99;

    tens = whole / 10;
    ones = whole % 10;
    dec  = (uint8_t)d;
}

float recompose_angle_90(bool negative, uint8_t tens, uint8_t ones, uint8_t dec) {
    float value = tens * 10 + ones + dec * 0.1f;
    return negative ? -value : value;
}

// Изменяет текущий (по edit_cursor) редактируемый разряд.
// Для знака delta не важна — обе кнопки (▲/▼) просто переключают +/-.
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

// Вход в редактирование уставки — вызывается только когда current_mode ==
// MODE_90_TARGET (проверка в loop()), поэтому target_oled гарантированно
// не nullptr.
void enter_target_edit() {
    Target_settings* t = settings.get_target_settings();
    decompose_angle_90(t->target_angle_deg, edit_negative, edit_tens, edit_ones, edit_dec);

    edit_cursor        = 0;
    edit_blink_visible  = true;
    edit_blink_timer    = millis();
    editing_target      = true;
    target_reached      = false;   // пока идёт настройка, индикация "достигнуто" не актуальна

    target_oled->print_target_edit_page();
    target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                     edit_cursor, edit_blink_visible);
}

// Выход из редактирования (после OK на последнем разряде) — сохраняет
// уставку в settings и возвращает обычный экран режима.
void exit_target_edit_and_save() {
    settings.get_target_settings()->target_angle_deg =
        recompose_angle_90(edit_negative, edit_tens, edit_ones, edit_dec);

    editing_target = false;
    sleep_manager.reset_activity();   // таймер сна был "заморожен" на время ввода — синхронизируем заново
    oled->print_base_page();          // тот же объект, что и раньше — просто перерисовать
}

// ─── Смена режима отображения (цикл вперёд/назад через placement new) ─────
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
    oled->print_base_page();
    apply_battery_to_oled();   // новый OLED-объект создан с нуля — без этого сбросится на 0%
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

    // OLED — placement new
#ifdef SETUP_DEBUG
    Serial.println(F("OLED: creating..."));
#endif
    oled = new (&get_storage().d90) OLED_Degrees_90();
    delay(500);
#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM after OLED create: "));
    Serial.println(memoryFree());
#endif

    oled->init(settings.get_display_settings());
#ifdef SETUP_DEBUG
    Serial.println(F("OLED: init OK"));
#endif
    blink_led(2);

    oled->print_start_page();

    // MPU
#ifdef SETUP_DEBUG
    Serial.println(F("MPU: init..."));
#endif
    mpu.init(oled, settings.get_mpu_settings());
#ifdef SETUP_DEBUG
    Serial.println(F("MPU: init OK"));
#endif
    blink_led(3);

    attachInterrupt(0, dmp_ready, RISING);

    oled->print_base_page();

    pinMode(A0, INPUT);
    pinMode(D7, OUTPUT);
    digitalWrite(D7, HIGH);

    // ─── Батарея ───────────────────────────────────────────────────
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
    // tick() всем трём кнопкам всегда, независимо от текущего состояния —
    // иначе GButton не отследит клики/удержания корректно.
    menu_btn.tick();
    mode_btn.tick();
    zero_btn.tick();

    if (editing_target) {
        // ── Режим ввода уставки: zero=▲, mode=▼, menu=OK ──────────────
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
                exit_target_edit_and_save();   // после десятой доли OK завершает ввод
            } else {
                edit_blink_visible = true;     // новый разряд сразу видимый, не "погашен"
                edit_blink_timer   = millis();
                target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                                 edit_cursor, edit_blink_visible);
            }
        }

        // Мигание текущего разряда — курсор ввода.
        if (editing_target && millis() - edit_blink_timer >= EDIT_BLINK_INTERVAL_MS) {
            edit_blink_timer = millis();
            edit_blink_visible = !edit_blink_visible;
            target_oled->update_target_edit(edit_negative, edit_tens, edit_ones, edit_dec,
                                             edit_cursor, edit_blink_visible);
        }
    } else {
        // ── Обычная работа кнопок ─────────────────────────────────────
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
    }

    digitalWrite(D5, (mpu.is_zeroing() || target_reached) ? HIGH : LOW);

    update_buzzer();   // неблокирующее переключение пина зуммера по micros()

    // Battery сам решает, когда реально мерить (см. MEASURE_INTERVAL_MS в battery.cpp);
    // update() возвращает true только когда процент или состояние правда изменились.
    bool battery_changed = battery.update();

    // Мигание контуром в LOW_BATTERY — отдельный таймер, не завязанный на
    // Battery::update(), т.к. само значение может не меняться, а мигать надо.
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
        blink_visible = true;   // вышли из LOW_BATTERY — сбрасываем фазу мигания
    }

    if (battery_changed || blink_toggled) {
        apply_battery_to_oled();
    }

    if (mpu_flag) {
        mpu.calculate_angles();

        // ── Проверка достижения уставки (только MODE_90_TARGET, не во время ввода) ──
        if (current_mode == MODE_90_TARGET && !editing_target) {
            float displayed_angle = -mpu.get_angle_degrees(AXIS::X);   // тот же знак, что и на экране
            Target_settings* t = settings.get_target_settings();
            bool target_reached_now = fabs(displayed_angle - t->target_angle_deg) <= t->tolerance_deg;

            if (target_reached_now && !target_reached) {
                start_beep();   // короткий бип только на входе в диапазон, не постоянно
            }
            target_reached = target_reached_now;
        } else {
            target_reached = false;
        }

        float angle{ mpu.get_angle_radians(AXIS::X) };
        if (!editing_target) {
            oled->update_angle_value(angle);
        }

        // Бездействие считаем по той же оси, что выводится на экран.
        // Во время редактирования уставки (устройство обычно держат неподвижно,
        // пока вводят цифры) sleep_manager.update() не вызываем вообще —
        // иначе таймер бездействия уведёт устройство в сон прямо посреди ввода.
        if (!editing_target && sleep_manager.update(mpu.get_angle_degrees(AXIS::X))) {
            mpu.sleep();
            oled->set_power(false);

            sleep_manager.enter_sleep();   // возврат — уже после пробуждения по кнопке

            oled->set_power(true);
            mpu.wake();
            sleep_manager.reset_activity();
        }

        mpu_flag = false;
    }
}
