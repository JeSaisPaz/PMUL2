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

void Pmul2Com::sendOrderDone() {
    // On envoie une trame avec teamId = 0x00 -> Commande terminée
    _stream.write(START_BYTE);
    _stream.write((uint8_t)0x00);
    _stream.write((uint8_t)0x00);
    _stream.write((uint8_t)0x00);
    _stream.write((uint8_t)0x00);
    _stream.write((uint8_t)0x00);
    _stream.write(END_BYTE);
}

bool Pmul2Com::readTargetOrder(Order& order) {
    // trame: [START_BYTE][team][blue][yellow][magenta][checksum][END_BYTE]
    
    if(_stream.available() < 8) {
        return false; // return si on ne respecte pas le format de la trame
    }

    if(_stream.read() != START_BYTE) {
        return false; // return si la trame ne commence pas par le bit de debut
    }

    if(_stream.read() != TARGET_ORDER_PREFIX) {
        return false; // doit etre TARGET_ORDER_PREFIX pour etre une commande valide
    }

    uint8_t teamId            = _stream.read();
    uint8_t blueTarget        = _stream.read();
    uint8_t yellowTarget      = _stream.read();
    uint8_t magentaTarget     = _stream.read();
    uint8_t receivedChecksum  = _stream.read();
    uint8_t endByte           = _stream.read();

    if (endByte != END_BYTE) {
        return false; // doit finir par le bit de fin
    }

    // vérification du checksum de la trame
    uint8_t calculatedChecksum = (teamId + blueTarget + yellowTarget + magentaTarget) & 0xFF;

    if (receivedChecksum != calculatedChecksum) {
        return false;
    }

    // enfin, affectation aux variables si tout est ok
    order.teamId        = teamId;
    order.blueAmount    = blueTarget;
    order.yellowAmount  = yellowTarget;
    order.magentaAmount = magentaTarget;

    return true;
}

bool Pmul2Com::readBlockInfo(Color& color, Team& team) {
    // trame: [START_BYTE][color][team][END_BYTE]

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
    case static_cast<uint8_t>(Team::Team05):
        team = Team::Team05;
        break;
    default:
        team = Team::TeamUnknown;
        break;
    }

    return true;
}
