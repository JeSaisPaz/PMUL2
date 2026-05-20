#include "pmul2-com.h"

Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {
    _transfer.begin(_stream, false, Serial1, 50);
}

// helpers prives

void Pmul2Com::_poll() {
    if (_transfer.available() > 0 && _transfer.status == NEW_DATA) {
        _lastPacketID = _transfer.currentPacketID();
        _packetReady = true;
    }
}

bool Pmul2Com::_checkPacket(uint8_t expectedPID) {
    _poll();
    if (!_packetReady) return false;
    if (_lastPacketID == expectedPID) return true;
    _consumePacket();
    _poll();
    return _packetReady && _lastPacketID == expectedPID;
}

void Pmul2Com::_consumePacket() {
    _packetReady = false;
}

void Pmul2Com::sendOrderDone() {
    _transfer.packet.txBuff[0] = STATUS_DONE;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendBusy() {
    _transfer.packet.txBuff[0] = STATUS_BUSY;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendReady() {
    _transfer.packet.txBuff[0] = STATUS_READY;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendScanNeeded() {
    _transfer.packet.txBuff[0] = STATUS_SCAN_NEEDED;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendPong() {
    _transfer.packet.txBuff[0] = 0x01;
    _transfer.sendData(1, PID_PING);
}

void Pmul2Com::sendSensorStatus(uint8_t ir1, uint8_t ir2, uint8_t ir3, uint8_t ir4, uint8_t ir5) {
    uint8_t mask = (ir1 ? 0x01 : 0x00)
                 | (ir2 ? 0x02 : 0x00)
                 | (ir3 ? 0x04 : 0x00)
                 | (ir4 ? 0x08 : 0x00)
                 | (ir5 ? 0x10 : 0x00);
    _transfer.packet.txBuff[0] = mask;
    _transfer.sendData(1, PID_SENSOR_STATUS);
}

void Pmul2Com::sendScanResult(uint16_t itemId, ItemStatus status) {
    _transfer.packet.txBuff[0] = (itemId >> 8) & 0xFF;
    _transfer.packet.txBuff[1] = itemId & 0xFF;
    _transfer.packet.txBuff[2] = static_cast<uint8_t>(status);
    _transfer.sendData(3, PID_SCAN_RESULT);
}

void Pmul2Com::sendLocalOrder(uint8_t lineCount, const uint8_t* colors, const uint8_t* qtys) {
    // payload: lineCount(1) + [color(1) + qty(1)] * N
    _transfer.packet.txBuff[0] = lineCount;
    for (uint8_t i = 0; i < lineCount && i < 8; i++) {
        _transfer.packet.txBuff[1 + i * 2]     = colors[i];
        _transfer.packet.txBuff[1 + i * 2 + 1] = qtys[i];
    }
    _transfer.sendData(1 + lineCount * 2, PID_LOCAL_ORDER);
}

// lecture Pi vers Arduino

bool Pmul2Com::readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId,
                            uint8_t& hue, uint8_t& saturation, uint8_t& value, uint8_t& team) {
    if (!_checkPacket(PID_ITEM_INFO)) return false;

    // payload: itemId (2B) + decision (1B) + orderId (1B) + hue (1B) + sat (1B) + val (1B) + team (1B) = 8 bytes
    uint8_t high, low, rawDecision;
    _transfer.packet.rxObj(high, 0);
    _transfer.packet.rxObj(low, 1);
    _transfer.packet.rxObj(rawDecision, 2);
    _transfer.packet.rxObj(orderId, 3);
    _transfer.packet.rxObj(hue, 4);
    _transfer.packet.rxObj(saturation, 5);
    _transfer.packet.rxObj(value, 6);
    _transfer.packet.rxObj(team, 7);

    _consumePacket();

    itemId = ((uint16_t)high << 8) | low;

    switch (rawDecision) {
        case static_cast<uint8_t>(ItemDecision::ORDER): decision = ItemDecision::ORDER; break;
        case static_cast<uint8_t>(ItemDecision::STOCK): decision = ItemDecision::STOCK; break;
        case static_cast<uint8_t>(ItemDecision::PASS) : decision = ItemDecision:: PASS; break;
        default:                                        decision = ItemDecision::NO_DECISION;  break;
    }

    return true;
}

bool Pmul2Com::handlePing() {
    _poll();
    if (_packetReady && _lastPacketID == PID_PING) {
        _consumePacket();
        sendPong();
        return true;
    }
    return false;
}

bool Pmul2Com::readColorList(uint8_t* colors, uint8_t& count) {
    if (!_checkPacket(PID_COLOR_LIST)) return false;

    // payload: count(1B) + [colorId(1B)] * N
    _transfer.packet.rxObj(count, 0);

    if (count > 4) count = 4;

    for (uint8_t i = 0; i < count; i++) {
        _transfer.packet.rxObj(colors[i], 1 + i);
    }

    _consumePacket();
    return true;
}

bool Pmul2Com::readCompletedCount(uint16_t& count) {
    if (!_checkPacket(PID_COMPLETED_COUNT)) return false;

    // payload: count (2 bytes big-endian)
    uint8_t high, low;
    _transfer.packet.rxObj(high, 0);
    _transfer.packet.rxObj(low, 1);

    _consumePacket();

    count = ((uint16_t)high << 8) | low;
    return true;
}
