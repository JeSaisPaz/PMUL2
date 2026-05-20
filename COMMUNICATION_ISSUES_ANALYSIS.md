# Communication Issues Analysis: Arduino ↔ Raspberry Pi

## Executive Summary
After analyzing the communication between `final.ino` (Arduino) and `final.py` (Raspberry Pi), I've identified several critical issues that could cause communication failures, timing problems, and system instability.

---

## 🔴 CRITICAL ISSUES

### 1. **Serial Port Mismatch**
**Arduino Side:**
- Uses `Serial` for Raspberry Pi communication (USB serial)
- Uses `Serial1` for debug output
```cpp
Pmul2Lib objetPmul(Serial);  // line 18 in final.ino
Serial1.println("[SYS] ON");  // debug output
```

**Raspberry Pi Side:**
- Correctly uses USB serial (`/dev/ttyUSB0` or `/dev/ttyUSB1`)
- Both set to 9600 baud

**Issue:** The Arduino library `pmul2-com.cpp` has a problematic constructor:
```cpp
Pmul2Com::Pmul2Com(Stream& stream) : _stream(stream) {
    _transfer.begin(_stream, false, Serial1, 50);  // Serial1 used for debug!
}
```
This passes `Serial1` as debug output to SerialTransfer, which could interfere with communication if Serial1 is not properly initialized.

---

### 2. **State Machine Synchronization Issues**

**Problem:** The Arduino's new state machine can get stuck waiting for sensor confirmations that may never arrive.

**Scenario:**
1. Arduino is in State 5 (waiting for confirmation)
2. If a box doesn't reach the confirmation sensor (IR_ORDER/IR_STOCK/IR_PASS), the system stays stuck
3. No timeout mechanism to recover

**Current Code (State 5):**
```cpp
case 5: {
    bool confirmed = false;
    switch(currentDecision) {
        case ItemDecision::ORDER:
            if(etatsIR[IR_ORDER]) {  // Waits forever if sensor never triggers
                confirmed = true;
            }
            break;
    }
    if(confirmed) {
        etapeActu = 0;  // Only resets if confirmed
    }
}
```

---

### 3. **Race Condition in State 4**

**Issue:** The static variable `previousBoxCleared` in State 4 can cause issues:
```cpp
case 4: {
    static bool previousBoxCleared = false;  // Static persists across calls!
    // ...
}
```

**Problems:**
- If system is stopped/restarted while in State 4, `previousBoxCleared` retains its value
- Could miss detecting the next box or block incorrectly

---

### 4. **Missing Communication Handshake**

**Arduino → Pi Flow:**
1. Arduino sends `sendScanNeeded()` (State 0)
2. Immediately moves to State 1 waiting for response
3. **No timeout** - if Pi doesn't respond, Arduino waits forever

**Pi → Arduino Flow:**
1. Pi receives scan request
2. Camera capture/QR decode could fail
3. Backend API could timeout (5 second timeout)
4. **No error feedback to Arduino** - Arduino keeps waiting

---

### 5. **Sensor Reading Logic Issues**

**Arduino reads sensors:**
```cpp
for (byte i = 0; i < 5; i++) {
    etatsIR[i] = !digitalRead(pinsIR[i]); // Inverts for true = detected
}
```

**But sends to Pi:**
```cpp
objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], etatsIR[2], etatsIR[3], etatsIR[4]);
```

**Pi interprets:**
```python
sensors = [
    {"name": "IR 1", "state": 1 if mask & 0x01 else 0},  # 1 = detected
    # ...
]
```

This is correct, but the frequent sensor status updates (every loop iteration) could flood the serial buffer.

---

## 🟡 MODERATE ISSUES

### 6. **No Error Recovery Mechanism**

**Missing Features:**
- No timeout for waiting states
- No retry mechanism for failed scans
- No way to skip a stuck box
- No manual override capability

### 7. **Serial Buffer Overflow Risk**

**Issue:** Arduino sends sensor status EVERY loop iteration:
```cpp
void loop() {
    // ...
    objetPmul.sendSensorStatus(...);  // Sends every 50ms minimum
    // ...
}
```

With 5 sensors + overhead, this is ~10 bytes every 50ms = 200 bytes/second just for sensors.
At 9600 baud = 960 bytes/second max throughput, sensor updates alone use 20% of bandwidth.

### 8. **Backend Communication Delays**

**Pi → Backend timeouts:**
- Scan POST: 5 seconds
- Color fetch: 3 seconds  
- Sensor status: 2 seconds

If backend is slow, the entire system blocks.

---

## 🟢 RECOMMENDATIONS

### 1. **Add Timeout Mechanism**
```cpp
// In final.ino
unsigned long stateStartTime = 0;
const unsigned long STATE_TIMEOUT = 5000; // 5 seconds

case 1: {  // Waiting for scan
    if (millis() - stateStartTime > STATE_TIMEOUT) {
        // Timeout - skip this box or retry
        etapeActu = 0;  // Reset
        currentDecision = ItemDecision::PASS;  // Default to pass
    }
    // ... existing code
}
```

### 2. **Fix Static Variable Issue**
```cpp
// Move outside switch statement
bool previousBoxCleared = false;

// Reset when entering state 4
case 4: {
    if (etapeActu != previousState) {
        previousBoxCleared = false;  // Reset on state entry
    }
    // ... rest of code
}
```

### 3. **Reduce Sensor Status Frequency**
```cpp
// Only send sensor status when it changes or every N ms
static uint8_t lastSensorMask = 0;
static unsigned long lastSensorSend = 0;

uint8_t currentMask = (etatsIR[0] ? 0x01 : 0) | /* ... */;
if (currentMask != lastSensorMask || millis() - lastSensorSend > 1000) {
    objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], /* ... */);
    lastSensorMask = currentMask;
    lastSensorSend = millis();
}
```

### 4. **Add Error Feedback**
In `final.py`, send error status back to Arduino:
```python
def handleScanNeeded():
    try:
        # ... existing scan code
    except Exception as e:
        # Send error packet to Arduino
        st.send(SerialTransfer.PID_STATUS, bytes([0xFF]))  # Error code
```

### 5. **Add Manual Override**
```cpp
// Check for manual intervention via button or keypad
if (digitalRead(OVERRIDE_BUTTON) == LOW) {
    // Force state progression
    etapeActu = 0;
    currentDecision = ItemDecision::PASS;
}
```

### 6. **Fix SerialTransfer Debug Port**
Ensure Serial1 is properly initialized in `setup()`:
```cpp
void setup() {
    Serial.begin(9600);   // Pi communication
    Serial1.begin(9600);  // Debug output - MUST be initialized!
    // ...
}
```

---

## 📊 Impact Assessment

| Issue | Severity | Likelihood | Impact |
|-------|----------|------------|---------|
| State machine stuck | HIGH | Medium | System halt |
| Serial buffer overflow | MEDIUM | High | Lost packets |
| Backend timeout | MEDIUM | Low | Delayed processing |
| Race condition | HIGH | Low | Wrong box routing |
| No error recovery | HIGH | High | Manual intervention needed |

---

## 🚀 Quick Fixes Priority

1. **Immediate:** Add timeout to State 1 (waiting for scan)
2. **Immediate:** Fix static variable in State 4
3. **Important:** Reduce sensor status frequency
4. **Important:** Add timeout to State 5 (confirmation)
5. **Nice to have:** Add error feedback from Pi
6. **Nice to have:** Manual override capability

---

## Testing Recommendations

1. **Test timeout scenarios:**
   - Disconnect Pi while box is waiting
   - Block backend API
   - Cover confirmation sensors

2. **Test edge cases:**
   - Rapid box succession
   - System restart mid-process
   - Camera failure scenarios

3. **Monitor serial traffic:**
   - Use serial monitor to check packet frequency
   - Verify no buffer overflows
   - Check CRC error rates

4. **Stress test:**
   - Run continuously for 1+ hours
   - Send boxes rapidly
   - Mix different decision types