#include "pmul2-com.h"

Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {}

void Pmul2Com::sendOrderUpdate(const Order& order) {
    uint8_t checksum = (order.teamId + order.blueAmount + order.yellowAmount + order.magentaAmount) & 0xFF;
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

void Pmul2Com::sendTargetOrder(const Order& order) {
    // Trame: [START_BYTE][TARGET_ORDER_PREFIX][teamId][blueAmount][yellowAmount][magentaAmount][checksum][END_BYTE]
    uint8_t checksum = (order.teamId + order.blueAmount + order.yellowAmount + order.magentaAmount) & 0xFF;
    _stream.write(START_BYTE);
    _stream.write(TARGET_ORDER_PREFIX);
    _stream.write(order.teamId);
    _stream.write(order.blueAmount);
    _stream.write(order.yellowAmount);
    _stream.write(order.magentaAmount);
    _stream.write(checksum);
    _stream.write(END_BYTE);
}

void Pmul2Com::sendBusy() {
    // Trame: [START_BYTE][STATUS_PREFIX][I'M BUSY][END_BYTE]
    _stream.write(START_BYTE);
    _stream.write(STATUS_PREFIX);
    _stream.write((uint8_t)0x01);
    _stream.write(END_BYTE);
}

void Pmul2Com::sendReady() {
    // Trame: [START_BYTE][STATUS_PREFIX][I'M READY][END_BYTE]
    _stream.write(START_BYTE);
    _stream.write(STATUS_PREFIX);
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

Pmul2Com::FrameType Pmul2Com::readFrame(Order& order, Color& color, Team& team) {
    // trame la plus petite = BlockInfo (4 bytes)
    if (_stream.available() < 4) {
        return FrameType::NONE;
    }

    // on regarde le START_BYTE sans le consommer pour pas le perdre
    // si le reste de la trame est pas encore arrive
    if (_stream.peek() != START_BYTE) {
        _stream.read(); // on jette le byte foireux pour pas rester bloque
        return FrameType::NONE;
    }

    // START_BYTE confirme, on le consomme pour pouvoir peek le discriminateur
    _stream.read();

    // on peek le 2e byte pour savoir quel type de trame on a
    int peeked = _stream.peek();
    if (peeked == -1) {
        return FrameType::NONE;
    }
    uint8_t discriminator = (uint8_t)peeked;

    if (discriminator == TARGET_ORDER_PREFIX) {
        // trame: [START][0xFF][team][blue][yellow][magenta][checksum][END] = 8 bytes
        // on a deja lu START, il reste 7 bytes
        if (_stream.available() < 7) {
            return FrameType::NONE;
        }

        _stream.read(); // consomme le discriminator
        uint8_t teamId            = _stream.read();
        uint8_t blueTarget        = _stream.read();
        uint8_t yellowTarget      = _stream.read();
        uint8_t magentaTarget     = _stream.read();
        uint8_t receivedChecksum  = _stream.read();
        uint8_t endByte           = _stream.read();

        if (endByte != END_BYTE) {
            return FrameType::NONE;
        }

        uint8_t calculatedChecksum = (teamId + blueTarget + yellowTarget + magentaTarget) & 0xFF;

        if (receivedChecksum != calculatedChecksum) {
            return FrameType::NONE;
        }

        order.teamId        = teamId;
        order.blueAmount    = blueTarget;
        order.yellowAmount  = yellowTarget;
        order.magentaAmount = magentaTarget;

        return FrameType::TARGET_ORDER;
    }

    // le discriminateur d'un BlockInfo doit etre une couleur valide
    // on verifie AVANT de consommer plus de bytes pour eviter de corrompre le stream
    if (discriminator != static_cast<uint8_t>(Color::Yellow) &&
        discriminator != static_cast<uint8_t>(Color::Blue) &&
        discriminator != static_cast<uint8_t>(Color::Magenta)) {
        return FrameType::NONE;
    }

    // c'est un BlockInfo: [START][color][team][END] = 4 bytes
    // on a deja lu START, il reste 3 bytes
    if (_stream.available() < 3) {
        return FrameType::NONE;
    }

    _stream.read(); // consomme le discriminator (= color)
    uint8_t rawTeam = _stream.read();
    uint8_t endByte = _stream.read();

    if (endByte != END_BYTE) {
        return FrameType::NONE;
    }

    switch (discriminator) {
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
            return FrameType::NONE;
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

    return FrameType::BLOCK_INFO;
}
