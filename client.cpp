// When Automatic control starts, initially turn off all other valves
//When controlling manually, you can't turn on more then one valve, clicking to turn one valve will turn all others off

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

#include <FS.h>  

// Pin definitions for the shift register
#define LATCH_PIN 12
#define CLOCK_PIN 13
#define DATA_PIN 14
#define OE_PIN 5

// Wi-Fi credentials (list of available networks)
struct WiFiConfig {
  const char* ssid;
  const char* password;
};

// List of Wi-Fi networks to connect to
WiFiConfig wifiList[] = {
  {"ssid1", "password1"},
  {"ssid2", "password1"},
  {"ssid3", "password1"},
  {"ssid4", "password1"}
};

const int numNetworks = sizeof(wifiList) / sizeof(wifiList[0]);

// Static IP configuration
IPAddress local_IP(172, 20, 10, 10);  // Set your desired static IP
IPAddress gateway(172, 20, 10, 1);     // Typically your router's IP
IPAddress subnet(255, 255, 255, 240);  // Standard subnet mask
IPAddress dns1(8, 8, 8, 8);            // Google DNS
IPAddress dns2(8, 8, 4, 4);            // Google DNS (secondary)


// Create a WebSocket server object
WebSocketsServer webSocket = WebSocketsServer(81);  // WebSocket on port 81
ESP8266WebServer server(80);  // HTTP server on port 80

#define NUM_VALVES 12  // Number of valves
bool valveStates[NUM_VALVES] = {true, false, false, false, false, false, false, false, false, false, false, false};  // Valve states: false = OFF, true = ON

uint16_t Data = 0; // Represents the data to shift out to the shift register

// Hardware relay order mapping
int valveRelayOrder[] = {0, 1, 2, 3, 4, 5, 15, 14, 13, 12, 11, 10}; // Hardware relay order

bool webSocketConnected = false;  // Track WebSocket connection status

bool automaticControlActive = false;
unsigned long int timeDurationMillis;
unsigned long int previousMillis;
int currentIndexValveOrder = 0;

void setup() {
  // Start serial communication
  Serial.begin(115200);

    // Mount LittleFS
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

  // Set shift register pins as output
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  digitalWrite(OE_PIN, LOW); // Output Enable

  // Connect to Wi-Fi
  WiFi.config(local_IP, gateway, subnet, dns1, dns2);

  Serial.println("Scanning for available Wi-Fi networks...");
  connectToWiFi();

  // Start the WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  controlValveState(0, true);
  valveStates[0] = true;

  for (int i=1; i<12; i++){
        controlValveState(i, false);
        valveStates[i] = false;
  }
  
  // Set up the HTTP server route
  server.on("/", HTTP_GET, handleRoot);

  // Start the HTTP server
  server.begin();
}

void loop() {

  // Handle client requests (if Wi-Fi is connected)
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.loop();
    server.handleClient();
  }

  connectToWiFi();

  handleAutomaticControl();

}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.printf("Client %u connected\n", num);
      webSocketConnected = true;

      // Send initial states as JSON to the client
      String initialStates = "{\"type\":\"init\",\"valves\":[";
      for (int i = 0; i < NUM_VALVES; i++) {
        initialStates += valveStates[i] ? "true" : "false";
        if (i < NUM_VALVES - 1) initialStates += ",";
      }
      initialStates += "]}";
      webSocket.sendTXT(num, initialStates);
      break;
    }

    case WStype_DISCONNECTED:
      Serial.printf("Client %u disconnected\n", num);
      webSocketConnected = false;
      break;

    case WStype_TEXT: {
      // Parse JSON payload
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, payload, length);

      if (error) {
        Serial.println("Invalid JSON received");
        return;
      }

      // Handle the command
      const char* type = doc["type"];
      if (strcmp(type, "toggle") == 0) {
        int valveIndex = doc["valve"];

      controlValveState(valveIndex, true);
      valveStates[valveIndex] = true;
  

    for (int i=0; i<12; i++){
      if(i != valveIndex){
        controlValveState(i, false);
        valveStates[i] = false;
      }

      if(WiFi.status() == WL_CONNECTED){
          String status = valveStates[i] ? "true" : "false";
          String message = "{\"type\":\"update\",\"valve\":" + String(i) + ",\"status\":" + status + "}";
          webSocket.broadcastTXT(message);
      }
    }
      } else if (strcmp(type, "startAuto") == 0){
          automaticControlActive = true;
          float durationTime = doc["duration"];
          timeDurationMillis = durationTime * 60 * 1000; //convert minutes to milliseconds 
          
      } else if (strcmp(type, "stopAuto") == 0){
          automaticControlActive = false;
          Serial.println("Stoped automatic control!");
      }
      break;
    }
  }
}

// Handle HTTP GET request to serve the HTML page

void handleRoot() {
    File file = LittleFS.open("/server_ui.html", "r");
    if (!file) {
        server.send(500, "text/plain", "Failed to open file");
        return;
    }
    
    String html = file.readString(); // Read file contents
    file.close();
    
    server.send(200, "text/html", html); // Send HTML response
}

// Function to control the valve states through the shift register
void controlValveState(int valveIndex, bool state) { 

  uint16_t mask = 1 << valveRelayOrder[valveIndex];

  if (state) {
    Data |= mask;  // Set the corresponding bit to 1
  } else {
    Data &= ~mask;  // Set the corresponding bit to 0
  }

  // Shift out data to the shift register
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (Data >> 8));
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, Data);
  digitalWrite(LATCH_PIN, HIGH);
}

void handleAutomaticControl() {

  if(!automaticControlActive){
    return;
  }

  unsigned long currentMillis = millis();

  if(currentMillis - previousMillis >= timeDurationMillis){
    previousMillis = currentMillis;
    controlValveState(currentIndexValveOrder, true);
    valveStates[currentIndexValveOrder] = true;
  

    for (int i=0; i<12; i++){
      if(i != currentIndexValveOrder){
        controlValveState(i, false);
        valveStates[i] = false;
      }

      if(WiFi.status() == WL_CONNECTED){
          String status = valveStates[i] ? "true" : "false";
          String message = "{\"type\":\"update\",\"valve\":" + String(i) + ",\"status\":" + status + "}";
          webSocket.broadcastTXT(message);
      }
    }

    if(currentIndexValveOrder == 11){
      currentIndexValveOrder = 0;
    }else{
      currentIndexValveOrder++; 
    }
  }
}

// Function to scan for Wi-Fi and connect to available networks
// Function to scan for Wi-Fi and connect to available networks
void connectToWiFi() {
  
  static int wifiindexnr = 0;
  static unsigned long lastScanTime = 0;

  if (WiFi.status() == WL_CONNECTED) {
    return;  // Already connected
  }

  // Only scan every 5 seconds
  if (millis() - lastScanTime < 5000) {
    return;
  }
  lastScanTime = millis();


  WiFi.begin(wifiList[wifiindexnr].ssid, wifiList[wifiindexnr].password);
  if(wifiindexnr == 3){
    wifiindexnr = 0;
  }else{
    wifiindexnr++;
  }
}