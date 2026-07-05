#include <Arduino.h>
#include <GyverButton.h>
#include <new>
#include "settings.h"
#include "oled.h"
#include "mpu.h"
#include "battery.h"

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
    OLED_Degrees_90  d90;
    OLED_Degrees_360 d360;
    OLED_Radians     rad;
    OledStorage() {}
    ~OledStorage() {}
};

// ─── Глобальные объекты ────────────────────────────────────────────────────
Settings settings;
MPU      mpu;
Battery  battery;
GButton  menu_btn(6);
GButton  mode_btn(7);
GButton  zero_btn(8);

OLED* oled = nullptr;

volatile bool mpu_flag = false;

uint32_t timer_sec{};
uint32_t uptime_sec{};

DisplayMode current_mode = MODE_90;

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

// ─── Смена режима через placement new ─────────────────────────────────────
void change_mode() {
    if (oled) oled->~OLED();

    switch (current_mode) {
        case MODE_90:
            oled = new (&get_storage().d360) OLED_Degrees_360();
            current_mode = MODE_360;
            break;
        case MODE_360:
            oled = new (&get_storage().rad) OLED_Radians();
            current_mode = MODE_RADIANS;
            break;
        case MODE_RADIANS:
            oled = new (&get_storage().d90) OLED_Degrees_90();
            current_mode = MODE_90;
            break;
    }

    oled->init(settings.get_display_settings());
    oled->print_base_page();
    apply_battery_to_oled();   // новый OLED-объект создан с нуля — без этого сбросится на 0%
    mpu.set_oled(oled);
#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM at start: "));
    Serial.println(memoryFree());
#endif
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

    timer_sec = millis();

#ifdef SETUP_DEBUG
    Serial.print(F("Free RAM at end of setup: "));
    Serial.println(memoryFree());
    Serial.println(F("Setup done"));
#endif
}

// ─── LOOP ──────────────────────────────────────────────────────────────────
void loop() {
    mode_btn.tick();
    zero_btn.tick();

    if (mode_btn.isClick()) {
        change_mode();
    }
    if (zero_btn.isClick()) {
        mpu.request_zero();
    }
    digitalWrite(D5, mpu.is_zeroing() ? HIGH : LOW);

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
        float angle{ mpu.get_angle_radians(AXIS::X) };
        oled->update_angle_value(angle);
        mpu_flag = false;
    }
}
