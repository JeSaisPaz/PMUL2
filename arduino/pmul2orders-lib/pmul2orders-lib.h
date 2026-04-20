/*
 * pmul2orders-lib.cpp
 * Librairie dévloppée dans le cadre du project multidisciplinaire 2,
 * celle-ci permet le management des commandes depuis une librairie
 * externe afin de rentre le code de notre sketch arduino principal
 * le plus lisible possible.
 * Authors: Louis, B. Adnane, OBT. Loic VC.
 * Date: 20/04/2026
 * Liscence: MIT (We love open source around here.)
 */

#ifndef pmul2orders-lib_h // protection au cas ou quelqu'un inclut 2 fois

#define pmul2orders-lib_h

class pmul2orders {
    public:
        // Fonctions et attributs publics
        Pmul2orders(Stream &serialPort); 
        void version();      
    private:
        // Fonctions et attributs privés
        Stream* _serial;
};






#endif