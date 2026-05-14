#include "pmul2-encoder.h"

Pmul2Encoder* Pmul2Encoder::_instance = nullptr;

Pmul2Encoder::Pmul2Encoder() {
    _instance = this;
    _delta = 0;
    _btnPressed = false;
    _lastBtn = true;

    pinMode(PIN_CLK, INPUT_PULLUP);
    pinMode(PIN_DT, INPUT_PULLUP);
    pinMode(PIN_SW, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_CLK), isrCLK, RISING);
}

void Pmul2Encoder::isrCLK() {
    if (!_instance) return;
    if (digitalRead(_instance->PIN_DT) == HIGH) {
        _instance->_delta++;
    } else {
        _instance->_delta--;
    }
}

int8_t Pmul2Encoder::readDelta() {
    noInterrupts();
    int8_t d = _delta;
    _delta = 0;
    interrupts();
    return d;
}

bool Pmul2Encoder::pressed() {
    bool now = digitalRead(PIN_SW);
    if (_lastBtn && !now) {
        _lastBtn = now;
        return true;
    }
    _lastBtn = now;
    return false;
}
