#include <LittleFS.h>                   //this needs to be first, or it all crashes and burns...

#if defined(ESP8266)
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#else
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#endif
//needed for library
#include <ESPAsyncWiFiManager.h>         //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define NUMBER_OF_SUBCRIBINGTOPICS 5

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_username[40];
char mqtt_pass[40] = "";
char mqtt_port[40] = "";
char mqtt_ssl[70] = "";
char *ptrmqtt_ssl = mqtt_ssl;
uint8_t mqtt_sslarray[70];
uint8_t *ptrmqtt_sslarray = mqtt_sslarray;
char mqtt_host[40] = "";
char mqtt_SubscribeTopics[NUMBER_OF_SUBCRIBINGTOPICS][150] =
{ "/webserverswitch",
  "/status/info",
};
char devicename[40] = "";
char softwareVersion[] = "0.2";

//flag for saving data
bool shouldSaveConfig = true;
bool flagStatusMessageSend = false;
bool flagWebServerActive = false;
const unsigned long statusMessageInterval = 1000 * 30 * 1; // 30s;
unsigned long statusMessageTimer = 0;
char mqttWillTopic[200];
char mqttStatusInfoTopic[200];
char *mqttStatusInfoMessage;
char *mqttSubcribeTopic;
char mqttWebServerSwitchTopic[200];
const char* PARAM_DEVICENAME = "devicename";
const char* PARAM_MQTTHOST = "mqtt_host";
const char* PARAM_MQTTUSERNAME = "mqtt_username";
const char* PARAM_MQTTPASS = "mqtt_pass";
const char* PARAM_MQTTPORT = "mqtt_port";
const char* PARAM_MQTTSSL = "mqtt_ssl";





//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

AsyncWebServer webServer(80);
DNSServer dns;


void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}
void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();

}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  for (int i = 0; i < NUMBER_OF_SUBCRIBINGTOPICS; i++)
  {

    mqttSubcribeTopic = (char*)calloc(1, 200 * sizeof(char));
    sprintf(mqttSubcribeTopic, "%s%s", devicename, mqtt_SubscribeTopics[i]);
    uint16_t packetIdSub = mqttClient.subscribe(mqttSubcribeTopic, 2);
    Serial.println("Subscribing ");
    Serial.print(mqttSubcribeTopic);
    Serial.print("QoS 2, packetId: ");
    Serial.println(packetIdSub);
    free(mqttSubcribeTopic);
  }
  if (mqttClient.connected()) {
    uint16_t packetIdPub = mqttClient.publish(mqttWillTopic, 1, true, "ON");

    Serial.print("Publishing at QoS 1, packetId: ");
    Serial.println(packetIdPub);
    statusMessageTimer = millis();

    publishStatusInfoMessage();
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.printf("Disconnected from MQTT: %u\n", static_cast<std::underlying_type<AsyncMqttClientDisconnectReason>::type>(reason));


  //  Serial.println("Disconnected from MQTT.");

  if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
    Serial.println("Bad server fingerprint.");
  }

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}


void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);

  if (strcmp(topic, mqttStatusInfoTopic) == 0) {

    flagStatusMessageSend = true;

  } else {
  }


  if (strcmp(topic, mqttWebServerSwitchTopic) == 0) {

    if (strcmp(payload, "ON") == 0) {
      startWebServer();
    } else if (strcmp(payload, "OFF") == 0) {
      stopWebServer();
    }
    else {
    }

  } else {
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);


        StaticJsonDocument<1024> json;


        DeserializationError error = deserializeJson(json, buf.get());
        Serial.println(buf.get());
        // Test if parsing succeeds.
        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());

        }


        else {
          Serial.println("\nparsed json");

          strcpy(devicename, json["devicename"]);
          strcpy(mqtt_host, json["mqtt_host"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_ssl, json["mqtt_ssl"]);


          wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
          wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

          sprintf(mqttStatusInfoTopic, "%s/status/info", devicename);
          sprintf(mqttWebServerSwitchTopic, "%s/webserverswitch", devicename);
          sprintf(mqttWillTopic, "%s/status", devicename);
          mqttClient.onConnect(onMqttConnect);
          mqttClient.onDisconnect(onMqttDisconnect);
          mqttClient.onSubscribe(onMqttSubscribe);
          mqttClient.onUnsubscribe(onMqttUnsubscribe);
          mqttClient.onMessage(onMqttMessage);
          mqttClient.onPublish(onMqttPublish);
          mqttClient.setWill(mqttWillTopic, 1, true, "OFF");
          mqttClient.setServer(mqtt_host, String(mqtt_port).toInt());
          mqttClient.setCredentials(mqtt_username, mqtt_pass);


#if ASYNC_TCP_SSL_ENABLED
          while (*ptrmqtt_ssl) {
            *ptrmqtt_sslarray = strtol (ptrmqtt_ssl, &ptrmqtt_ssl, 16);
            ptrmqtt_sslarray++;
            ptrmqtt_ssl++;
          }
          Serial.println("secure");
          mqttClient.setSecure(true);
          mqttClient.addServerFingerprint(mqtt_sslarray);
#endif

          Serial.println("********");
          Serial.println(mqtt_host);

        }

      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  AsyncWiFiManagerParameter custom_devicename("devicename", "devicename", devicename, 40);
  AsyncWiFiManagerParameter custom_mqtt_host("host", "mqtt host ", mqtt_host, 40);
  AsyncWiFiManagerParameter custom_mqtt_username("username", "mqtt username ", mqtt_username, 40);
  AsyncWiFiManagerParameter custom_mqtt_pass("pass", "mqtt password", mqtt_pass, 40);
  AsyncWiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 40);
  AsyncWiFiManagerParameter custom_mqtt_ssl("ssl", "mqtt fingerprints", mqtt_ssl, 70);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFi.persistent(true);
  AsyncWiFiManager wifiManager(&webServer, &dns);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_devicename);
  wifiManager.addParameter(&custom_mqtt_host);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_ssl);

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality();

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(devicename, custom_devicename.getValue());
  strcpy(mqtt_host, custom_mqtt_host.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_ssl, custom_mqtt_ssl.getValue());



  // strcpy(blynk_token, custom_blynk_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    StaticJsonDocument<200> json;
    json["devicename"] = devicename;
    json["mqtt_host"] = mqtt_host;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_pass"] = mqtt_pass;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_ssl"] = mqtt_ssl;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Hello, world");
  });

  webServer.on("/config.json", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/config.json");
    response->addHeader("Server", "ESP Async Web Server");
    request->send(response);
  });

  webServer.on("/config", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/config.html");
  });
  
  webServer.on("/config", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam(PARAM_DEVICENAME, true)) {
      String devicename = request->getParam(PARAM_DEVICENAME, true)->value();
      String mqtt_host = request->getParam(PARAM_MQTTHOST, true)->value();
      String mqtt_username = request->getParam(PARAM_MQTTUSERNAME, true)->value();
      String mqtt_pass = request->getParam(PARAM_MQTTPASS, true)->value();
      String mqtt_port = request->getParam(PARAM_MQTTPORT, true)->value();
      String mqtt_ssl = request->getParam(PARAM_MQTTSSL, true)->value();
      StaticJsonDocument<200> json;
      json["devicename"] = devicename;
      json["mqtt_host"] = mqtt_host;
      json["mqtt_username"] = mqtt_username;
      json["mqtt_pass"] = mqtt_pass;
      json["mqtt_port"] = mqtt_port;
      json["mqtt_ssl"] = mqtt_ssl;

      File configFile = LittleFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
        request->send(400, "text/plain", "Error. Can't open file.");
      } else {

        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.close();
        request->send(200, "text/plain", "Success, saving config.");
      }
    } else {
      Serial.println("Wrong parameters");
      request->send(400, "text/plain", "Error. Wrong parameters.");
    }

  });

  webServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/style.css");
  });

  webServer.on("/devicename", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(devicename));
  });

  webServer.on("/deleteconfig", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWiFiManager wifiManager(&webServer, &dns);
    if (LittleFS.remove("/config.json") ) {
      wifiManager.resetSettings();
      request->send(200, "text/plain", "config deleted");
    } else {
      request->send(200, "text/plain", "config not deleted");
    }
  });
  webServer.onNotFound(notFound);

  if (!MDNS.begin(devicename)) {
    Serial.println("Error starting mDNS");
    return;
  } else {
    Serial.println("mDNS responder started");
    MDNS.addService(devicename, "http", "tcp", 80);
    MDNS.update();

  }
}

void publishStatusInfoMessage() {
  mqttStatusInfoMessage = (char*)calloc(1, 200 * sizeof(char));
  snprintf(mqttStatusInfoMessage, 150, "{\"ip\":\"%s\",\"wifistrength\":%i,\"uptime\":%ld,\"ESPCore\":\"%s\",\"version\":\"%s\",\"webserver\":\"%s\"}", WiFi.localIP().toString().c_str(), WiFi.RSSI(), millis(), ESP.getCoreVersion().c_str(), softwareVersion, flagWebServerActive ? "true" : "false");



  uint16_t packetIdPub = mqttClient.publish(mqttStatusInfoTopic, 2, false, mqttStatusInfoMessage);

  Serial.print("Publishing at QoS 2, packetId: ");
  Serial.println(packetIdPub);
  statusMessageTimer = millis();



  free(mqttStatusInfoMessage);
}
void loop() {
  // put your main code here, to run repeatedly:
  if (flagWebServerActive) {
    MDNS.update();
  }
  if ( (millis() - statusMessageTimer) >= statusMessageInterval && mqttClient.connected())
  {
    publishStatusInfoMessage();
  }

  if (flagStatusMessageSend && !flagWebServerActive) {
    startDeepSleep();
  }

}

void startDeepSleep() {
  flagStatusMessageSend = false;
  Serial.println("Going to deep sleep...");
  ESP.deepSleep(30 * 1000000);
  yield();
}

bool startWebServer() {


  webServer.begin();
  flagWebServerActive = true;
  return flagWebServerActive;

}

bool stopWebServer() {


  webServer.end();
  flagWebServerActive = false;
  return flagWebServerActive;

}
