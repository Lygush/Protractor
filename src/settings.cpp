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
