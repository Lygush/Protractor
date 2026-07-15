#include "settings.h"
#include <EEPROM.h>   // LGT8F328P: своя эмуляция EEPROM через flash (ECCR),
                       // НЕ классический avr/eeprom.h — см. lgt_eeprom_init()
                       // в setup() (main.cpp) и комментарий в этом файле ниже.
#include <stddef.h>
#include <string.h>

namespace {
    constexpr uint16_t EEPROM_ADDR = 0;

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

void Settings::init_storage()
{
    // 1 страница по 1КБ (реально доступно ~1020 байт) — с огромным запасом
    // для sizeof(PersistedSettings) (десяток с небольшим байт). Без этого
    // вызова эмуляция EEPROM попросту выключена (бит в ECCR не взведён), и
    // чтение/запись работают непредсказуемо/зависают.
    lgt_eeprom_init(1);
}

void Settings::load_persisted()
{
    PersistedSettings loaded;
    lgt_eeprom_read_block(reinterpret_cast<uint8_t*>(&loaded), EEPROM_ADDR, sizeof(loaded));
    bool valid = (loaded.magic == PersistedSettings::MAGIC) &&
                 (loaded.checksum == compute_checksum(loaded));
    if (valid) {
        sound_settings.level                 = loaded.sound_level;
        oled_settings.brightness             = loaded.brightness;
        oled_settings.unit                   = loaded.unit;
        sleep_settings.inactivity_timeout_ms = loaded.sleep_timeout_ms;
        mpu_settings.axis                    = loaded.axis;
    } else {
        save_persisted();
    }
}

void Settings::save_persisted()
{
    PersistedSettings s;
    s.magic               = PersistedSettings::MAGIC;
    s.sound_level          = sound_settings.level;
    s.brightness           = oled_settings.brightness;
    s.unit                 = oled_settings.unit;
    s.sleep_timeout_ms     = sleep_settings.inactivity_timeout_ms;
    s.axis                 = mpu_settings.axis;
    s.checksum             = compute_checksum(s);
    lgt_eeprom_write_block(reinterpret_cast<uint8_t*>(&s), EEPROM_ADDR, sizeof(s));
}
