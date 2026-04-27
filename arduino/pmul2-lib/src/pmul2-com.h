#ifndef PMUL2_COM_H
#define PMUL2_COM_H

#include <Arduino.h>
#include "pmul2-colors.h"
#include "pmul2-orders.h"

class Pmul2Com {
    public:
        // constructeur
        explicit Pmul2Com(Stream& stream);

        // envoyer une update sur la commande vers le raspberry pi
        void sendOrderUpdate(const Order& order);

        // Signale au raspberry que la commande est terminée
        void sendOrderDone();

        // Signale que l'Arduino est occupe
        void sendBusy();
        
        // Signale que l'Arduino est disponible
        void sendReady();

        // lire une commande envoyée par le raspberry pi
        bool readTargetOrder(Order& order);

        // lire les informations d'un block envoyée par le raspberry pi
        bool readBlockInfo(Color& color, Team& team);

    private:
        Stream& _stream;
        static const uint8_t START_BYTE          = 0x02; // Debut de trame
        static const uint8_t END_BYTE            = 0x03; // Fin de trame
        static const uint8_t TARGET_ORDER_PREFIX = 0xFF; // Prefix d'une commande que l'ont doit executer
        static const uint8_t STATUS_PREFIX        = 0xFE; // Prefix d'un status
};

#endif
