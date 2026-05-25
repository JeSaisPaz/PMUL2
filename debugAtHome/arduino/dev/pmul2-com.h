#ifndef PMUL2_COM_H
#define PMUL2_COM_H

#include <Arduino.h>
#include "SerialTransfer.h"

// decisions possibles pour un item (miroir du backend)
enum class ItemDecision : uint8_t {
    PASS  = 0x00,  // bloc d'une autre equipe, on le laisse passer
    ORDER = 0x01,  // bloc pour notre commande, on le garde
    STOCK = 0x02,   // bloc pour nous mais pas dans la commande, on le stock
    NO_DECISION = 0x03 // le block n'a pas encore de decision du backend
};

// status de confirmation apres tri
enum class ItemStatus : uint8_t {
    CONFIRMED = 0x00,  // tri ok
    FAILED    = 0x01   // erreur, bloc perdu ou mal aiguille
};

class Pmul2Com {
    public:
        // IDs des packets pour SerialTransfer
        static const uint8_t PID_PING          = 0x00; // diag
        static const uint8_t PID_ITEM_INFO     = 0x10; // Pi vers Arduino: info sur le bloc scanne
        static const uint8_t PID_SCAN_RESULT   = 0x11; // Arduino vers Pi: resultat du tri
        static const uint8_t PID_SENSOR_STATUS = 0x12; // Arduino vers Pi: etat capteurs IR
        static const uint8_t PID_LOCAL_ORDER   = 0x04; // Arduino vers Pi: commande keypad
        static const uint8_t PID_COLOR_LIST    = 0x05; // Pi vers Arduino: couleurs actives
        static const uint8_t PID_COMPLETED_COUNT = 0x06; // Pi vers Arduino: nb commandes completes
        static const uint8_t PID_STATUS        = 0xFE; // status Arduino (ready/busy/done/scan_needed)

        // constructeur
        explicit Pmul2Com(Stream& stream);

        // envoi (Arduino vers Pi)
        void sendOrderDone();
        void sendBusy();
        void sendReady();
        void sendScanNeeded();
        void sendPong();
        void sendSensorStatus(uint8_t ir1, uint8_t ir2, uint8_t ir3, uint8_t ir4, uint8_t ir5);
        void sendScanResult(uint16_t itemId, ItemStatus status);
        // envoie une commande saisie au keypad: lines = [{color, qty}, ...]
        void sendLocalOrder(uint8_t lineCount, const uint8_t* colors, const uint8_t* qtys);

        // lecture (Pi vers Arduino)
        bool readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId);
        // recoit la liste des couleurs actives (max 4)
        bool readColorList(uint8_t* colors, uint8_t& count);
        // recoit le nombre de commandes completes du backend
        bool readCompletedCount(uint16_t& count);
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
};

#endif
