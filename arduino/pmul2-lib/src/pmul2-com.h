#ifndef PMUL2_COM_H
#define PMUL2_COM_H

#include <Arduino.h>
#include "SerialTransfer.h"
#include "pmul2-colors.h"
#include "pmul2-orders.h"

class Pmul2Com {
    public:
        enum class FrameType : uint8_t {
            NONE         = 0x00,
            TARGET_ORDER = 0x01,
            BLOCK_INFO   = 0x02
        };

        // IDs des packets pour SerialTransfer
        static const uint8_t PID_PING         = 0x00; // diag: ping/pong
        static const uint8_t PID_TARGET_ORDER = 0x01; // Commande a executer
        static const uint8_t PID_BLOCK_INFO   = 0x02; // Info d'un block scanne
        static const uint8_t PID_ORDER_UPDATE = 0x03; // Mise a jour de la progression
        static const uint8_t PID_STATUS       = 0xFE; // Status Arduino

        // constructeur
        explicit Pmul2Com(Stream& stream);

        // envoyer une update sur la commande vers le raspberry pi
        void sendOrderUpdate(const Order& order);

        // envoyer une commande cible vers le raspberry pi (avec prefix 0xFF)
        void sendTargetOrder(const Order& order);

        // Signale au raspberry que la commande est terminee
        void sendOrderDone();

        // Signale que l'Arduino est occupe
        void sendBusy();
        
        // Signale que l'Arduino est disponible
        void sendReady();

        // Signale au Pi qu'un bloc est en position de scan (IR1 declenche)
        void sendScanNeeded();

        // diag: repond pong a un ping
        void sendPong();

        // lire une commande envoyee par le raspberry pi
        bool readTargetOrder(Order& order);

        // lire les informations d'un block envoyee par le raspberry pi
        bool readBlockInfo(Color& color, Team& team);

        // dispatcheur: lit n'importe quelle trame entrante et retourne son type
        FrameType readFrame(Order& order, Color& color, Team& team);

        // diag: si un ping est dans le buffer, renvoie un pong
        bool handlePing();

    private:
        Stream& _stream;
        SerialTransfer _transfer;

        // Cache pour le dernier packet recu (evite de perdre un packet si on appelle
        // la mauvaise methode de lecture)
        bool _packetReady = false;
        uint8_t _lastPacketID = 0;

        // Codes de status internes
        static const uint8_t STATUS_READY = 0x00;
        static const uint8_t STATUS_BUSY  = 0x01;
        static const uint8_t STATUS_DONE  = 0x02;
        static const uint8_t STATUS_SCAN_NEEDED = 0x03; // bloc en position, scanne !

        // Helper: poke SerialTransfer pour voir si un packet est dispo
        // et le met en cache si oui
        void _poll();

        // Helper: verifie si un packet du type attendu est dans le cache
        bool _checkPacket(uint8_t expectedPID);

        // Helper: marque le packet en cache comme consomme
        void _consumePacket();

        // Helpers pour packer/unpacker les donnees dans le buffer de SerialTransfer
        void _packOrder(const Order& order);
        void _unpackOrder(Order& order);
};

#endif
