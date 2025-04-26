/*
0.0 - Base on ESP32_WiFiManager_MQTT_SPIFFS
    - Chage Serial.print to DebugMode
    - You can use wifi without MQTT Breker by removing the MQTT Broker field on web UI.
*/
#include <Arduino.h>
#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <TickTwo.h>
#include <ezLED.h>
#include <Button2.h>

//******************************** Configulation ****************************//
#define DebugMode  // Uncomment this line if you want to debug

#ifdef DebugMode
#define de(x)   Serial.print(x)
#define deln(x) Serial.println(x)
#else
#define de(x)
#define deln(x)
#endif

//******************************** Variables & Objects **********************//
#define deviceName "MyESP32"

bool storedValues;
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
    deln("mounting FS...");

    if (SPIFFS.begin()) {
        deln("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            // file exists, reading and loading
            deln("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                deln("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                JsonDocument json;
                auto         deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
                    deln("\nparsed json");
                    strcpy(mqttBroker, json["mqttBroker"]);
                    strcpy(mqttPort, json["mqttPort"]);
                    strcpy(mqttUser, json["mqttUser"]);
                    strcpy(mqttPass, json["mqttPass"]);
                    storedValues = json["storedValues"];
                } else {
                    deln("failed to load json config");
                }
            }
        }
    } else {
        deln("failed to mount FS");
    }
}

void saveConfigCallback() {
    // read updated parameters
    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(mqttUser, customMqttUser.getValue());
    strcpy(mqttPass, customMqttPass.getValue());
    // deln("The values in the file are: ");
    // deln("\tmqtt_broker : " + String(mqttBroker));
    // deln("\tmqtt_port : " + String(mqttPort));
    // deln("\tmqtt_user : " + String(mqttUser));
    // deln("\tmqtt_pass : " + String(mqttPass));

    // save the custom parameters to FS
    deln("saving config");
    JsonDocument json;
    json["mqttBroker"] = mqttBroker;
    json["mqttPort"]   = mqttPort;
    json["mqttUser"]   = mqttUser;
    json["mqttPass"]   = mqttPass;

    if (json["mqttBroker"] != "") {
        json["storedValues"] = true;
        storedValues         = json["storedValues"];
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        deln("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);

    configFile.close();
    // end save

    deln("\nlocal ip");
    deln(WiFi.localIP());
    deln(WiFi.gatewayIP());
    deln(WiFi.subnetMask());
    deln(WiFi.dnsIP());

    if (storedValues) {
        deln("Setting MQTT Broker: " + String(mqttBroker));
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    }

    // deln("set MQTT Broker: " + String(mqttBroker));
    // mqtt.setServer(mqttBroker, atoi(mqttPort));
    // tConnectMqtt.start();
}

//----------------- Wifi Manager --------------//
void wifiManagerSetup() {
    deln("Loading configuration...");
    loadConfigration();

    // add all your parameters here
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
    // wifiManager.setConfigPortalTimeout(60);  // auto close configportal after 30 seconds
    wifiManager.setConfigPortalBlocking(false);

    deln("Saving configuration...");
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    if (wifiManager.autoConnect(deviceName, "password")) {
        deln("connected...yeey :D");
    } else {
        deln("Configportal running");
    }
}

//----------------- Connect MQTT --------------//
void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
        deln("Connecting MQTT... ");
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
            deln("connected");
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 200ms ON, 200ms OFF, repeat 3 times, blink immediately
        } else {
            deln("failed state: " + String(mqtt.state()));
            deln("counter: " + String(tReconnectMqtt.counter()));
            if (tReconnectMqtt.counter() >= 3) {
                // ESP.restart();
                tReconnectMqtt.stop();
                // tConnectMqtt.interval(3600 * 1000);
                tConnectMqtt.interval(60 * 1000);  // Wait 1 minute before reconnecting.
                tConnectMqtt.resume();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) deln("WiFi is not connected");
    }
}

void connectMqtt() {
    if (!mqtt.connected()) {
        tConnectMqtt.pause();
        tReconnectMqtt.start();
    } else {
        mqtt.loop();
    }
}

//----------------- Reset WiFi Button ---------//
void resetWifiBtPressed(Button2& btn) {
    statusLed.turnON();
    deln("Deleting the config file and resetting WiFi.");
    SPIFFS.format();
    wifiManager.resetSettings();
    deln(String(deviceName) + " is restarting.");
    ESP.restart();
}

//********************************  Setup ***********************************//
void setup() {
    statusLed.turnOFF();
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);
    Serial.begin(115200);

    wifiManagerSetup();

    if (storedValues) {
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    }
}

//********************************  Loop ************************************//
void loop() {
    statusLed.loop();
    resetWifiBt.loop();
    wifiManager.process();
    tConnectMqtt.update();
    tReconnectMqtt.update();
}
