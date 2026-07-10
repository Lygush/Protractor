#include "settings.h"

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
