#ifndef PMUL2_COM_H
#define PMUL2_COM_H

#include <Arduino.h>
#include "SerialTransfer.h"
#include "pmul2-colors.h"
#include "pmul2-orders.h"

// decisions possibles pour un item (miroir du backend)
enum class ItemDecision : uint8_t {
    PASS  = 0x00,  // bloc d'une autre equipe, on le laisse passer
    ORDER = 0x01,  // bloc pour notre commande, on le garde
    STOCK = 0x02   // bloc pour nous mais pas dans la commande, on le stock
};

// status de confirmation apres tri
enum class ItemStatus : uint8_t {
    CONFIRMED = 0x00,  // tri ok
    FAILED    = 0x01   // erreur, bloc perdu ou mal aiguille
};

class Pmul2Com {
    public:
        // IDs des packets pour SerialTransfer
        static const uint8_t PID_PING         = 0x00; // diag
        static const uint8_t PID_TARGET_ORDER = 0x01; // commande depuis le Pi
        static const uint8_t PID_ORDER_UPDATE = 0x03; // progres vers le Pi
        static const uint8_t PID_ITEM_INFO    = 0x10; // Pi vers Arduino: info sur le bloc scanne
        static const uint8_t PID_SCAN_RESULT  = 0x11; // Arduino vers Pi: resultat du tri
        static const uint8_t PID_STATUS       = 0xFE; // status Arduino (ready/busy/done/scan_needed)

        // constructeur
        explicit Pmul2Com(Stream& stream);

        // envoi (Arduino vers Pi)
        void sendOrderUpdate(const Order& order);
        void sendTargetOrder(const Order& order);
        void sendOrderDone();
        void sendBusy();
        void sendReady();
        void sendScanNeeded();
        void sendPong();
        // envoie le resultat du tri d'un bloc (itemId 2 bytes, status 1 byte)
        void sendScanResult(uint16_t itemId, ItemStatus status);

        // lecture (Pi vers Arduino)
        bool readTargetOrder(Order& order);
        // recoit l'info d'un item scanne par le Pi/backend
        bool readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId);
        // diag
        bool handlePing();

    private:
        Stream& _stream;
        SerialTransfer _transfer;

        bool _packetReady = false;
        uint8_t _lastPacketID = 0;

        static const uint8_t STATUS_READY  = 0x00;
        static const uint8_t STATUS_BUSY   = 0x01;
        static const uint8_t STATUS_DONE   = 0x02;
        static const uint8_t STATUS_SCAN_NEEDED = 0x03;

        void _poll();
        void _consumePacket();
        bool _checkPacket(uint8_t expectedPID);

        void _packOrder(const Order& order);
        void _unpackOrder(Order& order);
};

#endif
