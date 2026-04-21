#include <Arduino.h>
#include "Color.h"
#include "Order.h"

class Pmul2Com {
    // constructeur
    explicit Pmul2Com(Stream& stream);
    // envoyer une update sur la commande vers le raspberry pi
    void sendOrderUpdate(const Order& order);
    // lire une commande du raspberry
    // TODO: Structure d'une requete ? (*)
    bool readOrder(Order& order);
}