#ifndef PMUL2_KEYPAD_H
#define PMUL2_KEYPAD_H

#include <Arduino.h>
#include <Keypad.h>

// 4x4 matrix keypad sur pins 22-29 du MEGA
// Rangees 22-25, Colonnes 26-29
class Pmul2Keypad {
    public:
        Pmul2Keypad();
        // retourne la touche pressee, ou '\0' si rien
        char read();

    private:
        static const byte ROWS = 4;
        static const byte COLS = 4;
        byte _rowPins[ROWS];
        byte _colPins[COLS];
        char _keys[ROWS][COLS];
        Keypad* _keypad;
};

#endif
