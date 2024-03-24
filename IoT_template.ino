#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

#define Y_LED 21
#define G_LED 22
#define ECHO_PIN 39
#define TRIG_PIN 34

long duration;
int distance;
bool isPaused = false;  // Global variable to track pause state

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to Wi-Fi");
  int timeoutCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    timeoutCount++;
    if (timeoutCount > 20) {
      Serial.println("Failed to connect to Wi-Fi");
      ESP.restart();
      return;
    }
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void messageHandler(char* topic, byte* payload, unsigned int length) {
  String message;

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Received message: ");
  Serial.println(message);
  Serial.print("From topic: ");
  Serial.println(String(topic));
  if (String(topic) == "tapeWinder/V2/testPlatform/control") {
    if (message == "pause") {
      isPaused = true;
      Serial.println("Pausing readings");
    } else if (message == "play") {
      isPaused = false;
      Serial.println("Resuming readings");
    }
  }
}

void connectAWS() {
  digitalWrite(Y_LED, HIGH);
  digitalWrite(G_LED, LOW);
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.setServer(AWS_IOT_ENDPOINT, 8883);

  Serial.print("Connecting to AWS IOT");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }

  Serial.println("AWS IoT Connected!");
  digitalWrite(Y_LED, LOW);
  digitalWrite(G_LED, HIGH);

  client.setCallback(messageHandler);
  client.subscribe("tapeWinder/V2/testPlatform/control");
}

String buildMessage(String m) {
  StaticJsonDocument<200> doc;
  doc["device"] = THINGNAME;
  doc["message"] = m;
  doc["timestamp"] = millis();

  String output;
  serializeJson(doc, output);

  return output;
}

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2;

  return distance;
}

void setup(void) {
  pinMode(Y_LED, OUTPUT);
  pinMode(G_LED, OUTPUT);

  digitalWrite(Y_LED, HIGH);
  digitalWrite(G_LED, LOW);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(115200);
  connectWiFi();
  connectAWS();

  Serial.println("Initialization complete!");
  digitalWrite(Y_LED, LOW);
  digitalWrite(G_LED, HIGH);
}

long prevMillis = 0;
int timeDelay = 250;  // Changed to 1000 for 1-second interval

void loop(void) {
  client.loop();  // Process MQTT messages

  if (!isPaused && millis() - prevMillis > timeDelay) {
    prevMillis = millis();
    float distance = readDistance();
    if (distance == 0 || distance > 2000) {
      Serial.println("ERROR: No pulse from sensor");
    } else {
      client.publish("tapeWinder/V2/testPlatform", buildMessage("{\"US1\":\"" + String(distance) + "\"}").c_str());
      Serial.print("Distance of ");
      Serial.print(distance);
      Serial.println(" cm was published to AWS");
    }
  }

  if (!client.connected()) {
    connectAWS();
  }
}
