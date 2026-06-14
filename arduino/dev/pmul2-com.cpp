#include "pmul2-com.h"

Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {
    _transfer.begin(_stream, false, Serial1, 50);

    _slotItemInfo       = {{}, 0, false};
    _slotColorList      = {{}, 0, false};
    _slotCompletedCount = {{}, 0, false};
    _slotCurrentOrder   = {{}, 0, false};
}

void Pmul2Com::_storeSlot(Slot& slot, uint8_t len) {
    uint8_t n = (len > SLOT_SIZE) ? SLOT_SIZE : len;
    for (uint8_t i = 0; i < n; i++) {
        slot.data[i] = _transfer.packet.rxBuff[i];
    }
    slot.len   = n;
    slot.ready = true;
}

void Pmul2Com::pollAll() {
    while (_transfer.available() > 0 && _transfer.status == NEW_DATA) {
        uint8_t pid = _transfer.currentPacketID();
        uint8_t len = _transfer.bytesRead;

        switch (pid) {
            case PID_ITEM_INFO:
                _storeSlot(_slotItemInfo, len);
                break;
            case PID_COLOR_LIST:
                _storeSlot(_slotColorList, len);
                break;
            case PID_COMPLETED_COUNT:
                _storeSlot(_slotCompletedCount, len);
                break;
            case PID_CURRENT_ORDER:
                _storeSlot(_slotCurrentOrder, len);
                break;
            default:
                break;
        }
    }
}

void Pmul2Com::sendScanNeeded() {
    _transfer.packet.txBuff[0] = STATUS_SCAN_NEEDED;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendSensorStatus(uint8_t ir1, uint8_t ir2, uint8_t ir3, uint8_t ir4, uint8_t ir5) {
    uint8_t mask = (ir1 ? 0x01 : 0x00) | (ir2 ? 0x02 : 0x00) | (ir3 ? 0x04 : 0x00) | (ir4 ? 0x08 : 0x00) | (ir5 ? 0x10 : 0x00);
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
    _transfer.packet.txBuff[0] = lineCount;
    for (uint8_t i = 0; i < lineCount && i < 8; i++) {
        _transfer.packet.txBuff[1 + i * 2]     = colors[i];
        _transfer.packet.txBuff[1 + i * 2 + 1] = qtys[i];
    }
    _transfer.sendData(1 + lineCount * 2, PID_LOCAL_ORDER);
}

bool Pmul2Com::readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId) {
    if (!_slotItemInfo.ready) return false;
    uint8_t high        = _slotItemInfo.data[0];
    uint8_t low         = _slotItemInfo.data[1];
    uint8_t rawDecision = _slotItemInfo.data[2];
    orderId             = _slotItemInfo.data[3];
    _slotItemInfo.ready = false;
    itemId = ((uint16_t)high << 8) | low;
    switch (rawDecision) {
        case static_cast<uint8_t>(ItemDecision::ORDER): decision = ItemDecision::ORDER; break;
        case static_cast<uint8_t>(ItemDecision::STOCK): decision = ItemDecision::STOCK; break;
        case static_cast<uint8_t>(ItemDecision::PASS) : decision = ItemDecision::PASS;  break;
        default:                                        decision = ItemDecision::NO_DECISION; break;
    }
    return true;
}

bool Pmul2Com::readColorList(uint8_t* colors, uint8_t& count) {
    if (!_slotColorList.ready) return false;
    count = _slotColorList.data[0];
    if (count > 4) count = 4;
    for (uint8_t i = 0; i < count; i++) {
        colors[i] = _slotColorList.data[1 + i];
    }
    _slotColorList.ready = false;
    return true;
}

bool Pmul2Com::readCompletedCount(uint16_t& count) {
    if (!_slotCompletedCount.ready) return false;
    count = ((uint16_t)_slotCompletedCount.data[0] << 8) | _slotCompletedCount.data[1];
    _slotCompletedCount.ready = false;
    return true;
}

bool Pmul2Com::readCurrentOrder(uint8_t& orderId, uint8_t* quantities) {
    if (!_slotCurrentOrder.ready) return false;

    orderId = _slotCurrentOrder.data[0];
    
    for (uint8_t i = 0; i < 4; i++) {
        quantities[i] = _slotCurrentOrder.data[1 + i];
    }

    _slotCurrentOrder.ready = false;
    return true;
}