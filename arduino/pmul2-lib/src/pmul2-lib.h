/*
 * pmul2-lib.h
 * Librairie developpee dans le cadre du projet multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rendre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT (We love open source around here.)
 */

#ifndef PMUL2_LIB_H
#define PMUL2_LIB_H

#include <Arduino.h>
#include "pmul2-com.h"

class Pmul2Lib {
    public:
        // constructeur
        Pmul2Lib(Stream &serialPort);

        // version de la lib
        void version();

        // ecriture vers le Pi/backend
        void sendOrderDone();
        void sendBusy();
        void sendReady();
        void sendScanNeeded();
        void sendPong();
        void sendScanResult(uint16_t itemId, ItemStatus status);

        // lecture depuis le Pi/backend
        bool readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId);
        bool handlePing();

    private:
        Stream* _serial;
        Pmul2Com _com;
};

#endif
