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

void Pmul2Com::sendScanResult(uint16_t itemId, ItemStatus status) {
    _transfer.packet.txBuff[0] = (itemId >> 8) & 0xFF;
    _transfer.packet.txBuff[1] = itemId & 0xFF;
    _transfer.packet.txBuff[2] = static_cast<uint8_t>(status);
    _transfer.sendData(3, PID_SCAN_RESULT);
}

void Pmul2Com::sendLocalOrder(uint8_t teamId, uint8_t lineCount, const uint8_t* colors, const uint8_t* qtys) {
    // payload: teamId(1) + lineCount(1) + [color(1) + qty(1)] * N
    _transfer.packet.txBuff[0] = teamId;
    _transfer.packet.txBuff[1] = lineCount;
    for (uint8_t i = 0; i < lineCount && i < 8; i++) {
        _transfer.packet.txBuff[2 + i * 2]     = colors[i];
        _transfer.packet.txBuff[2 + i * 2 + 1] = qtys[i];
    }
    _transfer.sendData(2 + lineCount * 2, PID_LOCAL_ORDER);
}

// lecture Pi vers Arduino

bool Pmul2Com::readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId) {
    if (!_checkPacket(PID_ITEM_INFO)) return false;

    // payload: itemId (2 bytes big-endian) + decision (1 byte) + orderId (1 byte) = 4 bytes
    uint8_t high, low, rawDecision;
    _transfer.packet.rxObj(high, 0);
    _transfer.packet.rxObj(low, 1);
    _transfer.packet.rxObj(rawDecision, 2);
    _transfer.packet.rxObj(orderId, 3);

    _consumePacket();

    itemId = ((uint16_t)high << 8) | low;

    switch (rawDecision) {
        case static_cast<uint8_t>(ItemDecision::ORDER): decision = ItemDecision::ORDER; break;
        case static_cast<uint8_t>(ItemDecision::STOCK): decision = ItemDecision::STOCK; break;
        default:                                        decision = ItemDecision::PASS;  break;
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
