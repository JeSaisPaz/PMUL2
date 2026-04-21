#include "pmul2-com.h"

Pmul2Com::sendOrder(const Order& order) {
    //TODO: uint8_t checksum 
    // TODO: Read pmul2-com.h (*)
    /*_stream.write(START_BYTE);
    _stream.write(order.blue);
    _stream.write(order.yellow);
    _stream.write(order.magenta);
    _stream.write(checksum);
    _stream.write(END_BYTE);*/
}

Pmul2Com::readOrder(Order& order) {
    uint8_t [] = (uint8_t)_stream.read();
    // TODO: Processing de la trame. 
    /*
        1. bite (lol) de debut ? 
        2. switch(couleur) {
        
           }
        3. bite (lol) de fin ?
    */
}