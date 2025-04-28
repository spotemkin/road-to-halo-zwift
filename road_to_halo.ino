#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUIDs for services
#define SERVICE_UUID_POWER "1818"      // Cycling Power Service
#define SERVICE_UUID_CSC "1816"        // Cycling Speed and Cadence
#define SERVICE_UUID_HEART_RATE "180D" // Heart Rate

// UUIDs for characteristics
#define CHAR_UUID_POWER "2A63"      // Power Measurement
#define CHAR_UUID_CSC "2A5B"        // CSC Measurement
#define CHAR_UUID_HEART_RATE "2A37" // Heart Rate Measurement

// Global variables
BLEServer *pServer = nullptr;
BLECharacteristic *pPowerChar = nullptr;
BLECharacteristic *pCadenceChar = nullptr;
BLECharacteristic *pHeartRateChar = nullptr;

// Simulation variables
uint32_t lastUpdate = 0;
uint16_t power = 200; // Initial power in watts
bool powerIncreasing = true;
uint8_t cadence = 90; // Initial cadence in rpm
bool cadenceIncreasing = true;
uint8_t heartRate = 135; // Initial heart rate
bool heartIncreasing = true;

// CSC variables
uint16_t crankRevolutions = 0;   // Total number of crank revolutions
uint16_t lastCrankEventTime = 0; // Last event time in 1/1024 sec

// Connection flag
bool deviceConnected = false;

// Class for connection handling
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        deviceConnected = true;
        Serial.println("Device connected");
    }

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected = false;
        Serial.println("Device disconnected");
        // Restart advertising on disconnect
        pServer->getAdvertising()->start();
    }
};

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting BLE emulator for Zwift...");

    // Initialize BLE
    BLEDevice::init("Kickr Core 5638");

    // Create server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // 1. Cycling Power Service
    BLEService *pPowerService = pServer->createService(SERVICE_UUID_POWER);
    pPowerChar = pPowerService->createCharacteristic(
        CHAR_UUID_POWER,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY);
    pPowerChar->addDescriptor(new BLE2902());

    // 2. Cycling Speed and Cadence Service
    BLEService *pCSCService = pServer->createService(SERVICE_UUID_CSC);
    pCadenceChar = pCSCService->createCharacteristic(
        CHAR_UUID_CSC,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY);
    pCadenceChar->addDescriptor(new BLE2902());

    // 3. Heart Rate Service
    BLEService *pHeartService = pServer->createService(SERVICE_UUID_HEART_RATE);
    pHeartRateChar = pHeartService->createCharacteristic(
        CHAR_UUID_HEART_RATE,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY);
    pHeartRateChar->addDescriptor(new BLE2902());

    // Start services
    pPowerService->start();
    pCSCService->start();
    pHeartService->start();

    // Configure advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID_POWER);
    pAdvertising->addServiceUUID(SERVICE_UUID_CSC);
    pAdvertising->addServiceUUID(SERVICE_UUID_HEART_RATE);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // Helps with iPhone connections
    pAdvertising->setMaxPreferred(0x12);

    // Start advertising
    pAdvertising->start();

    Serial.println("BLE emulator started. Device 'Kickr Core 5638' ready for connection.");
    Serial.println("Emulator provides Power, Cadence and Heart Rate in one device.");
}

void loop()
{
    // Update values every second
    if (millis() - lastUpdate > 1000)
    {
        lastUpdate = millis();
        updateValues();

        if (deviceConnected)
        {
            sendNotifications();
        }
    }

    // Small delay
    delay(20);
}

void updateValues()
{
    // Power: gradual change 200-220 W
    if (powerIncreasing)
    {
        power++;
        if (power >= 220)
            powerIncreasing = false;
    }
    else
    {
        power--;
        if (power <= 200)
            powerIncreasing = true;
    }

    // Cadence: change 90-98 rpm
    if (cadenceIncreasing)
    {
        cadence++;
        if (cadence >= 98)
            cadenceIncreasing = false;
    }
    else
    {
        cadence--;
        if (cadence <= 90)
            cadenceIncreasing = true;
    }

    // Heart rate: change 135-155 bpm
    if (heartIncreasing)
    {
        heartRate++;
        if (heartRate >= 155)
            heartIncreasing = false;
    }
    else
    {
        heartRate--;
        if (heartRate <= 135)
            heartIncreasing = true;
    }

    // Update crank revolutions
    crankRevolutions++;
    // Rotation time in 1/1024 sec (roughly corresponds to cadence)
    uint16_t timeDiff = 60 * 1024 / cadence;
    lastCrankEventTime = (lastCrankEventTime + timeDiff) % 65536;
}

void sendNotifications()
{
    // 1. Power characteristic - Cycling Power Measurement
    uint8_t powerData[4];
    // Flags: 0x00 = Only instantaneous power
    powerData[0] = 0x00; // Flags (LSO)
    powerData[1] = 0x00; // Flags (MSO)
    // Instantaneous power (Little Endian)
    powerData[2] = power & 0xFF;        // LSB
    powerData[3] = (power >> 8) & 0xFF; // MSB

    pPowerChar->setValue(powerData, sizeof(powerData));
    pPowerChar->notify();

    // 2. Cadence characteristic - CSC Measurement
    uint8_t cadenceData[7];
    // Flags: 0x02 = Only crank revolution data
    cadenceData[0] = 0x02; // Flags
    // Cumulative Crank Revolutions (Little Endian)
    cadenceData[1] = crankRevolutions & 0xFF;        // LSB
    cadenceData[2] = (crankRevolutions >> 8) & 0xFF; // MSB
    // Last Crank Event Time (Little Endian) in 1/1024 seconds
    cadenceData[3] = lastCrankEventTime & 0xFF;        // LSB
    cadenceData[4] = (lastCrankEventTime >> 8) & 0xFF; // MSB

    pCadenceChar->setValue(cadenceData, 5); // Only using 5 bytes
    pCadenceChar->notify();

    // 3. Heart Rate characteristic - Heart Rate Measurement
    uint8_t heartData[2];
    heartData[0] = 0x00;      // Flags: 0x00 = UINT8 format for heart rate
    heartData[1] = heartRate; // Heart rate value

    pHeartRateChar->setValue(heartData, sizeof(heartData));
    pHeartRateChar->notify();

    // Debug information
    Serial.print("Power: ");
    Serial.print(power);
    Serial.print(" W, Cadence: ");
    Serial.print(cadence);
    Serial.print(" rpm, HR: ");
    Serial.print(heartRate);
    Serial.print(" bpm, Crank Revs: ");
    Serial.println(crankRevolutions);
}
