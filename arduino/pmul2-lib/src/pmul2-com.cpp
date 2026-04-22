#include "pmul2-com.h"

// constructeur
Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {}

// envoyer une update sur la commande vers le raspberry pi
void Pmul2Com::sendOrderUpdate(const Order& order) {
    uint8_t checksum = order.blueAmount + order.yellowAmount + order.magentaAmount + order.otherColorAmount;

    _stream.write(START_BYTE);
    _stream.write(order.blueAmount);
    _stream.write(order.yellowAmount);
    _stream.write(order.magentaAmount);
    _stream.write(order.otherColorAmount);
    _stream.write(checksum);
    _stream.write(END_BYTE);
}

// lire une commande du raspberry
bool Pmul2Com::readOrder(Order& order) {
    // START_BYTE + nombre de boite de chaque couleur + CHECKSUM + END_BYTE
    // INFO: c'est _stream.available() < 7 et pas <= 7 car on veut s'assurer d'avoir au moins 7 octets à lire, sinon on risque de lire une trame incomplète (J'ai eu la blague)
    if (_stream.available() < 7) { 
        return false;
    }

    if (_stream.read() != START_BYTE) {
        return false;
    }

    uint8_t blueAmount       = _stream.read();
    uint8_t yellowAmount     = _stream.read();
    uint8_t magentaAmount    = _stream.read();
    uint8_t otherColorAmount = _stream.read();
    uint8_t receivedChecksum = _stream.read();
    uint8_t endByte          = _stream.read();

    if (endByte != END_BYTE) {
        return false;
    }

    uint8_t calculatedChecksum = blueAmount + yellowAmount + magentaAmount + otherColorAmount;

    if (receivedChecksum != calculatedChecksum) {
        return false;
    }

    order.blueAmount       = blueAmount;
    order.yellowAmount     = yellowAmount;
    order.magentaAmount    = magentaAmount;
    order.otherColorAmount = otherColorAmount;

    return true;
}

// lire une couleur détectée envoyée par le raspberry pi
// Trame : [START][color][END]
// TODO: Implementer [teamNumber] dans la trame
bool Pmul2Com::readColor(Color& color) {
    if (_stream.available() < 3) {
        return false;
    }

    if (_stream.read() != START_BYTE) {
        return false;
    }

    uint8_t raw     = _stream.read();
    uint8_t endByte = _stream.read();

    if (endByte != END_BYTE) {
        return false;
    }

    switch (raw) {
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
            color = Color::Other;
            break;
    }

    return true;
}
