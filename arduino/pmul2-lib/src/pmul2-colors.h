#ifndef PMUL2_COLORS_H
#define PMUL2_COLORS_H

#include <stdint.h>

// objet couleur, representé par un byte (plus facile a prendre par la communication serie)
enum class Color : uint8_t {
    Yellow  = 0x01,
    Blue    = 0x02,
    Magenta = 0x03,
    Other   = 0xFF
};

#endif
