#include "oled.h"

void OLED::init()
{
    oled.init();
    oled.clear();
    oled.update();
}

void OLED::print_start_page()
{  
    oled.clear();
    oled.drawBitmap(32, 10, Frog_64x45, 64, 45);
    oled.rect(80, 16, 82, 18);
    oled.update();
    delay(600);
    oled.clear(80, 16, 82, 18);
    oled.line(80, 17, 82, 17);
    oled.update(80, 16, 82, 18);
    delay(600);
    oled.clear(80, 17, 82, 17);
    oled.rect(80, 16, 82, 18);
    oled.update(80, 16, 82, 18);
    delay(600);
}

void OLED::print_base_page()
{
    oled.clear();
    oled.home();
    oled.drawBitmap(0, 0, Battery_20x9, 20, 9);
    oled.update();
}

void OLED::print_progress_bar(uint8_t actual_section, uint8_t full_bar)
{
    int segment_lenght{120 / full_bar};
    if (actual_section == 0) {
        oled.clear();
        oled.roundRect(4, 25, 124, 45, OLED_STROKE);
        oled.roundRect(3, 25, 125, 45, OLED_STROKE);
        oled.roundRect(2, 25, 126, 45, OLED_STROKE);
        oled.update();
    }
    else {
        oled.roundRect(4, 25, (int)(segment_lenght * actual_section) + segment_lenght + 4, 45);
        oled.update(); 
    }
}
