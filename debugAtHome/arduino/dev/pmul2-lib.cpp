/*
 * pmul2-lib.cpp
 * Librairie developpee dans le cadre du projet multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rendre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT (We love open source around here.)
 * Version: beta-v0.0.2
 */

#include <Arduino.h>
#include "pmul2-lib.h"

Pmul2Lib::Pmul2Lib(Stream &serialPort) : _com(serialPort) {
    _serial = &serialPort;
}

void Pmul2Lib::version() {
    _serial->println("Pmul2Lib version: beta-v0.0.2");
}

void Pmul2Lib::sendOrderDone() {
    _com.sendOrderDone();
}

void Pmul2Lib::sendBusy() {
    _com.sendBusy();
}

void Pmul2Lib::sendReady() {
    _com.sendReady();
}

void Pmul2Lib::sendScanNeeded() {
    _com.sendScanNeeded();
}

void Pmul2Lib::sendPong() {
    _com.sendPong();
}

void Pmul2Lib::sendScanResult(uint16_t itemId, ItemStatus status) {
    _com.sendScanResult(itemId, status);
}

void Pmul2Lib::sendSensorStatus(uint8_t ir1, uint8_t ir2, uint8_t ir3, uint8_t ir4, uint8_t ir5) {
    _com.sendSensorStatus(ir1, ir2, ir3, ir4, ir5);
}

void Pmul2Lib::sendLocalOrder(uint8_t lineCount, const uint8_t* colors, const uint8_t* qtys) {
    _com.sendLocalOrder(lineCount, colors, qtys);
}

bool Pmul2Lib::readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId) {
    return _com.readItemInfo(itemId, decision, orderId);
}

bool Pmul2Lib::readColorList(uint8_t* colors, uint8_t& count) {
    return _com.readColorList(colors, count);
}

bool Pmul2Lib::readCompletedCount(uint16_t& count) {
    return _com.readCompletedCount(count);
}

bool Pmul2Lib::handlePing() {
    return _com.handlePing();
}
