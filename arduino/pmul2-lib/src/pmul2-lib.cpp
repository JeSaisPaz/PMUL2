/*
 * pmul2orders-lib.cpp
 * Librairie dévloppée dans le cadre du project multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rentre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis, B. Adnane, OBT. Loic VC.
 * Date: 20/04/2026
 * Liscence: MIT (We love open source around here.)
 * Version: alpha-v0.0.2
 */

#include "Arduino.h"
#include "pmul2orders-lib.h"

Pmul2Lib::Pmul2Lib(Stream &serialPort) {
    _serial = &serialPort;
}

 void Pmul2Lib::version() {
    _serial->println("Pmul2orders version: alpha-v0.0.1");
 }