#include <SPI.h>
#include <LoRa.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ==============================
// CONFIGURATION
// ==============================
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TX_CHARACTERISTIC_UUID "cc821ea3-9b9f-4eb8-8884-25b57d00f77b" // ADD THIS
BLECharacteristic* pTxCharacteristic = NULL; // ADD THIS

// LoRa Pin Definitions
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  26

// State Variables
BLEServer* pServer = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

String pendingTxPayload = "";
bool triggerTransmission = false;
unsigned int txPacketCounter = 0;

// Delay control to prevent RF flooding
unsigned long lastTxTime = 0;
const long txCooldown = 1500; // Minimum 1.5 seconds between transmissions

// ==============================
// BLE CALLBACKS
// ==============================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("\n[BLE] App connected!");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("\n[BLE] App disconnected!");
    }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      // Using ESP32 v3.x String format
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        pendingTxPayload = rxValue;
        triggerTransmission = true;

        // ADD THIS BLOCK TO ACKNOWLEDGE THE PHONE!
        if (pTxCharacteristic != NULL) {
            String ackMsg = "{\"status\":\"received\"}";
            pTxCharacteristic->setValue(ackMsg.c_str());
            pTxCharacteristic->notify(); 
            Serial.println("[BLE] Sent ACK to App");
        }
        // ----------------------------------------
      }
    }
};

// ==============================
// SETUP
// ==============================
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- ResQMesh TRANSMITTER Node Booting ---");

  // Initialize LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  Serial.println("[LoRa] Initializing at 433 MHz...");
  if (!LoRa.begin(433E6)) {
    Serial.println("[LoRa] CRITICAL ERROR: Hardware not found!");
    // Continue running to allow BLE testing even if LoRa is disconnected
  } else {
    LoRa.setSyncWord(0xF3);
    LoRa.setTxPower(17); // Set transmit power (2-20dBm)
    Serial.println("[LoRa] Initialization Successful.");
  }

  // Initialize BLE
  Serial.println("[BLE] Starting Server...");
  BLEDevice::init("ResQMesh_Relay");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
                                       
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  // ADD THIS BLOCK right before pService->start();
  pTxCharacteristic = pService->createCharacteristic(
                        TX_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());
  // ---------------------------------------------
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Ready. Waiting for Kotlin App...");
}

// ==============================
// MAIN LOOP
// ==============================
void loop() {
  // 1. Handle BLE Re-Advertising
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); 
      pServer->startAdvertising();
      Serial.println("[BLE] Advertising restarted.");
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // 2. Handle Serial Monitor Input (Fallback Testing)
  if (Serial.available() > 0) {
    String simulatedAppInput = Serial.readString();
    simulatedAppInput.trim();
    if (simulatedAppInput.length() > 0) {
      pendingTxPayload = simulatedAppInput;
      triggerTransmission = true;
    }
  }

  // 3. Execute Transmission Safely
  if (triggerTransmission) {
    if (millis() - lastTxTime >= txCooldown) {
      transmitLoRa(pendingTxPayload);
      lastTxTime = millis();
    } else {
      Serial.println("[TX] WARNING: Transmission delayed (Cooldown active).");
    }
    // Clear flags
    triggerTransmission = false;
    pendingTxPayload = "";
  }
}

// ==============================
// HELPER FUNCTIONS
// ==============================
void transmitLoRa(String payload) {
  txPacketCounter++;
  Serial.println("\n-----------------------------------");
  Serial.printf("[TX] Sending Packet #%u\n", txPacketCounter);
  Serial.println("[TX] Payload: " + payload);
  
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  
  Serial.println("[TX] Status: Transmission Complete");
  Serial.println("-----------------------------------");
}