#ifndef PMUL2_COLORS_H
#define PMUL2_COLORS_H

#include <stdint.h>

// IDs des couleurs pour le keypad (miroir du backend)
// A=Bleu, B=Jaune, C=Magenta
enum class KeypadColor : uint8_t {
    BLUE    = 0x02,
    YELLOW  = 0x01,
    MAGENTA = 0x03,
    NONE    = 0x00
};

// mapping touche keypad vers couleur
inline KeypadColor keyToColor(char key) {
    switch (key) {
        case 'A': return KeypadColor::BLUE;
        case 'B': return KeypadColor::YELLOW;
        case 'C': return KeypadColor::MAGENTA;
        default:  return KeypadColor::NONE;
    }
}

inline const char* colorName(KeypadColor c) {
    switch (c) {
        case KeypadColor::BLUE:    return "Bleu";
        case KeypadColor::YELLOW:  return "Jaune";
        case KeypadColor::MAGENTA: return "Magenta";
        default:                   return "?";
    }
}

#endif
