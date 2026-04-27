/*
 * pmul2-lib.h
 * Librairie développée dans le cadre du projet multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rendre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis B., Adnane O.B.T., Loic V.C.
 * Date: 20/04/2026
 * License: MIT (We love open source around here.)
 */

#ifndef PMUL2_LIB_H // protection au cas ou quelqu'un inclut 2 fois
#define PMUL2_LIB_H

#include <Arduino.h>
#include "pmul2-colors.h"
#include "pmul2-orders.h"
#include "pmul2-com.h"

class Pmul2Lib {
    public:
        // fonctions et attributs publics

        // constructeur
        Pmul2Lib(Stream &serialPort);
        // Ecriture vers le raspberry
        // fonction pour avoir un print de la version de la librairie dans la communication serie
        void version();
        // envoie une mise à jour de commande vers le Raspberry Pi
        void sendOrder(const Order& order);
        // envoie que la commande est finie
        void sendOrderDone();
        // envoie que l'arduino est occupé
        void sendBusy();
        // envoie que l'arduino est disponible
        void sendReady();
        
        // Lecture depuis le raspberry
        // lit une information envoyée par le Raspberry Pi
        bool readTargetOrder(Order& order);
        bool readInfo(Color& detected);
        
    private:
        // fonctions et attributs privés
        Stream* _serial;
        Pmul2Com _com;
};

#endif
