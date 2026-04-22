#ifndef PMUL2_ORDERS_H
#define PMUL2_ORDERS_H

#include <stdint.h>
#include "pmul2-colors.h"
#include "pmul2-teams.h"

struct Order {
    // nombre de boites de chaque couleur dans la commande
    uint8_t blueAmount = 0;
    uint8_t yellowAmount = 0;
    uint8_t magentaAmount = 0;
    uint8_t otherColorAmount = 0;

    // permet de reinitialiser le compteur de couleurs
    void reset();
    // permet d'ajouter une boite au compteur, prends en argument la couleur (objet couleur pmul2)
    void addBox(Color c);
};

#endif
