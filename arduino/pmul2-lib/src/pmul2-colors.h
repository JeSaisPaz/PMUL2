#ifndef PMUL2_COLORS_H
#define PMUL2_COLORS_H

#include <stdint.h>

// byte IDs des couleurs (miroir du backend)
// le backend envoie la liste des IDs actifs, l'Arduino les stocke
// et les affiche dans l'ordre sur les touches A, B, C, D

const uint8_t COLOR_YELLOW  = 0x01;
const uint8_t COLOR_BLUE    = 0x02;
const uint8_t COLOR_MAGENTA = 0x03;

// table de lookup: byte ID -> nom affichable sur LCD
struct ColorEntry {
    uint8_t id;
    const char* name;
};

const ColorEntry COLOR_TABLE[] = {
    {COLOR_YELLOW,  "Jaune"},
    {COLOR_BLUE,    "Bleu"},
    {COLOR_MAGENTA, "Magenta"},
};
const uint8_t COLOR_TABLE_SIZE = sizeof(COLOR_TABLE) / sizeof(COLOR_TABLE[0]);

// trouve le nom d'une couleur par son ID
inline const char* colorNameById(uint8_t id) {
    for (uint8_t i = 0; i < COLOR_TABLE_SIZE; i++) {
        if (COLOR_TABLE[i].id == id) return COLOR_TABLE[i].name;
    }
    return "?";
}

#endif
