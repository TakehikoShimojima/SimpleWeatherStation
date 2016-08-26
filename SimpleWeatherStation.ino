/*
 * BME280を使った温度・湿度・気圧ロガー
 * 
 * 立ち上がったら時刻とBME280の値を読む
 * rtcメモリーからデーターを読み、有効か確認(CRCを確認)
 * restartの値により処理を分ける
 * restart==0  wi-fi onで起動。valid==trueならデーターを送信。
 * ++restartし、バッファーに時刻とBME280の値を書く
 */
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <TimeLib.h>
#include "RX8025.h"
#include "BME280.h"
#include "Ambient.h"

extern "C" {
#include "user_interface.h"
#include "sntp.h"
}

uint32_t getSntpTime(void);
uint32_t calcCRC32(const uint8_t *data, size_t length);
void sendDataToAmbient();

#define _DEBUG 1
#if _DEBUG
#define DBG(...) { Serial.print(__VA_ARGS__); }
#else
#define DBG(...)
#endif /* _DBG */

#define SDA 14
#define SCL 13

#define MaxRestart 6
#define PERIOD 300

#pragma pack(push)
#pragma pack(4)
struct OneData_t {
    time_t created;
    double temp;
    double humid;
    double pressure;
};

#define MaxDataArea  (512 - sizeof(uint32_t) - sizeof(int) - sizeof(time_t))
#define MaxBlocks    (MaxDataArea / sizeof(OneData_t))

struct Data {
    uint32_t crc32;
    int    restart;
    time_t nextAdjust;
    struct OneData_t blocks[MaxBlocks];
};

union {
    struct Data data;
    byte buffer[512];
} rtcData;

#pragma pack(pop)

const char* ssid = "...ssid...";
const char* password = "...password...";
WiFiClient client;

unsigned int channelId = 100;
const char* writeKey = "...writeKey...";
Ambient ambient;

RX8025 rx8025;
BME280 bme280;

void setup()
{
    bool valid;  //  RTCメモリーの値が有効かどうかのフラグ
    int restart;  //  何回目の起動かのカウンター。0の時はWi-Fiオンで起動されている

    int t = millis();

#ifdef _DEBUG
    Serial.begin(115200);
    delay(10);
#endif
    DBG("\r\nStart\r\n");

    Wire.begin(SDA, SCL);

    bme280.begin(SDA, SCL);  //  Wi-Fi接続には時間がかかり、測定周期がぶれるので、
                             //  Wi-Fi接続より先にセンサーを読んでしまう

    time_t currentTime;
    double temp, humid, pressure;

    //  電源オン直後(rx8025.needInit()がtrue)はRTCの値が不定なので読まない
    currentTime = rx8025.needInit() ? 0 : rx8025.readRTC();

    temp = bme280.readTemperature();
    humid = bme280.readHumidity();
    pressure = bme280.readPressure();

    DBG("currentTime: "); DBG(currentTime);
    DBG(", temp: "); DBG(temp);
    DBG(" DegC,  humid: "); DBG(humid);
    DBG(" %, pressure: "); DBG(pressure);
    DBG(" hPa\r\n");
    delay(100);

    // RTCメモリーからデーターを読む
    ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData));

    uint32_t crcOfData = calcCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    if ((crcOfData == rtcData.data.crc32) && 0 <= rtcData.data.restart && rtcData.data.restart < MaxRestart) {
        restart = rtcData.data.restart;
        valid = true;
    } else {
        restart = 0;
        rtcData.data.nextAdjust = 0;
        valid = false;
    }

    DBG("valid: ");DBG(valid);
    DBG(", restart: ");DBG(restart);DBG("\r\n");

    if (restart == 0) { // restartが0の時だけWi-Fiがオンになっている
        wifi_set_sleep_type(LIGHT_SLEEP_T);

        WiFi.begin(ssid, password);  //  Wi-Fi APに接続

        while (WiFi.status() != WL_CONNECTED) {  //  Wi-Fi AP接続待ち
            delay(0);
        }

        DBG("WiFi connected\r\nIP address: ");
        DBG(WiFi.localIP()); DBG("\r\n");

        //  RTCの初期化が必要か、次の時刻合わせの時刻なら
        if (rx8025.needInit() || (valid && (rtcData.data.nextAdjust < currentTime))) {
            time_t now = getSntpTime();  //  SNTPで時刻取得

            DBG("init RX8025: ");DBG(now);DBG("\r\n");
            rx8025.begin(SDA, SCL);
            rx8025.writeRTC(now);
            rtcData.data.nextAdjust = now + 24 * 3600;  //  次の時刻合わせを1日後に設定
        } else {
            DBG("Doesn't init RX8025\r\n");
        }
    }

    int indx = restart - 1;
    if (indx < 0) {
        indx = MaxRestart - 1;
    }

    rtcData.data.blocks[indx].created = currentTime;
    rtcData.data.blocks[indx].temp = temp;
    rtcData.data.blocks[indx].humid = humid;
    rtcData.data.blocks[indx].pressure = pressure;

    if (restart == 0 && valid) {
        sendDataToAmbient();
    }

    if (++restart >= MaxRestart) {
        restart = 0;
    }
    rtcData.data.restart = restart;
    rtcData.data.crc32 = calcCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    // RTCメモリーにデーターを書く
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

    t = millis() - t;
    t = (t < PERIOD * 1000) ? (PERIOD * 1000 - t) : 1;
    if (restart == 0) {
        ESP.deepSleep(t * 1000, RF_DEFAULT);
    } else {
        ESP.deepSleep(t * 1000, RF_DISABLED);
    }
    delay(1000);
}


void loop()
{
    while (true) {
        yield();
    }
}

uint32_t calcCRC32(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

uint32_t getSntpTime() {
    configTime(0 * 3600, 0, "ntp.nict.jp", NULL, NULL);

    uint32_t t = 0;
    int cnt = 0;
    while (t == 0) {
        t = sntp_get_current_timestamp();
        delay(10);
        if (++cnt > 100) {
            break;
        }
    }
    return t;
}

#define BUFSIZE 400

char buffer[BUFSIZE];

void sendDataToAmbient() {
    int i;
    time_t created;
    int vb;
    char tempbuf[12], humidbuf[12], pressurebuf[12], vbbuf[12];

    ambient.begin(channelId, writeKey, &client);

    sprintf(buffer, "{\"writeKey\":\"%s\",\"data\":[", writeKey);
    for (i = 0; i < MaxRestart; i++) {

        created = rtcData.data.blocks[i].created;
        dtostrf(rtcData.data.blocks[i].temp, 3, 1, tempbuf);
        dtostrf(rtcData.data.blocks[i].humid, 3, 1, humidbuf);
        dtostrf(rtcData.data.blocks[i].pressure, 3, 1, pressurebuf);

        if (created) {
            sprintf(&buffer[strlen(buffer)], "{\"created\":%d,\"time\":1,\"d1\":%s,\"d2\":%s,\"d3\":%s},", created, tempbuf, humidbuf, pressurebuf);
        }
    }

    vb = system_adc_read();
    dtostrf((float)vb / 1024.0f / 20.0f * 120.0f, 4, 2, vbbuf);

    buffer[strlen(buffer)-2] = '\0';
    sprintf(&buffer[strlen(buffer)], ",\"d4\":%s}", vbbuf);

    sprintf(&buffer[strlen(buffer)], "]}\r\n");

    DBG("buf: ");DBG(strlen(buffer));DBG(" bytes\r\n");

    int n = ambient.bulk_send(buffer);
    DBG("sent: ");DBG(n);DBG("\r\n");
    DBG(buffer);DBG("\r\n");
}

