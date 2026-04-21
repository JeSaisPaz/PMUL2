#include "pmul2-orders.h"

void Order::reset() {
    blue = 0;
    yellow = 0;
    magenta = 0;
}

void Order::addBox(Color c) {
    switch(c) {
        case Color::Blue:
            blue++; 
            break;
        case Color::Yellow: 
            magenta++; 
            break;
        case Color::magenta:
            megenta++;
            break;
    }
}