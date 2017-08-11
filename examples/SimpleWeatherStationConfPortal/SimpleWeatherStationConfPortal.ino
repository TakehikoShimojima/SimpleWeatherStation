/*
 * BME280とNJL7502Lを使った温度・湿度・気圧+照度ロガー
 * NJL7502LをADコンバーターMCP3002で読む。
 * RTCモジュールを使わず、deepSleep直前の時刻とdeepSleep時間から
 * deepSleep後の時刻を計算する
 * 
 * 立ち上がったらRTCメモリーからRTCメモリーからdeepSleep直前の時刻と
 * deepSleep時間を読み、現在時刻を計算してシステムに設定(setTime)
 * BME280の値を読む
 * restartの値により処理を分ける
 * restart==0  wi-fi onで起動。valid==trueならデーターを送信。
 * 
 * ++restartし、バッファーに時刻とBME280の値を書く
 * 
 * RTCメモリーは電源を切ると消えるので、channelIdとwriteKeyは失われる。
 * RTCメモリーがvalidでなければConfigPortalを立ち上げ、外からssid、パスワードを設定。
 * 
 */
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <TimeLib.h>

#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>

#include "BME280_SPI.h"
#include "MCP3002.h"
#include "Ambient.h"

extern "C" {
#include "user_interface.h"
#include "sntp.h"
}

uint32_t getSntpTime(void);
uint32_t calcCRC32(const uint8_t *data, size_t length);
void sendDataToAmbient(bool valid);

#define _DEBUG 1
#if _DEBUG
#define DBG(...) { Serial.print(__VA_ARGS__); }
#define DBGLED(...) { digitalWrite(__VA_ARGS__); }
#else
#define DBG(...)
#define DBGLED(...)
#endif /* _DBG */

#define MCP_CS 5
#define BME_CS 15

#define MaxRestart 6
#define PERIOD 300
#define COMP 0.978

#pragma pack(push)
#pragma pack(4)
struct OneData_t {
    time_t created;
    double temp;
    double humid;
    double pressure;
    double illumi;
};

#define MaxDataArea  (512 - sizeof(uint32_t) - sizeof(int) - sizeof(time_t))
#define MaxBlocks    (MaxDataArea / sizeof(OneData_t))

struct Data {
    uint32_t crc32;
    int    restart;
    time_t tbefore;
    time_t sleeptime;
    int    channelId;
    char   writeKey[24];
    struct OneData_t blocks[MaxBlocks];
};

union {
    struct Data data;
    byte buffer[512];
} rtcData;

#pragma pack(pop)

WiFiClient client;
Ambient ambient;

MCP3002 mcp3002;
BME280 bme280;

bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
//    DBG("Should save config\r\n");
    shouldSaveConfig = true;
}

void setup()
{
    WiFiManager wifiManager;
    bool valid;  //  RTCメモリーの値が有効かどうかのフラグ
    int restart;  //  何回目の起動かのカウンター。0の時はWi-Fiオンで起動されている
    time_t currenttime;
    double sleeptime;
    time_t sntptime;

    int t = millis();

    // RTCメモリーからデーターを読む
    ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData));

    uint32_t crcOfData = calcCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    if ((crcOfData == rtcData.data.crc32) && 0 <= rtcData.data.restart && rtcData.data.restart < MaxRestart) {
        restart = rtcData.data.restart;
        valid = true;
        sleeptime = (double)rtcData.data.sleeptime * COMP;
        currenttime = rtcData.data.tbefore + (time_t)sleeptime;
    } else {
        restart = 0;
        valid = false;
        sleeptime = 0;
        currenttime = 0;
    }
    setTime(currenttime);
#ifdef _DEBUG
    Serial.begin(115200);
    delay(10);
//    pinMode(LED, OUTPUT);
#endif
    DBG("\r\nStart\r\n");
    if (valid) {
        DBG("sleeptime: ");DBG(rtcData.data.sleeptime);
        DBG(", tbefore: ");DBG(rtcData.data.tbefore);DBG("\r\n");
    }
    mcp3002.begin(MCP_CS);
    bme280.begin(BME_CS);    //  Wi-Fi接続には時間がかかり、測定周期がぶれるので、
                             //  Wi-Fi接続より先にセンサーを読んでしまう

    double temp, humid, pressure, illumi;

    temp = bme280.readTemperature();
    humid = bme280.readHumidity();
    pressure = bme280.readPressure();

    mcp3002.readData(0);                   // リセット後、1回目のreadは値が0になってしまうので読み捨てる
    illumi = (double)(mcp3002.readData(0));
    illumi = illumi * 3.3 / 1024;          // 負荷抵抗両端の電圧
    illumi = illumi / 1000 * 1000000 * 2;  // 負荷抵抗=1kΩ  l→Lux

    int indx = restart - 1;
    if (indx < 0) {
        indx = MaxRestart - 1;
    }
    rtcData.data.blocks[indx].created = now();
    rtcData.data.blocks[indx].temp = temp;
    rtcData.data.blocks[indx].humid = humid;
    rtcData.data.blocks[indx].pressure = pressure;
    rtcData.data.blocks[indx].illumi = illumi;

    DBG("currentTime: "); DBG(currenttime);
    DBG(", temp: "); DBG(temp);
    DBG(" DegC,  humid: "); DBG(humid);
    DBG(" %, pressure: "); DBG(pressure);
    DBG(" hPa, illumi: "); DBG(illumi);
    DBG(" Lux\r\n");
    delay(200);

    DBG("valid: ");DBG(valid);
    DBG(", restart: ");DBG(restart);DBG("\r\n");
    delay(200);

    if (restart == 0) { // restartが0の時だけWi-Fiがオンになっている
        wifi_set_sleep_type(LIGHT_SLEEP_T);

        // id/name placeholder/prompt default length
        WiFiManagerParameter custom_ambient_channelId("channelId", "channel Id", "", 16);
        WiFiManagerParameter custom_ambient_writeKey("writeKey", "writeKey", "", 24);

        wifiManager.setDebugOutput(false);
        wifiManager.setSaveConfigCallback(saveConfigCallback);
        wifiManager.addParameter(&custom_ambient_channelId);
        wifiManager.addParameter(&custom_ambient_writeKey);

//        wifiManager.resetSettings();
        if (valid) {
            wifiManager.autoConnect("Ambient setup");
        } else {
            wifiManager.startConfigPortal("Ambient setup");
        }

        //read updated parameters
        char channelId[16], writeKey[24];
        strcpy(channelId, custom_ambient_channelId.getValue());
        strcpy(writeKey, custom_ambient_writeKey.getValue());

        if (shouldSaveConfig) {
            rtcData.data.channelId = atoi(channelId);
            strcpy(rtcData.data.writeKey, writeKey);
        }
        DBG("channelId: ");DBG(rtcData.data.channelId);DBG("\r\n");
        DBG("writeKey: ");DBG(rtcData.data.writeKey);DBG("\r\n");

        DBG("WiFi connected\r\nIP address: ");
        DBG(WiFi.localIP()); DBG("\r\n");

        time_t sntptime = getSntpTime();  //  SNTPで時刻取得
        DBG("SNTP time: ");DBG(sntptime);DBG("\r\n");
        setTime(sntptime);
        if (!valid) {
            rtcData.data.blocks[indx].created = now();
        }
    }

//    if (restart == 0 && valid) {
    if (restart == 0) {               // 30分待たずに動作確認するため、validがfalseでもAmbientに送る
        sendDataToAmbient(valid);
    }

    if (++restart >= MaxRestart) {
        restart = 0;
    }

    rtcData.data.restart = restart;
    rtcData.data.tbefore = now();

    t = millis() - t; // t: リスタート直後からの経過時間(ミリ秒)
    t = (t < PERIOD * 1000) ? (PERIOD * 1000 - t) : 1;  // sleeptime(ミリ秒)

    rtcData.data.sleeptime = (time_t)((double)t / 1000.0 / COMP);
    rtcData.data.crc32 = calcCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    // RTCメモリーにデーターを書く
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

    ESP.deepSleep((int)(t * 1000.0 / COMP), (restart == 0) ? RF_DEFAULT : RF_DISABLED);

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
        delay(20);
        if (++cnt > 500) {
            break;
        }
    }
    return t;
}

#define BUFSIZE 500

char buffer[BUFSIZE];

void sendDataToAmbient(bool valid) {
    int i;
    time_t created;
    int vb;
    char tempbuf[12], humidbuf[12], pressurebuf[12], illumibuf[12], vbbuf[12];

    DBG("Ambient.send() to: ");DBG(rtcData.data.channelId);
    DBG(", writeKey: ");DBG(rtcData.data.writeKey);DBG("\r\n");

    ambient.begin(rtcData.data.channelId, rtcData.data.writeKey, &client);

    sprintf(buffer, "{\"writeKey\":\"%s\",\"data\":[", rtcData.data.writeKey);
    if (valid) { // もしRTCデーターがvalidなら全部のデーターを送信
        i = 0;
    } else { // そうでないなら[MaxRestart - 1]のスロットだけ送信
        i = MaxRestart - 1;
    }
    for (; i < MaxRestart; i++) {

        created = rtcData.data.blocks[i].created;
        dtostrf(rtcData.data.blocks[i].temp, 3, 1, tempbuf);
        dtostrf(rtcData.data.blocks[i].humid, 3, 1, humidbuf);
        dtostrf(rtcData.data.blocks[i].pressure, 3, 1, pressurebuf);
        dtostrf(rtcData.data.blocks[i].illumi, 3, 1, illumibuf);

        sprintf(&buffer[strlen(buffer)], "{\"created\":%d,\"time\":1,\"d1\":%s,\"d2\":%s,\"d3\":%s,\"d5\":%s},", created, tempbuf, humidbuf, pressurebuf, illumibuf);
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

