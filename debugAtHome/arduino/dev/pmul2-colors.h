#ifndef PMUL2_COLORS_H
#define PMUL2_COLORS_H

#include <stdint.h>

// byte IDs des couleurs (miroir du backend)
const uint8_t COLOR_BLUE  = 0x01;
const uint8_t COLOR_YELLOW    = 0x02;
const uint8_t COLOR_MAGENTA = 0x03;
const uint8_t COLOR_BROWN   = 0x04;
const uint8_t COLOR_ORANGE  = 0x05;

// table de lookup: byte ID -> nom affichable sur LCD
struct ColorEntry {
    uint8_t id;
    const char* name;
};

const ColorEntry COLOR_TABLE[] = {
    {COLOR_YELLOW,  "Yellow"},
    {COLOR_BLUE,    "Blue"},
    {COLOR_MAGENTA, "Magenta"},
    {COLOR_BROWN,   "Brown"},
    {COLOR_ORANGE,  "Orange"},
};
const uint8_t COLOR_TABLE_SIZE = sizeof(COLOR_TABLE) / sizeof(COLOR_TABLE[0]);

inline const char* colorNameById(uint8_t id) {
    for (uint8_t i = 0; i < COLOR_TABLE_SIZE; i++) {
        if (COLOR_TABLE[i].id == id) return COLOR_TABLE[i].name;
    }
    return "?";
}

#endif
