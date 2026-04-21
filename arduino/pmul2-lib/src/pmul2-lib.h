/*
 * pmul2-lib.cpp
 * Librairie dévloppée dans le cadre du project multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rentre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis, B. Adnane, OBT. Loic VC.
 * Date: 20/04/2026
 * Liscence: MIT (We love open source around here.)
 */

#ifndef pmul2-lib_h // protection au cas ou quelqu'un inclut 2 fois
#define pmul2-lib_h

#include "pmul2-colors.h"
#include "pmul2-orders.h"
#include ""

class Pmul2Lib {
    public:
        // fonctions et attributs publics

        // constructeur
        Pmul2Lib(Stream &serialPort); 
        // fonction pour avoir un print de la version de la librairie dans la communication serie
        void version();      
    private:
        // fonctions et attributs privés
        Stream* _serial;
};

#endif