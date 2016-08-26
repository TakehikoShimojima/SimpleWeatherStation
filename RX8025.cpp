#include <arduino.h>
#include <TimeLib.h>
#include "RX8025.h"

RX8025::RX8025() {
}

void RX8025::begin(int sda, int sck) {
    Wire.begin(sda, sck);

    writeReg(RX8025_CMD1, RX8025_24);
    writeReg(RX8025_CMD2, 0x00);
}

bool RX8025::needInit() {
    uint8_t r = readReg(RX8025_CMD2);
    return (r & RX8025_PON);
}

time_t RX8025::readRTC() {
    int yy, mm, dd, HH, MM, SS;

    Wire.beginTransmission(RX8025_ADDR);
    Wire.write(0x00);
    Wire.endTransmission(false);

    Wire.requestFrom(RX8025_ADDR, 7);

    SS = fromClockFormat(Wire.read());
    MM = fromClockFormat(Wire.read());
    HH = fromClockFormat(Wire.read());
    Wire.read();  //  dummy read
    dd = fromClockFormat(Wire.read());
    mm = fromClockFormat(Wire.read());
    yy = fromClockFormat(Wire.read()) + 2000;

    setTime(HH, MM, SS, dd, mm, yy);
    return (now());
}

void RX8025::writeRTC(time_t t) {
    int yy, mm, dd, HH, MM, SS;

    setTime(t);

    yy = year() % 100;
    mm = month();
    dd = day();
    HH = hour();
    MM = minute();
    SS = second();

    Wire.beginTransmission(RX8025_ADDR);
    Wire.write(0x00);  //  address=0
    Wire.write(toClockFormat(SS));
    Wire.write(toClockFormat(MM));
    Wire.write(toClockFormat(HH));
    Wire.write(0x00);  //  weekdays
    Wire.write(toClockFormat(dd));
    Wire.write(toClockFormat(mm));
    Wire.write(toClockFormat(yy));
    Wire.endTransmission();
    delay(1);    
}

int RX8025::readReg(int addr) {
    Wire.beginTransmission(RX8025_ADDR);
    Wire.write(addr);
    Wire.endTransmission(false);

    Wire.requestFrom(RX8025_ADDR, 1);
    while (Wire.available() == 0) ;

    uint8_t r = Wire.read();

    return r;
}

void RX8025::writeReg(int addr, int value) {
    Wire.beginTransmission(RX8025_ADDR);
    Wire.write(addr);   // レジスタ指定（下位4bitは0）
    Wire.write(value);  // 値セット
    Wire.endTransmission();
    delay(1);
}

byte RX8025::fromClockFormat(int inClock) {
  return ((inClock & 0xf0) >> 4) * 10 + (inClock & 0x0f);
}

byte RX8025::toClockFormat(int inValue) {
  return abs(inValue / 10) * 16 + (inValue % 10);
}

