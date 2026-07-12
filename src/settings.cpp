#include "settings.h"
#include <avr/eeprom.h>
#include <stddef.h>

namespace {
    constexpr int EEPROM_ADDR = 0;

    // XOR всех байт структуры, кроме самого поля checksum (оно всегда
    // последнее) — не крипто, просто отличить "мусор/битую запись" от
    // валидных данных.
    uint8_t compute_checksum(const PersistedSettings& s) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&s);
        uint8_t sum = 0;
        for (size_t i = 0; i < sizeof(PersistedSettings) - sizeof(s.checksum); i++) {
            sum ^= bytes[i];
        }
        return sum;
    }
}

Mpu_settings *Settings::get_mpu_settings()
{
    return &mpu_settings;
}

Display_settings *Settings::get_display_settings()
{
    return &oled_settings;
}

Battery_settings *Settings::get_battery_settings()
{
    return &battery_settings;
}

Sleep_settings *Settings::get_sleep_settings()
{
    return &sleep_settings;
}

Target_settings *Settings::get_target_settings()
{
    return &target_settings;
}

Sound_settings *Settings::get_sound_settings()
{
    return &sound_settings;
}

void Settings::load_persisted()
{
    // Персистентность через EEPROM временно отключена — не влезала в флеш
    // вместе с остальным меню (см. историю чата). Код ниже рабочий и
    // проверенный, просто закомментирован — раскомментировать одним куском,
    // как только где-то ещё найдётся ~450 байт про запас.
    //
    // PersistedSettings loaded;
    // eeprom_read_block(&loaded, (const void*)EEPROM_ADDR, sizeof(loaded));
    // bool valid = (loaded.magic == PersistedSettings::MAGIC) &&
    //              (loaded.checksum == compute_checksum(loaded));
    // if (valid) {
    //     sound_settings.level                 = loaded.sound_level;
    //     oled_settings.brightness             = loaded.brightness;
    //     sleep_settings.inactivity_timeout_ms = loaded.sleep_timeout_ms;
    // } else {
    //     save_persisted();
    // }
}

void Settings::save_persisted()
{
    // См. комментарий в load_persisted() выше.
    //
    // PersistedSettings s;
    // s.magic               = PersistedSettings::MAGIC;
    // s.sound_level          = sound_settings.level;
    // s.brightness           = oled_settings.brightness;
    // s.sleep_timeout_ms     = sleep_settings.inactivity_timeout_ms;
    // s.checksum             = compute_checksum(s);
    // eeprom_update_block(&s, (void*)EEPROM_ADDR, sizeof(s));
}
