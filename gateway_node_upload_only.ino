#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ==============================
// CONFIGURATION
// ==============================
const char* ssid     = "realme GT 2";
const char* password = "josh1234";


// LoRa Pin Definitions (ESP32)
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  26

// State Variables
unsigned long previousMillis = 0;
const long interval = 10000;
unsigned int rxPacketCounter = 0;

// ==============================
// SETUP
// ==============================
void setup() {
  Serial.begin(115200);
  while (!Serial); 
  Serial.println("\n--- ResQMesh GATEWAY Node Booting ---");

  WiFi.mode(WIFI_STA);
  connectToWiFi();

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  Serial.println("[LoRa] Initializing at 433 MHz...");
  if (!LoRa.begin(433E6)) {
    Serial.println("[LoRa] CRITICAL ERROR: Hardware not found!");
    while (1); 
  }else {
    // ADD THIS LINE TO BOTH ESP32s! (0xF3 is just a random hex ID for your network)
    LoRa.setSyncWord(0xF3);
  Serial.println("[LoRa] Initialization Successful. Listening...");
  }
}

// ==============================
// MAIN LOOP
// ==============================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Maintain WiFi Connection (Non-blocking)
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval)) {
    Serial.println("[WiFi] Connection lost. Reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
    previousMillis = currentMillis;
  }

  // 2. Listen for LoRa Packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    rxPacketCounter++;
    String receivedPayload = "";
    
    while (LoRa.available()) {
      receivedPayload += (char)LoRa.read();
    }
    
    Serial.println("\n-----------------------------------");
    Serial.printf("[LoRa] Packet #%u Received\n", rxPacketCounter);
    Serial.printf("[LoRa] RSSI: %d dBm | Size: %d bytes\n", LoRa.packetRssi(), packetSize);
    Serial.println("[LoRa] Payload: " + receivedPayload);

    // Basic malformed packet check (ensure it starts with JSON bracket)
    if (receivedPayload.startsWith("{")) {
      if (WiFi.status() == WL_CONNECTED) {
        sendToBackend(receivedPayload);
      } else {
        Serial.println("[System] ERROR: Cannot upload. WiFi disconnected.");
      }
    } else {
      Serial.println("[System] WARNING: Malformed or non-JSON packet dropped.");
    }
    Serial.println("-----------------------------------");
  }
}

// ==============================
// HELPER FUNCTIONS
// ==============================
void connectToWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);

  // We rely entirely on the router/hotspot to provide the correct DNS now
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] Assigned IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Failed to connect.");
  }
}

void sendToBackend(String payload) {
  Serial.println("[HTTP] Preparing RAW HTTP POST request...");

  // CRITICAL: We changed this from WiFiClientSecure to standard WiFiClient!
  WiFiClient client; 
  
  const char* host = "switchyard.proxy.rlwy.net";
  const int port = 50954; // Railway's custom proxy port

  Serial.printf("[HTTP] Connecting to %s:%d...\n", host, port);

  // 1. Connect using standard unencrypted TCP
  if (!client.connect(host, port)) {
    Serial.println("[HTTP] CRITICAL ERROR: TCP Connection Failed!");
    return;
  }

  Serial.println("[HTTP] Connection Successful! Sending data...");

  // 2. Build the HTTP POST request (Notice we include the port in the Host header)
  String httpRequest = String("POST /api/alerts/ingress HTTP/1.1\r\n") +
                       "Host: " + String(host) + ":" + String(port) + "\r\n" +
                       "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n" +
                       "Content-Type: application/json\r\n" +
                       "Content-Length: " + String(payload.length()) + "\r\n" +
                       "Connection: close\r\n\r\n" +
                       payload;

  // 3. Blast it to the server
  client.print(httpRequest);

  // 4. Wait for the server to reply
  long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 10000) {
      Serial.println("[HTTP] ERROR: Server took too long to respond.");
      client.stop();
      return;
    }
    delay(10);
  }

  // 5. Read the very first line of the server's response (e.g., "HTTP/1.1 200 OK")
  if (client.available()) {
    String responseLine = client.readStringUntil('\n');
    Serial.print("[HTTP] Server Replied: ");
    Serial.println(responseLine);
  }

  client.stop();
  Serial.println("[HTTP] Connection closed safely.");
}