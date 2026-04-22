#ifndef PMUL2_TEAMS_H
#define PMUL2_TEAMS_H

#include <stdint.h>

// Objet Team
enum class Team : uint8_t {
    Team01      = 0x01,
    Team02      = 0x02,
    Team03      = 0x03,
    Team04      = 0x04,
    TeamUnknown = 0xFF
};

#endif
