#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h> // Add PubSubClient library
#include <credentials.h>
#include "AsyncPing.h"
#include <NTPClient.h>

// Global variables
LiquidCrystal_I2C lcd(0x27, 20, 4); // Change the address (0x27) according to your display
IPAddress ip(192, 168, 0, 9);
const long interval = 10000;
unsigned long previousMillis = 0;
AsyncPing Pings[3];

// LED pin for ping loss notification
const int NOTIF_LED = D3;

// MQTT
const char *mqtt_server = "192.168.0.90";
const int mqtt_port = 1883;
const char *mqtt_topic = "/lcd-debug";
WiFiClient espClient;
PubSubClient client(espClient);

// NTP Configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 5 * 3600 + 30 * 60;  // GMT offset for Kolkata (IST)
const int daylightOffset_sec = 0;  // No daylight saving time offset

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, daylightOffset_sec);

void setupWiFi()
{
  Serial.begin(115200);
  Serial.println("Booting");
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    lcd.setCursor(0, 0);
    lcd.print("Connection Failed!");
    delay(5000);
    ESP.restart();
  }
  lcd.setCursor(0, 1);
  lcd.print("Connected");
  lcd.setCursor(0, 2);
  lcd.print(WiFi.localIP());
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupOTA() {
  ArduinoOTA.onStart([]() { Serial.println("Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  lcd.setCursor(0, 1);
  lcd.print("Ready");
  lcd.setCursor(0, 2);
  lcd.print(WiFi.localIP());
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(3000);
}
void setupMQTT()
{
  client.setServer(mqtt_server, mqtt_port);
  // Add any additional setup for MQTT (e.g., authentication)
}

void reconnectMQTT()
{
  while (!client.connected())
  {
    Serial.println("Attempting MQTT connection...");
    if (client.connect("ESP8266Client"))
    {
      Serial.println("Connected to MQTT broker");
    }
    else
    {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 seconds...");
      delay(5000);
    }
  }
}

void publishPingStatus(const char *ip, bool pingStatus, int avg_time_ms)
{
  if (client.connected())
  {
    String status = pingStatus ? "Success" : "Failure";
    String message = "IP: " + String(ip) + ", Status: " + status + ", Average Time: " + String(avg_time_ms) + " ms";
    client.publish(mqtt_topic, message.c_str());
  }
}

void controlLCDBacklight()
{
  int currentHour = timeClient.getHours();
  String message = "Hour : " + String(currentHour);
  client.publish(mqtt_topic, message.c_str());
  if (currentHour >= 21 || currentHour < 6)
  {
    lcd.noBacklight(); // Turn off the LCD backlight between 9 PM and 6 AM
  }
  else
  {
    lcd.backlight(); // Turn on the LCD backlight during the day
  }
}

void ping()
{
  const char *ips[] = {"8.8.8.8", "1.1.1.1"};

  for (int i = 0; i < 2; i++)
  {
    IPAddress addr;
    if (!WiFi.hostByName(ips[i], addr))
      addr.fromString(ips[i]);

    Pings[i].on(true, [i, addr](const AsyncPingResponse &response)
                {
                  if (response.answer)
                  {
                    Serial.printf("Ping to %s: Success, Average Time: %d ms\n", addr.toString().c_str(), response.time);
                    digitalWrite(NOTIF_LED, LOW); // Turn off the LED for success
                  }
                  else
                  {
                    Serial.printf("Ping to %s: Failure\n", addr.toString().c_str());
                    digitalWrite(NOTIF_LED, HIGH); // Turn on the LED for failure
                  }

                  publishPingStatus(addr.toString().c_str(), response.answer, response.time);
                  return true; // stop after response
                });

    Serial.printf("Started ping to %s:\n", addr.toString().c_str());
    Pings[i].begin(addr);
  }
}

void setup()
{
  pinMode(NOTIF_LED, OUTPUT);
  setupWiFi();
  setupOTA();
  setupMQTT();
  timeClient.begin();
}

void loop()
{
  ArduinoOTA.handle();
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    timeClient.update();
    controlLCDBacklight();
    ping();
    if (!client.connected())
    {
      reconnectMQTT();
    }
    client.loop();
  }
}
