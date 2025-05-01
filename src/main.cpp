/*

*/
#include <Arduino.h>
#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Button2.h>
#include <ezLED.h>
#include <TickTwo.h>

//******************************** Configulation ****************************//
#define _DEBUG_  // Comment this line if you don't want to debug

//******************************** Variables & Objects **********************//
#define deviceName "MyESP32"

bool mqttParameter;
//----------------- esLED ---------------------//
#define ledPin LED_BUILTIN
ezLED statusLed(ledPin);

//----------------- Reset WiFi Button ---------//
#define resetWifiBtPin 0
Button2 resetWifiBt;

//----------------- WiFi Manager --------------//
char mqttBroker[16] = "192.168.0.10";
char mqttPort[6]    = "1883";
char mqttUser[10];
char mqttPass[10];

WiFiManager wifiManager;

WiFiManagerParameter customMqttBroker("broker", "mqtt server", mqttBroker, 16);
WiFiManagerParameter customMqttPort("port", "mqtt port", mqttPort, 6);
WiFiManagerParameter customMqttUser("user", "mqtt user", mqttUser, 10);
WiFiManagerParameter customMqttPass("pass", "mqtt pass", mqttPass, 10);

//----------------- MQTT ----------------------//
WiFiClient   espClient;
PubSubClient mqtt(espClient);

//******************************** Tasks ************************************//
// void    mqttStateDetector();
// TickTwo tMqttStateDetector(mqttStateDetector, 3000, 0, MILLIS);

void    connectMqtt();
void    reconnectMqtt();
TickTwo tConnectMqtt(connectMqtt, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tReconnectMqtt(reconnectMqtt, 3000, 0, MILLIS);

//******************************** Functions ********************************//
//----------------- SPIFFS --------------------//
void loadConfigration() {
// clean FS, for testing
// SPIFFS.format();

// read configuration from FS json
#ifdef _DEBUG_
    Serial.println(F("mounting FS..."));
#endif

    if (SPIFFS.begin()) {
#ifdef _DEBUG_
        Serial.println(F("mounted file system"));
#endif
        if (SPIFFS.exists("/config.json")) {
// file exists, reading and loading
#ifdef _DEBUG_
            Serial.println(F("reading config file"));
#endif
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
#ifdef _DEBUG_
                Serial.println(F("opened config file"));
#endif
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                JsonDocument json;
                auto         deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
#ifdef _DEBUG_
                    Serial.println(F("\nparsed json"));
#endif
                    strcpy(mqttBroker, json["mqttBroker"]);
                    strcpy(mqttPort, json["mqttPort"]);
                    strcpy(mqttUser, json["mqttUser"]);
                    strcpy(mqttPass, json["mqttPass"]);
                    mqttParameter = json["mqttParameter"];
                } else {
#ifdef _DEBUG_
                    Serial.println(F("failed to load json config"));
#endif
                }
            }
        }
    } else {
#ifdef _DEBUG_
        Serial.println(F("failed to mount FS"));
#endif
    }
}

void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (String(topic) == "test/subscribe/topic") {
        if (message == "aValue") {
            // Do something
        } else if (message == "otherValue") {
            // Do something
        }
    }
}

void mqttInit() {
#ifdef _DEBUG_
    Serial.print(F("MQTT parameters are "));
#endif
    if (mqttParameter) {
#ifdef _DEBUG_
        Serial.println(F(" available"));
#endif
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        mqtt.setCallback(handleMqttMessage);
        tConnectMqtt.start();
    } else {
#ifdef _DEBUG_
        Serial.println(F(" not available."));
#endif
    }
}

void saveConfigCallback() {
    // read updated parameters
    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(mqttUser, customMqttUser.getValue());
    strcpy(mqttPass, customMqttPass.getValue());
#ifdef _DEBUG_
    Serial.println(F("The values in the file are: "));
    Serial.print(F("\tmqtt_broker : "));
    Serial.println(mqttBroker);
    Serial.print(F("\tmqtt_port : "));
    Serial.println(mqttPort);
    Serial.print(F("\tmqtt_user : "));
    Serial.println(mqttUser);
    Serial.print(F("\tmqtt_pass : "));
    Serial.println(mqttPass);
#endif

// save the custom parameters to FS
#ifdef _DEBUG_
    Serial.println(F("saving config"));
#endif
    JsonDocument json;
    json["mqttBroker"] = mqttBroker;
    json["mqttPort"]   = mqttPort;
    json["mqttUser"]   = mqttUser;
    json["mqttPass"]   = mqttPass;

    if (json["mqttBroker"] != "") {
        json["mqttParameter"] = true;
        mqttParameter         = json["mqttParameter"];
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
#ifdef _DEBUG_
        Serial.println(F("failed to open config file for writing"));
#endif
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);

    configFile.close();
    // end save
#ifdef _DEBUG_
    Serial.println(F("\nlocal ip"));
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
    Serial.println(WiFi.dnsIP());
#endif

    mqttInit();
}

//----------------- Wifi Manager --------------//
void wifiManagerSetup() {
#ifdef _DEBUG_
    Serial.println(F("Loading configuration..."));
#endif
    loadConfigration();

    // add all your parameters here
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
    // wifiManager.setConfigPortalTimeout(60);  // auto close configportal after 30 seconds
    wifiManager.setConfigPortalBlocking(false);
#ifdef _DEBUG_
    Serial.println(F("Saving configuration..."));
#endif
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    if (wifiManager.autoConnect(deviceName, "password")) {
#ifdef _DEBUG_
        Serial.println(F("WiFi is connected :D"));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("Configportal running"));
#endif
    }
}

void subscribeMqtt() {
#ifdef _DEBUG_
    Serial.println(F("Subscribing to the MQTT topics..."));
#endif
    mqtt.subscribe("test/subscribe/topic");
}

void publishMqtt() {
#ifdef _DEBUG_
    Serial.println(F("Publishing to the MQTT topics..."));
#endif
    mqtt.publish("test/publish/topic", "Hello World!");
}

//----------------- Connect MQTT --------------//
void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
#ifdef _DEBUG_
        Serial.println(F("Connecting MQTT... "));
#endif
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
            Serial.printf("tReconnectMqtt, counter: %d\n", tReconnectMqtt.counter());
#ifdef _DEBUG_
            Serial.println(F("Connected"));
#endif
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 200ms ON, 200ms OFF, repeat 3 times, blink immediately
            subscribeMqtt();
            publishMqtt();
        } else {
#ifdef _DEBUG_
            Serial.print(F("failed state: "));
            Serial.println(mqtt.state());
            Serial.print(F("counter: "));
            Serial.println(tReconnectMqtt.counter());
#endif
            if (tReconnectMqtt.counter() >= 3) {
                tReconnectMqtt.stop();
                tConnectMqtt.interval(60 * 1000);  // Wait 1 minute before reconnecting.
                tConnectMqtt.start();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) {
#ifdef _DEBUG_
            Serial.println(F("WiFi is not connected"));
#endif
        }
    }
}

void connectMqtt() {
    if (!mqtt.connected()) {
        Serial.printf("tConnectMqtt, counter: %d\n", tConnectMqtt.counter());
        tConnectMqtt.stop();
        tReconnectMqtt.start();
    } else {
        mqtt.loop();
    }
}

//----------------- Reset WiFi Button ---------//
void resetWifiBtPressed(Button2& btn) {
    statusLed.turnON();
#ifdef _DEBUG_
    Serial.println(F("Deleting the config file and resetting WiFi."));
#endif
    SPIFFS.format();
    wifiManager.resetSettings();
#ifdef _DEBUG_
    Serial.print(deviceName);
    Serial.println(F(" is restarting."));
#endif
    ESP.restart();
    delay(3000);
}

//********************************  Setup ***********************************//
void setup() {
    Serial.begin(115200);
    statusLed.turnOFF();
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);

    wifiManagerSetup();
    mqttInit();
}

//********************************  Loop ************************************//
void loop() {
    statusLed.loop();
    resetWifiBt.loop();
    wifiManager.process();
    tConnectMqtt.update();
    tReconnectMqtt.update();
}
