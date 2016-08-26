#ifndef RX8025_H
#define RX8025_H

#include <Wire.h>

#define RX8025_ADDR     0x32
#define RX8025_CMD1     0xe0
#define RX8025_CMD2     0xf0

#define RX8025_24       0x20

#define RX8025_PON      0x10

class RX8025 {
public:
    RX8025();

    void begin(int sda, int sck);

    bool needInit(void);

    time_t readRTC(void);
    void writeRTC(time_t t);

private:
    int readReg(int addr);
    void writeReg(int addr, int value);
    byte fromClockFormat(int inClock);
    byte toClockFormat(int inValue);
};

#endif // RX8025_H

