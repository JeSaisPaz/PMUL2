#include "pmul2-com.h"

Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {}

void Pmul2Com::sendOrderUpdate(const Order& order) {
    uint8_t checksum = (order.teamNumber + order.blueAmount + order.yellowAmount + order.magentaAmount) & 0xFF;
    _stream.write(START_BYTE);
    _stream.write(order.teamId);
    _stream.write(order.blueAmount);
    _stream.write(order.yellowAmount);
    _stream.write(order.magentaAmount);
    _stream.write(checksum);
    _stream.write(END_BYTE);
}

bool Pmul2Com::readOrder(Order& order, Team& team) {
    if (_stream.available() < 6) { 
        return false;
    }

    if (_stream.read() != START_BYTE) {
        return false;
    }

    uint8_t teamNumber       = _stream.read();
    uint8_t blueAmount       = _stream.read();
    uint8_t yellowAmount     = _stream.read();
    uint8_t magentaAmount    = _stream.read();
    uint8_t receivedChecksum = _stream.read();
    uint8_t endByte          = _stream.read();

    if (endByte != END_BYTE) {
        return false;
    }

    uint8_t calculatedChecksum = (teamNumber + blueAmount + yellowAmount + magentaAmount) & 0xFF;

    if (receivedChecksum != calculatedChecksum) {
        return false;
    }

    order.teamNumber       = teamNumber;
    order.blueAmount       = blueAmount;
    order.yellowAmount     = yellowAmount;
    order.magentaAmount    = magentaAmount;

    return true;
}

bool Pmul2Com::readBlockInfo(Color& color, Team& team) {
    if (_stream.available() < 4) {
        return false;
    }

    if (_stream.read() != START_BYTE) { 
        return false;
    }

    uint8_t rawColor     = _stream.read();
    uint8_t rawTeam      = _stream.read();
    uint8_t endByte      = _stream.read();

    if (endByte != END_BYTE) {
        return false;
    }

    switch (rawColor) {
        case static_cast<uint8_t>(Color::Yellow):
            color = Color::Yellow;
            break;
        case static_cast<uint8_t>(Color::Blue):
            color = Color::Blue;
            break;
        case static_cast<uint8_t>(Color::Magenta):
            color = Color::Magenta;
            break;
        default:
            return false;
    }

    switch (rawTeam) {
    case static_cast<uint8_t>(Team::Team01):
        team = Team::Team01;
        break;
    case static_cast<uint8_t>(Team::Team02):
        team = Team::Team02;
        break;
    case static_cast<uint8_t>(Team::Team03):
        team = Team::Team03;
        break;
    case static_cast<uint8_t>(Team::Team04):
        team = Team::Team04;
        break;
    case static_cast<uint8_t>(Team::TeamUnknown):
        team = Team::TeamUnknown;
        break;
    
    default:
        team = Team::TeamUnknown;
        break;
    }

    return true;
}
