#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <string.h>  // memcmp

// ============================================================================
//  Полный драйвер W25Q32 — используется ТОЛЬКО в writer-тулe (умеет стирать
//  и писать). В основную прошивку идёт урезанная read-only версия
//  (w25q32_read.h) — она значительно легче по flash.
// ============================================================================

class W25Q32 {
public:
    explicit W25Q32(uint8_t csPin) : _cs(csPin) {}

    void begin() {
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
        SPI.begin();
    }

    // Читает JEDEC ID (manufacturer, memType, capacity). Для W25Q32 обычно
    // 0xEF 0x40 0x16. Используем как быструю проверку "модуль вообще
    // подключен и отвечает" перед стиранием/записью.
    void readJedecId(uint8_t out[3]) {
        select();
        SPI.transfer(0x9F);
        out[0] = SPI.transfer(0x00);
        out[1] = SPI.transfer(0x00);
        out[2] = SPI.transfer(0x00);
        deselect();
    }

    void readBlock(uint32_t addr, uint8_t* buf, uint16_t len) {
        select();
        SPI.transfer(0x03);
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        for (uint16_t i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
        deselect();
    }

    void eraseSector(uint32_t addr) {
        writeEnable();
        select();
        SPI.transfer(0x20);  // Sector Erase (4KB)
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        deselect();
        waitBusy();
    }

    // Пишет один блок ≤256 байт, НЕ пересекающий границу страницы.
    // Для больших блоков используй writeBuffer() — она сама режет на страницы.
    void writePage(uint32_t addr, const uint8_t* data, uint16_t len) {
        writeEnable();
        select();
        SPI.transfer(0x02);  // Page Program
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        for (uint16_t i = 0; i < len; i++) SPI.transfer(data[i]);
        deselect();
        waitBusy();
    }

    // Пишет произвольную длину, сама режет на страницы по 256 байт с учётом
    // выравнивания. Сектор(а) должны быть предварительно стёрты.
    void writeBuffer(uint32_t addr, const uint8_t* data, uint32_t len) {
        uint32_t written = 0;
        while (written < len) {
            uint32_t pageOffset = (addr + written) % FLASH_PAGE_SIZE_LOCAL;
            uint32_t chunk = FLASH_PAGE_SIZE_LOCAL - pageOffset;
            if (chunk > (len - written)) chunk = len - written;
            writePage(addr + written, data + written, chunk);
            written += chunk;
        }
    }

    bool verifyBuffer(uint32_t addr, const uint8_t* data, uint32_t len) {
        uint8_t buf[64];
        uint32_t checked = 0;
        while (checked < len) {
            uint16_t chunk = (len - checked) > 64 ? 64 : (uint16_t)(len - checked);
            readBlock(addr + checked, buf, chunk);
            if (memcmp(buf, data + checked, chunk) != 0) return false;
            checked += chunk;
        }
        return true;
    }

private:
    static constexpr uint32_t FLASH_PAGE_SIZE_LOCAL = 256;
    uint8_t _cs;

    void select()   { digitalWrite(_cs, LOW); }
    void deselect() { digitalWrite(_cs, HIGH); }

    void writeEnable() {
        select();
        SPI.transfer(0x06);
        deselect();
    }

    uint8_t readStatus1() {
        select();
        SPI.transfer(0x05);
        uint8_t s = SPI.transfer(0x00);
        deselect();
        return s;
    }

    void waitBusy() {
        while (readStatus1() & 0x01) { /* BUSY bit */ }
    }
};
