/*
 * pmul2-lib.cpp
 * Librairie développée dans le cadre du projet multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rendre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT (We love open source around here.)
 * Version: alpha-v0.0.2
 */

#include <Arduino.h>
#include "pmul2-lib.h"

Pmul2Lib::Pmul2Lib(Stream &serialPort) : _com(serialPort) {
    _serial = &serialPort;
}

void Pmul2Lib::version() {
    _serial->println("Pmul2Lib version: alpha-v0.0.2");
}

void Pmul2Lib::sendOrder(const Order& order) {
    _com.sendOrderUpdate(order);
}

bool Pmul2Lib::readColor(Color& detected) {
    return _com.readColor(detected);
}
