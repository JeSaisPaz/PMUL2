/*
 * pmul2-encoder.h
 * Driver pour encodeur rotatoire avec bouton poussoir.
 * Compatible Arduino Mega 2560 — polling logiciel (pas d'interruption).
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT
 */

#ifndef PMUL2_ENCODER_H
#define PMUL2_ENCODER_H

#include <Arduino.h>

class Pmul2Encoder {
    public:
        Pmul2Encoder();

        int8_t readDelta();
        bool pressed();

    private:
        static const uint8_t CLK_PIN = 22;
        static const uint8_t DT_PIN  = 23;
        static const uint8_t SW_PIN  = 24;

        static const unsigned long DEBOUNCE_MS = 30;

        uint8_t _clkPrev;
        uint8_t _clkCurr;
        int8_t _delta;

        bool _btnStable;
        bool _btnLastStable;
        bool _btnFlag;
        unsigned long _btnLastTime;
        bool _btnCurr;
};

#endif
