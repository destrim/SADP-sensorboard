#include "WiFi.h"
#include "Preferences.h"
#include "BluetoothSerial.h"
#include "DHT.h"
#include "HTTPClient.h"

#define DHTPIN 27
#define DHTTYPE DHT11

String ssids[20];

const char* ssid = "";
const char* pass = "";
String clientSsid;
String clientPass;

const char* ntpServer = "pool.ntp.org";
const char* serverName = "https://jsonplaceholder.typicode.com/posts";

const char* deviceName = "sensorboard_01";

struct tm timeinfo;
const long  gmtOffset = 3600;
const int   daylightOffset = 3600;

const long wifiTimeout = 10000;

unsigned long lastTime = 0;
unsigned long timerDelay = 4500;

typedef enum { 
  NONE,
  BT_CONNECTED,
  NETWORK_SCANNED,
  WAIT_FOR_SSID,
  GOT_SSID,
  WAIT_FOR_PASS,
  GOT_PASS,
  WAIT_CONNECT,
  CONNECTED,
  FAILED
} ConnectionStates;

ConnectionStates connectionState = NONE;

bool connEstablished = false;
bool btDisconnect = false;

BluetoothSerial SerialBT;
Preferences preferences;
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  preferences.begin("wifi_access", false);

  if (!init_wifi()) {
    SerialBT.register_callback(callback);
    SerialBT.begin(deviceName);
    establish_connection();
  }

  configTime(gmtOffset, daylightOffset, ntpServer);
  dht.begin();
}

bool init_wifi() {
  String tmpSsid = preferences.getString("ssid");
  String tmpPass = preferences.getString("pass");
  ssid = tmpSsid.c_str();
  pass = tmpPass.c_str();

  Serial.println(ssid);
  Serial.println(pass);

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

  long startWifi = millis();
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startWifi > wifiTimeout) {
      WiFi.disconnect(true, true);
      return false;
    }
  }
  
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  
  return true;
}

void scan_wifi_networks() {
  WiFi.mode(WIFI_STA);
  int n =  WiFi.scanNetworks();
  if (n == 0) {
    SerialBT.println("no networks found");
  } else {
    SerialBT.println(String(n) + " networks found");
    delay(1000);
    for (int i = 0; i < n; ++i) {
      ssids[i + 1] = WiFi.SSID(i);
//      Serial.print(i + 1);
//      Serial.print(": ");
//      Serial.println(ssids[i + 1]);
      SerialBT.println(String(i + 1) + ": " + WiFi.SSID(i) + " (Strength:" + WiFi.RSSI(i) + ")");
      Serial.println(String(i + 1) + ": " + WiFi.SSID(i) + " (Strength:" + WiFi.RSSI(i) + ")");
    }
    connectionState = NETWORK_SCANNED;
  }
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    connectionState = BT_CONNECTED;
  }

  if (event == ESP_SPP_DATA_IND_EVT && connectionState == WAIT_FOR_SSID) {
    int i = SerialBT.readString().toInt();
    clientSsid = ssids[i];
    connectionState = GOT_SSID;
  }

  if (event == ESP_SPP_DATA_IND_EVT && connectionState == WAIT_FOR_PASS) {
    clientPass = SerialBT.readString();
    clientPass.trim();
    connectionState = GOT_PASS;
  }
}

void disconnect_bluetooth() {
  delay(1000);
  Serial.println("BT stopping");
  SerialBT.println("Bluetooth disconnecting...");
  delay(1000);
  SerialBT.flush();
  SerialBT.disconnect();
  SerialBT.end();
  Serial.println("BT stopped");
  delay(1000);
  btDisconnect = false;
}

void establish_connection() {
  while (!connEstablished) {
    if (btDisconnect) {
      disconnect_bluetooth();
      connEstablished = true;
    }
  
    switch (connectionState)
    {
      case BT_CONNECTED:
        SerialBT.println("Scanning Wi-Fi networks");
        Serial.println("Scanning Wi-Fi networks");
        scan_wifi_networks();
        connectionState = NETWORK_SCANNED;
        break;

      case NETWORK_SCANNED:
        SerialBT.println("Please enter the number for your Wi-Fi");
        Serial.println("Please enter the number for your Wi-Fi");
        connectionState = WAIT_FOR_SSID;
        break;
  
      case GOT_SSID:
        SerialBT.println("Please enter your Wi-Fi password");
        Serial.println("Please enter your Wi-Fi password");
        connectionState = WAIT_FOR_PASS;
        break;
  
      case GOT_PASS:
        SerialBT.println("Please wait for Wi-Fi connection...");
        Serial.println("Please wait for Wi_Fi connection...");
        connectionState = WAIT_CONNECT;
        preferences.putString("ssid", clientSsid);
        preferences.putString("pass", clientPass);
        if (init_wifi()) {
          btDisconnect = true;
          connectionState = CONNECTED;
        } else {
          connectionState = FAILED;
        }
        break;
  
      case FAILED:
        SerialBT.println("Wi-Fi connection failed");
        Serial.println("Wi-Fi connection failed");
        delay(2000);
        connectionState = BT_CONNECTED;
        break;
    }
  }
}

void loop() {
  if ((millis() - lastTime) > timerDelay) {
    if(WiFi.status()== WL_CONNECTED){
      WiFiClient client;
      HTTPClient http;

      float t = dht.readTemperature();
      float h = dht.readHumidity();
      
      if (isnan(h) || isnan(t)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
      } else {
        http.begin(client, serverName);
        http.addHeader("Content-Type", "application/json");

        getLocalTime(&timeinfo);
        
        char date_array[20];
        strftime(date_array, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);
        String date = String(date_array);
        
        String request = "{\"name\":\""
                         + String(deviceName)
                         + "\",\"timestamp\":\""
                         + date
                         + "\",\"temp\":\""
                         + String(t)
                         + "\",\"hum\":\""
                         + String(h)
                         + "\"}";
        
        Serial.println(request);
        int httpResponseCode = http.POST(request);
       
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        
        http.end();
      }
    }
    else {
      Serial.println("WiFi Disconnected");
    }
    lastTime = millis();
  }
}
