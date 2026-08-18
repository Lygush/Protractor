#pragma once
#include <Arduino.h>
#include <SPI.h>

// ============================================================================
//  Read-only драйвер W25Q32 — этот файл идёт в ОСНОВНУЮ прошивку Protractor
//  (не в writer). Умеет только читать — команд erase/write здесь нет
//  специально, чтобы не тратить на них flash в прошивке, которой они не
//  нужны. Положить в lib/W25Q32Read/src/W25Q32Read.h (или src/ проекта).
// ============================================================================

class W25Q32Read {
public:
    explicit W25Q32Read(uint8_t csPin) : _cs(csPin) {}

    void begin() {
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
        SPI.begin();
    }

    // Быстрая проверка на старте: отвечает ли чип и это точно Winbond.
    bool present() {
        uint8_t mfr = readJedecManufacturer();
        return mfr == 0xEF;
    }

    void readBlock(uint32_t addr, uint8_t* buf, uint16_t len) const {
        digitalWrite(_cs, LOW);
        SPI.transfer(0x03);
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        for (uint16_t i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
        digitalWrite(_cs, HIGH);
    }

    // Точечное чтение одного байта — то, что нужно getFont() в GyverOLED
    // для отрисовки "холодных" (не горячих) символов по одному байту-столбцу.
    uint8_t readByte(uint32_t addr) const {
        uint8_t b;
        readBlock(addr, &b, 1);
        return b;
    }

    // Power-down (0xB9) — переводит чип в режим глубокого сна (~1мкА вместо
    // ~4-15мА в холостом режиме). Вызывать перед тем, как МК сам уходит в
    // sleep_cpu() (см. sleep_manager.cpp / SleepManager::enter_sleep()) —
    // иначе флеш всё время сна продолжает потреблять ток вхолостую.
    void powerDown() const {
        digitalWrite(_cs, LOW);
        SPI.transfer(0xB9);
        digitalWrite(_cs, HIGH);
    }

    // Release from Power-down (0xAB) — выводит чип обратно в рабочий режим.
    // По даташиту чипу нужно tRES1 (~3мкс) до готовности принять следующую
    // команду (чтение и т.п.) — delayMicroseconds(20) с запасом. Обязательно
    // вызывать перед первым readBlock()/readByte() после powerDown().
    void wakeUp() const {
        digitalWrite(_cs, LOW);
        SPI.transfer(0xAB);
        digitalWrite(_cs, HIGH);
        delayMicroseconds(20);
    }

private:
    uint8_t readJedecManufacturer() {
        digitalWrite(_cs, LOW);
        SPI.transfer(0x9F);
        uint8_t mfr = SPI.transfer(0x00);
        SPI.transfer(0x00);
        SPI.transfer(0x00);
        digitalWrite(_cs, HIGH);
        return mfr;
    }

    uint8_t _cs;
};
