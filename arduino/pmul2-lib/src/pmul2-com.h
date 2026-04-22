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

        // lire une commande envoyée par le raspberry pi
        bool readOrder(Order& order);

        // lire une couleur détectée envoyée par le raspberry pi
        bool readColor(Color& color);

    private:
        Stream& _stream;
        static const uint8_t START_BYTE = 0x02; // Debut de trame
        static const uint8_t END_BYTE   = 0x03; // Fin de trame
};

#endif
