#include "pmul2-keypad.h"

Pmul2Keypad::Pmul2Keypad() {
    _rowPins[0] = 22; _rowPins[1] = 23; _rowPins[2] = 24; _rowPins[3] = 25;
    _colPins[0] = 26; _colPins[1] = 27; _colPins[2] = 28; _colPins[3] = 29;

    _keys[0][0] = '1'; _keys[0][1] = '2'; _keys[0][2] = '3'; _keys[0][3] = 'A';
    _keys[1][0] = '4'; _keys[1][1] = '5'; _keys[1][2] = '6'; _keys[1][3] = 'B';
    _keys[2][0] = '7'; _keys[2][1] = '8'; _keys[2][2] = '9'; _keys[2][3] = 'C';
    _keys[3][0] = '*'; _keys[3][1] = '0'; _keys[3][2] = '#'; _keys[3][3] = 'D';

    _keypad = new Keypad(makeKeymap(_keys), _rowPins, _colPins, ROWS, COLS);
}

char Pmul2Keypad::read() {
    return _keypad->getKey();
}
