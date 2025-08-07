#include "oled.h"

//////////////// OLED BASE CLASS /////////////////////

void OLED::init(Display_settings *disp_settings)
{
    oled.init();
    oled.setContrast(15); 
    oled.clear();
    oled.update();
    oled_settings = disp_settings;
}

void OLED::print_start_page()
{  
    oled.clear();
    oled.drawBitmap(32, 10, frog_64x45, 64, 45);
    oled.rect(80, 16, 82, 18);
    oled.update();
    delay(400);
    oled.clear(80, 16, 82, 18);
    oled.line(80, 17, 82, 17);
    oled.update(80, 16, 82, 18);
    delay(400);
    oled.clear(80, 17, 82, 17);
    oled.rect(80, 16, 82, 18);
    oled.update(80, 16, 82, 18);
    delay(400);
}

void OLED::print_base_page_common()
{
    update_battery_value("TODO!");
    oled.setScale(0);
    oled.setCursor(0, 7);
    oled.print("zero");
    oled.setCursor(100, 7);
    oled.print("menu");
    oled.update();
}

void OLED::update_battery_value(String battery)
{
    oled.clear(0, 3, 20, 12);
    oled.drawBitmap(0, 3, battery_20x9, 20, 9);
    oled.update(0, 3, 20, 12);
}

// String OLED::alignment(float& angle_f)
// {
//     String angle_str{(int)angle_f};
//     while (angle_str.length() < 3) {
//         angle_str = " " + angle_str;
//     }
//     angle_str += '.';
//     angle_str += ((int)((angle_f - (int)angle_f) * 10));
//     return angle_str;
// }

void OLED::print_progress_bar(uint8_t actual_section, uint8_t full_bar, char* text)
{
    oled.setCursor(0,1);
    oled.setScale(1);
    oled.print("Calibrate ");
    oled.print(text);
    oled.update();
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

void OLED::change_revers()
{
    revers = (revers) ? false : true;
    oled.flipV(revers); 
    oled.flipH(revers);
}

//////////////// OLED DEGREE 360 MODE /////////////////////

void OLED_Degrees_360::print_base_page()
{
    oled.clear();
    print_base_page_common();

    oled.drawBitmap(47, 0, mode_360_34x14, 34, 14);
    oled.circle(118, 7, 3, OLED_STROKE);
    oled.circle(118, 7, 4, OLED_STROKE);
    oled.update();
}

void OLED_Degrees_360::update_angle_value(float angle)
{
    oled.clear(0, 15, 120, 45);
    oled.setScale(3);
    
     // Только нужную область
    //печатать значение относительно точки
    oled.setCursor(45,3);
    angle = degrees(angle);
    if (angle < 0) {
        angle += 360;
    }
    int angle_int{round(angle)};
    oled.print(angle_int);
    //oled.print(alignment_on_dot(degrees(abs(angle))));
    oled.update(0, 15, 120, 45); // Только нужную область
}

int OLED_Degrees_360::alignment(float angle_f)
{
    return 25;
}

//////////////// OLED DEGREE 90 LEFT MODE /////////////////////

OLED_Degrees_90::OLED_Degrees_90(DisplayMode _mode) : mode(_mode){};

void OLED_Degrees_90::print_base_page()
{
    oled.clear();
    print_base_page_common();

    if (mode == DisplayMode::MODE_90_LEFT) {
        oled.drawBitmap(47, 0, mode_left_90_34x14, 34, 14);
    }
    else if (mode == DisplayMode::MODE_90_RIGHT) {
        oled.drawBitmap(47, 0, mode_right_90_34x14, 34, 14);
    }    
    oled.circle(118, 7, 3, OLED_STROKE);
    oled.circle(118, 7, 4, OLED_STROKE);
    oled.update();
}

void OLED_Degrees_90::update_angle_value(float angle)
{
    oled.clear(0, 15, 120, 45);
    oled.setScale(3);
    angle = degrees(angle);

    if (mode == DisplayMode::MODE_90_LEFT) {
        if (angle > 0) {
            oled.setCursor(3,3);
            oled.print("-");
        }
    }
    else if (mode == DisplayMode::MODE_90_RIGHT) {
        if (angle < 0) {
            oled.setCursor(3,3);
            oled.print("-");
        }
    }

    oled.setCursor(alignment(angle), 3);
    if (angle > 98) {
        angle = 99;
    }
    else if (angle < -99) {
        angle = -99;
    }
    angle = abs(angle);
    oled.print(angle);
    oled.update();
}

int OLED_Degrees_90::alignment(float angle_f)
{
    if (abs(angle_f) >= 10) {
        return 25;
    }
    else return 43;
}

//////////////// OLED RADIANS MODE /////////////////////

void OLED_Radians::print_base_page()
{
    oled.clear();
    print_base_page_common();

    oled.setCursor(113, 1);
    oled.setScale(1);
    oled.print("PI");
    oled.update();   
}

void OLED_Radians::update_angle_value(float angle)
{
    oled.clear(0, 15, 120, 45);
    oled.setScale(3);
    if (angle < 0) {
        oled.setCursor(3,3);
        oled.print("-");
    }
     // Только нужную область
    oled.setCursor(25,3);
    oled.print(abs(angle),1);
    oled.update(0, 15, 120, 45); // Только нужную область
}

int OLED_Radians::alignment(float angle_f)
{
    return 25;
}

