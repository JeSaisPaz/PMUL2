#include "pmul2-orders.h"

void Order::reset() {
    blueAmount = 0;
    yellowAmount = 0;
    magentaAmount = 0;
}

void Order::addBox(Color c) {
    switch (c) {
        case Color::Blue:
            blueAmount++;
            break;
        case Color::Yellow:
            yellowAmount++;
            break;
        case Color::Magenta:
            magentaAmount++;
            break;
    }
}