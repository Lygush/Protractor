#include "settings.h"

Mpu_settings *Settings::get_mpu_settings()
{
    return &mpu_settings;
}

Display_settings *Settings::get_display_settings()
{
    return &oled_settings;
}
