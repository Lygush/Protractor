#include <Arduino.h>
#include <Wire.h>
#include <string.h>  // memcmp
#include <GyverOLED.h>
#include "flash_map.h"
#include "w25q32_full.h"
#include "dmpmemory.h"
#include "charmap_clean.h"
#include "icons_clean.h"

// Пины по вашей распайке: CS=D10, SCK=D13, MISO=D12, MOSI=D11 (аппаратный SPI)
#define FLASH_CS_PIN 10

W25Q32 flash(FLASH_CS_PIN);
GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;

// ─── Пины основной прошивки Protractor, переводим в безопасное состояние ──
// Список из main.cpp/settings.h основного проекта:
//   D2  — INT0, вход прерывания от MPU6050 (управляется самим MPU, не нами)
//   D3  — BUZZER_PIN, пищалка (output)
//   D5  — светодиод-моргалка (output)
//   D6  — menu_btn (GButton, вход с pullup)
//   D7  — mode_btn (GButton, вход с pullup); ВНИМАНИЕ — в setup() основной
//         прошивки этот же пин ПОВТОРНО настраивается как OUTPUT и держится
//         HIGH после отрисовки базового экрана. Это выглядит как реальный
//         конфликт в самом Protractor (одновременно кнопка и force-output),
//         я не трогаю эту логику и не могу однозначно сказать, что это —
//         второе намерение (power-latch?) или баг. Здесь ставлю его в режим
//         кнопки (INPUT_PULLUP) как более безопасный вариант на время записи
//         flash — если у вас на D7 реально что-то ещё повешено физически,
//         проверьте перед первым включением.
//   D8  — zero_btn (GButton, вход с pullup)
//   A1  — вход АЦП делителя батареи (по умолчанию INPUT, трогать не нужно)
//   A4/A5 — I2C шина (используем сами под OLED прогресс-бар)
//   D10-D13 — заняты под SPI к W25Q32, не трогаем
void set_main_firmware_pins_safe() {
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);      // пищалка молчит

    pinMode(5, OUTPUT);
    digitalWrite(5, LOW);      // светодиод погашен

    pinMode(6, INPUT_PULLUP);  // menu_btn — состояние покоя
    pinMode(7, INPUT_PULLUP);  // mode_btn — состояние покоя (см. предупреждение выше)
    pinMode(8, INPUT_PULLUP);  // zero_btn — состояние покоя

    // D2 (INT0) и A1 — оставляем как есть (обычный INPUT по умолчанию),
    // ими управляет внешняя периферия (MPU6050 / делитель батареи),
    // а не сама плата.
}

// ─── Вывод прогресса на OLED ───────────────────────────────────────────────
uint8_t progress_row = 0;

void oled_println(const char* text, bool ok_line = false) {
    if (progress_row > 7) {                 // экран кончился — скроллим сбросом
        oled.clear();
        progress_row = 0;
    }
    oled.setCursor(0, progress_row);
    oled.print(text);
    oled.update();
    progress_row++;
    (void)ok_line;
}

void oled_status(const char* label, bool ok) {
    oled.setCursor(0, progress_row);
    oled.print(label);
    oled.print(ok ? F(" OK") : F(" FAIL"));
    oled.update();
    progress_row++;
    if (progress_row > 7) { delay(600); oled.clear(); progress_row = 0; }
}

// Читает PROGMEM-массив в SRAM-буфер порциями, чтобы не класть большие
// куски в стек/RAM целиком без необходимости.
void copyProgmemToRam(const uint8_t* progmemSrc, uint8_t* ramDst, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) ramDst[i] = pgm_read_byte(progmemSrc + i);
}

bool writeBlobFromProgmem(const char* label, uint32_t flashAddr,
                           const uint8_t* progmemSrc, uint32_t len) {
    Serial.print(F("  -> "));
    Serial.print(label);
    Serial.print(F(" ("));
    Serial.print(len);
    Serial.print(F(" байт)... "));

    // Пишем порциями по 64 байта из RAM-буфера, читая их из PROGMEM на лету
    uint8_t buf[64];
    uint32_t written = 0;
    while (written < len) {
        uint16_t chunk = (len - written) > 64 ? 64 : (uint16_t)(len - written);
        copyProgmemToRam(progmemSrc + written, buf, chunk);
        flash.writeBuffer(flashAddr + written, buf, chunk);
        written += chunk;
    }

    // Верификация той же порционной читкой из PROGMEM для сравнения
    bool ok = true;
    written = 0;
    uint8_t verifyBuf[64];
    while (written < len) {
        uint16_t chunk = (len - written) > 64 ? 64 : (uint16_t)(len - written);
        copyProgmemToRam(progmemSrc + written, buf, chunk);
        flash.readBlock(flashAddr + written, verifyBuf, chunk);
        if (memcmp(buf, verifyBuf, chunk) != 0) { ok = false; break; }
        written += chunk;
    }

    Serial.println(ok ? F("OK") : F("ОШИБКА ВЕРИФИКАЦИИ!"));
    oled_status(label, ok);
    return ok;
}

uint16_t calcCrcFromFlash(uint32_t flashAddr, uint32_t len) {
    uint16_t crc = 0xFFFF;
    uint8_t buf[64];
    uint32_t off = 0;
    while (off < len) {
        uint16_t chunk = (len - off) > 64 ? 64 : (uint16_t)(len - off);
        flash.readBlock(flashAddr + off, buf, chunk);
        for (uint16_t i = 0; i < chunk; i++) {
            crc ^= buf[i];
            for (uint8_t b = 0; b < 8; b++) {
                if (crc & 1) crc = (crc >> 1) ^ 0xA001;
                else crc >>= 1;
            }
        }
        off += chunk;
    }
    return crc;
}

void setup() {
    // Пины основной прошивки — в безопасное состояние САМЫМ первым делом,
    // до всего остального (в т.ч. до инициализации SPI/OLED).
    set_main_firmware_pins_safe();

    Serial.begin(115200);
    while (!Serial) {}
    delay(500);

    Wire.begin();
    oled.init();
    oled.clear();
    oled.setScale(1);
    progress_row = 0;
    oled_println("Flash Writer");
    oled_println("start...");

    Serial.println(F("=== Protractor Flash Writer ==="));
    flash.begin();

    uint8_t id[3];
    flash.readJedecId(id);
    Serial.print(F("JEDEC ID: "));
    Serial.print(id[0], HEX); Serial.print(' ');
    Serial.print(id[1], HEX); Serial.print(' ');
    Serial.println(id[2], HEX);

    char idline[24];
    snprintf(idline, sizeof(idline), "JEDEC %02X %02X %02X", id[0], id[1], id[2]);
    oled_println(idline);

    if (id[0] != 0xEF) {
        Serial.println(F("!!! Модуль не отвечает как Winbond-флешка. Проверь пины/питание. Останов."));
        oled_println("NO FLASH FOUND!");
        oled_println("check wiring");
        while (1) {}
    }

    Serial.println(F("Стираю сектора 0-3 (16 КБ)..."));
    oled_println("Erasing 0-3...");
    for (uint32_t s = 0; s < FLASH_SECTORS_USED; s++) {
        flash.eraseSector(s * FLASH_SECTOR_SIZE);
        Serial.print(F("  сектор "));
        Serial.print(s);
        Serial.println(F(" стёрт"));
    }
    oled_println("Erase done");

    Serial.println(F("Записываю данные:"));
    oled_println("Writing...");
    bool ok = true;
    ok &= writeBlobFromProgmem("dmpMemory", ADDR_DMP_MEMORY, dmpMemoryData, SIZE_DMP_MEMORY);
    ok &= writeBlobFromProgmem("charMap", ADDR_CHARMAP, &charMapData[0][0], SIZE_CHARMAP);

    // Иконки — пишем каждую по её смещению из ICON_TABLE
    ok &= writeBlobFromProgmem("bat_outline", ADDR_ICONS + 0,   battery_outline_25x12_data, 50);
    ok &= writeBlobFromProgmem("bat_segment", ADDR_ICONS + 50,  battery_segment_3x12_data,  6);
    ok &= writeBlobFromProgmem("mode_360",    ADDR_ICONS + 56,  mode_360_34x14_data,        68);
    ok &= writeBlobFromProgmem("mode_90",     ADDR_ICONS + 124, mode_90_24x14_data,         48);
    ok &= writeBlobFromProgmem("circle_14",   ADDR_ICONS + 172, circle_14x14_data,          28);
    ok &= writeBlobFromProgmem("pi_14",       ADDR_ICONS + 200, pi_14x14_data,              28);
    ok &= writeBlobFromProgmem("percent_14",  ADDR_ICONS + 228, percent_14x14_data,         28);
    ok &= writeBlobFromProgmem("mm_m_22",     ADDR_ICONS + 256, mm_m_22x14_data,            44);
    ok &= writeBlobFromProgmem("arrow_up",    ADDR_ICONS + 300, arrow_up_7x8_data,          7);
    ok &= writeBlobFromProgmem("arrow_down",  ADDR_ICONS + 307, arrow_down_7x8_data,        7);
    ok &= writeBlobFromProgmem("arrow_left",  ADDR_ICONS + 314, arrow_left_8x7_data,        8);
    ok &= writeBlobFromProgmem("arrow_right", ADDR_ICONS + 322, arrow_right_8x7_data,       8);
    ok &= writeBlobFromProgmem("tgt_24x14",   ADDR_ICONS + 330, tgt_24x14_data,             48);
    ok &= writeBlobFromProgmem("x_axe_14x14", ADDR_ICONS + 378, x_axe_14x14_data,           28);
    ok &= writeBlobFromProgmem("y_axe_15x14", ADDR_ICONS + 406, y_axe_15x14_data,           30);
    ok &= writeBlobFromProgmem("frog_logo",   ADDR_ICONS + 436, Frog_128x64_data,           1024);

    if (!ok) {
        Serial.println(F("!!! Есть ошибки записи выше. Заголовок НЕ пишу. Останов."));
        oled_println("WRITE FAILED");
        oled_println("header NOT set");
        while (1) {}
    }

    Serial.println(F("Считаю CRC16 по данным из flash и пишу заголовок..."));
    oled_println("CRC + header...");
    FlashHeader hdr;
    hdr.magic       = HEADER_MAGIC;
    hdr.crc_dmp     = calcCrcFromFlash(ADDR_DMP_MEMORY, SIZE_DMP_MEMORY);
    hdr.crc_charmap = calcCrcFromFlash(ADDR_CHARMAP, SIZE_CHARMAP);
    hdr.crc_icons   = calcCrcFromFlash(ADDR_ICONS, SIZE_ICONS_TOTAL);

    flash.writeBuffer(ADDR_HEADER, (const uint8_t*)&hdr, sizeof(hdr));

    // Финальная проверка заголовка
    FlashHeader verify;
    flash.readBlock(ADDR_HEADER, (uint8_t*)&verify, sizeof(verify));
    bool hdrOk = (memcmp(&hdr, &verify, sizeof(hdr)) == 0);

    Serial.println(F("---"));
    Serial.print(F("crc_dmp     = 0x")); Serial.println(hdr.crc_dmp, HEX);
    Serial.print(F("crc_charmap = 0x")); Serial.println(hdr.crc_charmap, HEX);
    Serial.print(F("crc_icons   = 0x")); Serial.println(hdr.crc_icons, HEX);
    Serial.println(hdrOk ? F("ЗАГОЛОВОК ЗАПИСАН И ПРОВЕРЕН OK")
                          : F("!!! ЗАГОЛОВОК НЕ СОШЁЛСЯ ПРИ ПРОВЕРКЕ"));
    Serial.println(F("=== Готово. Можно перепрошивать плату основной прошивкой. ==="));

    oled.clear();
    oled.setCursor(0, 0);
    oled.print(hdrOk ? "DONE - OK!" : "HEADER FAIL!");
    oled.setCursor(0, 2);
    oled.print("Reflash main FW");
    oled.setCursor(0, 3);
    oled.print("now.");
    oled.update();
}

void loop() {}
