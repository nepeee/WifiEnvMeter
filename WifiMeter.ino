#define WIFI_SSID "asd"
#define WIFI_PASSWORD "asdbsd"
#define SLEEP_PERIOD 600
#define TIMEOUT_PERIOD 10
#define POST_URL "http://1.2.3.4/asd/bsd"

extern "C" {
  #include <user_interface.h>
  #include <lwip/err.h>
  #include <lwip/dns.h>
}

#include <ESP8266HTTPClient.h> //Edited line 869 & 728 to allow to connect with ip
#include <Ticker.h>
#include <Wire.h>
#include "SparkFunBME280.h"

#define MAX44009_ADDR 0x4A
#define SENSOR_WAKE 14
#define BME280_START_DELAY 3
#define MIN_VOLTAGE 790 //239*3.3
#define IP_VALID 0x9A
#define IP_INVALID 0x00
#define readRtcRam() ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData))
#define writeRtcRam() ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData))

struct {
  uint16_t wakeTime;
  uint8_t ipValid;
  ip_info ipInfo;
  ip_addr_t dns0;
  ip_addr_t dns1;
} rtcData;

float readMax44009() {
  uint8_t data0, data1;

  Wire.begin();
  
  Wire.beginTransmission(MAX44009_ADDR);
  Wire.write(0x03);
  if (Wire.endTransmission(0)!=0)
    return -1;
  Wire.requestFrom(MAX44009_ADDR, 2);
  if (Wire.available()!=2)
    return -1;
    
  data0 = Wire.read();
  data1 = Wire.read();

  int exponent = (data0 & 0xF0) >> 4;
  int mantissa = ((data0 & 0x0F) << 4) | (data1 & 0x0F);
  return pow(2, exponent) * mantissa * 0.045;
}

void timeout() {
  Serial.println("Timeout");
  rtcData.ipValid = IP_INVALID;
  writeRtcRam();

  ESP.deepSleep(SLEEP_PERIOD * 1000000);
}

BME280 bme280;
uint16_t vbat;

void setup() {
  Ticker ticker;
  ticker.attach(TIMEOUT_PERIOD, timeout);
  
  if (ESP.getResetInfoPtr()->reason==REASON_DEEP_SLEEP_AWAKE) {
    vbat = analogRead(A0);// 1/1024v * 4 - 3,90625v/LSB
    if (vbat<MIN_VOLTAGE) {
      vbat = analogRead(A0);
      if (vbat<MIN_VOLTAGE)
        ESP.deepSleep(0);
    }

    readRtcRam();
  }
  else {
    vbat = 0;
    rtcData.wakeTime = 0;
    rtcData.ipValid = IP_INVALID;
  }

  Serial.begin(115200);
  Serial.println();
  Serial.println("Start");

  pinMode(SENSOR_WAKE, OUTPUT);
  digitalWrite(SENSOR_WAKE, HIGH);
  delay(BME280_START_DELAY);
  bme280.begin();

  wifi_set_sleep_type(NONE_SLEEP_T);
  if (rtcData.ipValid==IP_VALID) {
    wifi_set_channel(6);
    
    wifi_station_dhcpc_stop();
    wifi_set_ip_info(STATION_IF, &rtcData.ipInfo);
    dns_setserver(0, &rtcData.dns0);
    dns_setserver(1, &rtcData.dns1);
  }
  else {
    char ssid[32] = WIFI_SSID;
    char password[64] = WIFI_PASSWORD;
    struct station_config stationConf;

    stationConf.bssid_set = 0;
    os_memcpy(&stationConf.ssid, ssid, 32);
    os_memcpy(&stationConf.password, password, 64);
    
    wifi_set_opmode(STATION_MODE);
    wifi_station_set_auto_connect(0);
    wifi_station_set_config(&stationConf);
  }
  wifi_station_connect();
  Serial.println("Wifi connecting");

  while ((wifi_station_get_connect_status()!=STATION_GOT_IP))// || ((bme280.readRegister(BME280_STAT_REG) & 0x09)!=0)
    yield();

  //Serial.println(millis());

  String request = "id="+String(ESP.getChipId())
    +"&vbat="+vbat
    +"&wakeTime="+rtcData.wakeTime
    +"&humidity="+bme280.readHumidity()
    +"&temp="+bme280.readTemperature()
    +"&pressure="+bme280.readPressure()
    +"&illumination="+readMax44009();

  digitalWrite(SENSOR_WAKE, LOW);

  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Host", "xyz.com");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST(request);

  wifi_get_ip_info(STATION_IF, &rtcData.ipInfo);
  rtcData.dns0 = dns_getserver(0);
  rtcData.dns1 = dns_getserver(1);
  rtcData.ipValid = IP_VALID;
  rtcData.wakeTime = millis();
  writeRtcRam();
    
  Serial.println(rtcData.wakeTime);

  ESP.deepSleep(SLEEP_PERIOD * 1000000);
}

void loop() {}
