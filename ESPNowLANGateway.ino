#include <esp_now.h>
#include <ETH.h>
#include <WiFiClientSecure.h>
#include "PubSubClient.h"

#define CHANNEL 1

// keep in sync with slave struct
struct __attribute__((packed)) SENSOR_DATA {
  uint8_t  version;
  uint32_t bootcount;
  uint16_t hg;
  float    temp;
  uint16_t battRaw;
  uint16_t battVolt;
  uint8_t  battCharge;
} sensorData;

#define ETH_POWER_PIN   -1
#define ETH_ADDR        1

//TODO: Configuration
const char* host = "espNow32mqtt1";

//TODO: Configuration
#define MQTT_SERVER      "..."
#define MQTT_PORT        8883
#define MQTT_CLIENTID    "..."
#define MQTT_USERNAME    "..."
#define MQTT_KEY         "..."

//TODO: Configuration
#define MQTT_ROOT_TOPIC        "octopus/espnow"

#define KEEPALIVE 5000
#define MQTT_KEEPALIVE 10000

WiFiClientSecure mqttclient;
PubSubClient mqtt(mqttclient);

// Init ESP Now with fallback
void InitESPNow() {
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  }
  else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
}

static bool eth_connected = false;
bool     espnow_received = false;
char     espnow_macStr[18];
uint8_t  espnow_dataLen;
uint8_t  *espnow_data;

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname(host);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void MQTT_connect() {
  // Loop until we're reconnected
  int retry = 0;
  while (!mqtt.connected()) {
    retry++;
    Serial.print(F("Attempting MQTT connection..."));

    // Create a random client ID
    String clientId = MQTT_CLIENTID;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_KEY)) {
      Serial.println(F("connected"));
      // Once connected, publish an announcement...
      mqtt.publish(MQTT_ROOT_TOPIC, MQTT_CLIENTID);
      mqtt.subscribe(String(MQTT_ROOT_TOPIC + String(F("/read"))).c_str());
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(mqtt.state());
      if (retry > 10) {
        Serial.println("Failed connect to MQTT, reboot");
        ESP.restart();
      }

      Serial.println(F(" try again in 5 seconds"));

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// config AP SSID
void configDeviceAP() {
  WiFi.mode(WIFI_AP);

  const char *SSID = "ESPNow Gateway";
  bool result = WiFi.softAP(SSID, "donttellmeyourpassword", CHANNEL, 0);
  if (!result) {
    Serial.println("AP Config failed.");
  } else {
    Serial.println("AP Config Success. Broadcasting with AP: " + String(SSID));
    Serial.print("AP MAC: "); Serial.println(WiFi.softAPmacAddress());
  }
}

void setup() {
  Serial.begin(115200);

  Serial.print(F("LAN connect... "));

  WiFi.onEvent(WiFiEvent);
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLOCK_GPIO17_OUT);

  int retry = 0;
  while (!eth_connected) {
    Serial.println(F("Waiting"));
    delay(1000);
    retry++;
    if (retry > 10) {
      break;
    }
  }

  if (!eth_connected) {
    Serial.println("Connect LAN failed, rebooting");
    ESP.restart();
  }

  Serial.println(F("Connected"));

  configDeviceAP();

  InitESPNow();

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println(F("Ready"));
  Serial.print(F("IP address: "));
  Serial.println(ETH.localIP());

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("Message arrived ["));
  Serial.print(topic);
  Serial.print(F("] "));
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (espnow_received) {
    Serial.println("Not processed yet, skipping message");
    return;
  }

  espnow_received = true;
  snprintf(espnow_macStr, sizeof(espnow_macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  espnow_dataLen = data_len;
  espnow_data = (uint8_t*)malloc(data_len*sizeof(uint8_t));
  memcpy(espnow_data, data, data_len);
}

long previousMQTTKeepAliveMillis = 0;
long mqtt_keepalive_counter = 0;
void handleMQTTKeepAlive() {
  if (millis() - previousMQTTKeepAliveMillis >= MQTT_KEEPALIVE) {
    previousMQTTKeepAliveMillis = millis();

    mqtt_keepalive_counter++;

    if (!mqtt.connected()) {
      return;
    }

    mqtt.publish(String(MQTT_ROOT_TOPIC + String("/keepalive")).c_str(), String(mqtt_keepalive_counter).c_str());
    Serial.println("MQTT Keep Alive");
  }
}


long previousKeepAliveMillis = 0;
void handleKeepAlive() {
  if (millis() - previousKeepAliveMillis >= KEEPALIVE) {
    previousKeepAliveMillis = millis();
    Serial.println("Keep Alive");
  }
}

void processEspNowData() {
  Serial.print("Last Packet Recv from: "); Serial.println(espnow_macStr);
  Serial.print("Last Packet Recv Len: "); Serial.println(espnow_dataLen);

  Serial.print("Last Packet Recv Data: ");

  for (int i = 0; i < espnow_dataLen; i++) {
    Serial.print(espnow_data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  if (sizeof(sensorData) == espnow_dataLen) {
    Serial.println("Got known data");
    memcpy(&sensorData, espnow_data, sizeof(sensorData));
    Serial.println("Sensor data");
    Serial.print("Uptime: "); Serial.println(sensorData.bootcount);
    Serial.print("Temperature: "); Serial.println(sensorData.temp);
    Serial.print("pressure: "); Serial.println(sensorData.hg);
    Serial.print("Battery level: ");
    Serial.print(sensorData.battCharge);
    Serial.print("% (");
    Serial.print(sensorData.battVolt);
    Serial.print("V) (");
    Serial.print(sensorData.battRaw);
    Serial.println(" RAW)");
  }

    if (mqtt.connected()) {
      mqtt.publish(String(MQTT_ROOT_TOPIC + String("/")+espnow_macStr).c_str(), espnow_data, espnow_dataLen);
    }
}

void loop() {

  if (espnow_received)
  {
    Serial.println("Received ESP Now data");

    if (mqtt.connected()) {
      mqtt.publish(String(MQTT_ROOT_TOPIC + String("/receive")).c_str(), "Received ESPNow message");
    }

    processEspNowData();

    free(espnow_data);
    espnow_received = false;
  }

  if (eth_connected) {
    if (!mqtt.connected()) {
      MQTT_connect();
    }

    mqtt.loop();

    handleMQTTKeepAlive();

  }

  handleKeepAlive();
}
