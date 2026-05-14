#ifndef PMUL2_ENCODER_H
#define PMUL2_ENCODER_H

#include <Arduino.h>

// encodeur rotatoire avec bouton poussoir
// CLK=22, DT=23, SW=24 (interrupts)

class Pmul2Encoder {
    public:
        Pmul2Encoder();

        // lecture du delta depuis le dernier appel (-1, 0, +1)
        int8_t readDelta();

        // true si le bouton vient d'etre presse (front descendant)
        bool pressed();

    private:
        static const uint8_t PIN_CLK = 22;
        static const uint8_t PIN_DT  = 23;
        static const uint8_t PIN_SW  = 24;

        volatile int8_t _delta;
        volatile bool   _btnPressed;
        bool            _lastBtn;

        static void isrCLK();
        static void isrDT();  // unused, handled in isrCLK

        static Pmul2Encoder* _instance;
};

#endif
