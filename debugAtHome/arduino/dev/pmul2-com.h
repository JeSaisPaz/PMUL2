#ifndef PMUL2_COM_H
#define PMUL2_COM_H

#include <Arduino.h>
#include "SerialTransfer.h"

enum class ItemDecision : uint8_t {
    PASS        = 0x00,
    ORDER       = 0x01,
    STOCK       = 0x02,
    NO_DECISION = 0x03
};

enum class ItemStatus : uint8_t {
    CONFIRMED = 0x00,
    FAILED    = 0x01
};

class Pmul2Com {
    public:
        static const uint8_t PID_ITEM_INFO       = 0x10;
        static const uint8_t PID_SCAN_RESULT     = 0x11;
        static const uint8_t PID_SENSOR_STATUS   = 0x12;
        static const uint8_t PID_LOCAL_ORDER     = 0x04;
        static const uint8_t PID_COLOR_LIST      = 0x05;
        static const uint8_t PID_COMPLETED_COUNT = 0x06;
        static const uint8_t PID_CURRENT_ORDER   = 0x07;
        static const uint8_t PID_STATUS          = 0xFE;

        explicit Pmul2Com(Stream& stream);

        void pollAll();

        void sendScanNeeded();
        void sendSensorStatus(uint8_t ir1, uint8_t ir2, uint8_t ir3, uint8_t ir4, uint8_t ir5);
        void sendScanResult(uint16_t itemId, ItemStatus status);
        void sendLocalOrder(uint8_t lineCount, const uint8_t* colors, const uint8_t* qtys);

        bool readItemInfo(uint16_t& itemId, ItemDecision& decision, uint8_t& orderId);
        bool readColorList(uint8_t* colors, uint8_t& count);
        bool readCompletedCount(uint16_t& count);
        bool readCurrentOrder(uint8_t& orderId, uint8_t* quantities);

    private:
        Stream& _stream;
        SerialTransfer _transfer;

        static const uint8_t STATUS_SCAN_NEEDED = 0x03;

        static const uint8_t SLOT_SIZE = 16;

        struct Slot {
            uint8_t data[SLOT_SIZE];
            uint8_t len;
            bool    ready;
        };

        Slot _slotItemInfo;
        Slot _slotColorList;
        Slot _slotCompletedCount;
        Slot _slotCurrentOrder;

        void _storeSlot(Slot& slot, uint8_t len);
};

#endif