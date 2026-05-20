/*
 * pmul2-encoder.cpp
 * Driver pour encodeur rotatoire avec bouton poussoir.
 * Compatible Arduino Mega 2560 — polling logiciel (pas d'interruption).
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT
 */

#include "pmul2-encoder.h"

Pmul2Encoder::Pmul2Encoder()
    : _clkPrev(HIGH), _clkCurr(HIGH), _delta(0),
      _btnStable(false), _btnLastStable(true), _btnFlag(false),
      _btnLastTime(0), _btnCurr(true)
{
    pinMode(CLK_PIN, INPUT_PULLUP);
    pinMode(DT_PIN,  INPUT_PULLUP);
    pinMode(SW_PIN,  INPUT_PULLUP);
}

int8_t Pmul2Encoder::readDelta() {
    _clkCurr = digitalRead(CLK_PIN);

    if (_clkCurr != _clkPrev) {
        if (digitalRead(DT_PIN) != _clkCurr) {
            _delta++;
        } else {
            _delta--;
        }
        _clkPrev = _clkCurr;
    }

    int8_t result = _delta;
    _delta = 0;
    return result;
}

bool Pmul2Encoder::pressed() {
    bool reading = digitalRead(SW_PIN);

    if (reading != _btnCurr) {
        _btnLastTime = millis();
    }

    _btnCurr = reading;

    if ((millis() - _btnLastTime) > DEBOUNCE_MS) {
        _btnStable = reading;
    }

    _btnFlag = false;
    if (_btnStable == LOW && _btnLastStable == HIGH) {
        _btnFlag = true;
    }

    _btnLastStable = _btnStable;
    return _btnFlag;
}
