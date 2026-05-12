#include "pmul2-com.h"

Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {
    // SerialTransfer en mode silencieux (debug sur Serial1 pour pas polluer le port de com)
    _transfer.begin(_stream, false, Serial1, 50);
}

// --- Helpers prives pour packer/unpacker les donnees ---

void Pmul2Com::_packOrder(const Order& order) {
    _transfer.packet.txObj(order.teamId, 0);
    _transfer.packet.txObj(order.blueAmount, 1);
    _transfer.packet.txObj(order.yellowAmount, 2);
    _transfer.packet.txObj(order.magentaAmount, 3);
}

void Pmul2Com::_unpackOrder(Order& order) {
    _transfer.packet.rxObj(order.teamId, 0);
    _transfer.packet.rxObj(order.blueAmount, 1);
    _transfer.packet.rxObj(order.yellowAmount, 2);
    _transfer.packet.rxObj(order.magentaAmount, 3);
}

// --- Helpers prives pour le cache de packets ---

void Pmul2Com::_poll() {
    // toujours pomper les bytes du stream, meme si on a un packet en cache
    // comme ca si un packet du mauvais type est arrive, il se fait ecraser
    // par le suivant au lieu de bloquer le systeme
    if (_transfer.available() > 0 && _transfer.status == NEW_DATA) {
        _lastPacketID = _transfer.currentPacketID();
        _packetReady = true;
    }
}

bool Pmul2Com::_checkPacket(uint8_t expectedPID) {
    _poll();
    if (!_packetReady) return false;

    if (_lastPacketID == expectedPID) return true;

    // packet du mauvais type — on le degage pour pas rester coince
    _consumePacket();
    // on retente direct au cas ou le bon serait arrive en dessous
    _poll();
    return _packetReady && _lastPacketID == expectedPID;
}

void Pmul2Com::_consumePacket() {
    _packetReady = false;
}

// --- Envoi de donnees vers le Raspberry Pi ---

void Pmul2Com::sendOrderUpdate(const Order& order) {
    // Packet ID 0x03: mise a jour de progression
    _packOrder(order);
    _transfer.sendData(4, PID_ORDER_UPDATE);
}

void Pmul2Com::sendTargetOrder(const Order& order) {
    // Packet ID 0x01: commande a executer (envoyee par le raspberry, recue par l'arduino)
    // Mais on la garde pour symetrie au cas ou l'Arduino doit renvoyer une commande
    _packOrder(order);
    _transfer.sendData(4, PID_TARGET_ORDER);
}

void Pmul2Com::sendOrderDone() {
    // Packet ID 0xFE status 0x02: commande terminee
    _transfer.packet.txBuff[0] = STATUS_DONE;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendBusy() {
    // Packet ID 0xFE status 0x01: arduino occupe
    _transfer.packet.txBuff[0] = STATUS_BUSY;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendReady() {
    // Packet ID 0xFE status 0x00: arduino dispo
    _transfer.packet.txBuff[0] = STATUS_READY;
    _transfer.sendData(1, PID_STATUS);
}

void Pmul2Com::sendScanNeeded() {
    // Packet ID 0xFE status 0x03: bloc bloque, le Pi doit scanner maintenant
    _transfer.packet.txBuff[0] = STATUS_SCAN_NEEDED;
    _transfer.sendData(1, PID_STATUS);
}

// --- Lecture de donnees depuis le Raspberry Pi ---

bool Pmul2Com::readTargetOrder(Order& order) {
    // Verifie si on a un packet TARGET_ORDER dans le cache
    if (!_checkPacket(PID_TARGET_ORDER)) return false;

    // Extraction: teamId (1 byte) + blueAmount (1) + yellowAmount (1) + magentaAmount (1) = 4 bytes
    _unpackOrder(order);
    _consumePacket();
    return true;
}

bool Pmul2Com::readBlockInfo(Color& color, Team& team) {
    // Verifie si on a un packet BLOCK_INFO dans le cache
    if (!_checkPacket(PID_BLOCK_INFO)) return false;

    // Extraction: color (1 byte) + team (1 byte) = 2 bytes
    uint8_t rawColor;
    uint8_t rawTeam;

    _transfer.packet.rxObj(rawColor, 0);
    _transfer.packet.rxObj(rawTeam, 1);

    _consumePacket();

    // Conversion couleur
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

    // Conversion team
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
    // Dispatcheur: poke SerialTransfer, puis route selon le Packet ID
    _poll();

    if (!_packetReady) return FrameType::NONE;

    switch (_lastPacketID) {
        case PID_TARGET_ORDER: {
            _unpackOrder(order);
            _consumePacket();
            return FrameType::TARGET_ORDER;
        }

        case PID_BLOCK_INFO: {
            uint8_t rawColor;
            uint8_t rawTeam;

            _transfer.packet.rxObj(rawColor, 0);
            _transfer.packet.rxObj(rawTeam, 1);
            _consumePacket();

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

        default:
            // Packet inconnu, on le jette pour pas rester bloque
            _consumePacket();
            return FrameType::NONE;
    }
}
