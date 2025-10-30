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

// ============================ CONFIGURABLE PARAMETERS ============================
// Mode selection: "auto" or "manual"
const String TRAINING_MODE = "manual";            // Training mode selection

// Training parameters
const uint16_t FTP = 250;                     // Functional Threshold Power in watts
const uint8_t HR_BASE = 135;                  // Base heart rate at 50% FTP
const uint8_t HR_MAX = 170;                   // Max heart rate at 127% FTP

// Custom workout generation parameters
const uint16_t CUSTOM_DURATION_MINUTES = 240; // Training duration for main interval block in minutes
const float CUSTOM_TARGET_TSS = 350.0;        // Target TSS for the custom interval

// Workout structure parameters
const uint8_t MAX_CUSTOM_SEGMENTS = 70;       // Exact number of custom segments to generate

// Manual mode parameters
const uint16_t MANUAL_BASE_POWER = 200;       // Base power for manual mode in watts
const uint16_t MANUAL_POWER_STEP = 10;        // Power increment/decrement step in watts
const uint16_t MANUAL_DURATION_MINUTES = 60;  // Default duration for manual mode

// Button pins for manual mode
const uint8_t BUTTON_POWER_DOWN = 13;         // D13 - decrease power button
const uint8_t BUTTON_POWER_UP = 15;           // D15 - increase power button
const uint16_t BUTTON_DEBOUNCE_MS = 200;      // Button debounce delay in milliseconds
// ============================ END CONFIGURABLE PARAMETERS ============================

// Workout segment structure
struct WorkoutSegment {
    uint16_t duration;    // Duration in seconds
    float powerLow;       // Starting power multiplier
    float powerHigh;      // Ending power multiplier
};

// Dynamic workout array
WorkoutSegment* workout = nullptr;
uint8_t totalWorkoutSegments = 0;

// Global variables
BLEServer *pServer = nullptr;
BLECharacteristic *pPowerChar = nullptr;
BLECharacteristic *pCadenceChar = nullptr;
BLECharacteristic *pHeartRateChar = nullptr;

// Simulation variables
uint32_t lastUpdate = 0;
uint32_t lastVisualizationUpdate = 0;
uint16_t power = 0; // Current power in watts
uint8_t cadence = 90; // Initial cadence in rpm
bool cadenceIncreasing = true;
uint8_t heartRate = 135; // Current heart rate

// Workout tracking variables
uint32_t workoutStartTime = 0;
uint32_t totalWorkoutDuration = 0;
uint8_t currentSegment = 0;
uint32_t segmentStartTime = 0;
bool workoutActive = false;
bool workoutFinished = false;
uint32_t cooldownStartTime = 0;
uint8_t finalHeartRate = 135;

// Manual mode variables
uint16_t manualTargetPower = MANUAL_BASE_POWER;  // Current target power in manual mode
uint32_t lastButtonPressDown = 0;                // Last time power down button was pressed
uint32_t lastButtonPressUp = 0;                  // Last time power up button was pressed
bool buttonDownLastState = HIGH;                 // Previous state of power down button
bool buttonUpLastState = HIGH;                   // Previous state of power up button

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

// Calculate TSS for a segment with linear power interpolation
float calculateSegmentTSS(float powerStart, float powerEnd, uint16_t duration) {
    // Average power for interpolated segment
    float averagePower = (powerStart + powerEnd) / 2.0;
    // Intensity Factor (IF) = averagePower (since FTP = 1.0 in relative units)
    float IF = averagePower;
    // TSS = (seconds × IF²) × 100 / 3600
    return (duration * IF * IF * 100.0) / 3600.0;
}

// Generate random float between min and max
float randomFloat(float minVal, float maxVal) {
    return minVal + (float)random(0, 10000) / 10000.0 * (maxVal - minVal);
}

// Generate random integer between min and max (inclusive)
uint16_t randomInt(uint16_t minVal, uint16_t maxVal) {
    return minVal + random(0, maxVal - minVal + 1);
}

// Print current configuration
void printConfiguration() {
    Serial.println("CONFIGURATION:");
    Serial.println("----------------------------------------");
    Serial.print("FTP: "); Serial.print(FTP); Serial.println(" W");
    Serial.print("Heart Rate Range: "); Serial.print(HR_BASE); Serial.print("-"); Serial.print(HR_MAX); Serial.println(" BPM");
    Serial.println();
    if (TRAINING_MODE == "manual") {
        Serial.println("MANUAL MODE ACTIVE");
        Serial.print("Duration: "); Serial.print(MANUAL_DURATION_MINUTES); Serial.println(" min");
        Serial.print("Base Power: "); Serial.print(MANUAL_BASE_POWER); Serial.println(" W");
        Serial.print("Power Step: "); Serial.print(MANUAL_POWER_STEP); Serial.println(" W");
    } else {
        Serial.print("Target Duration: "); Serial.print(CUSTOM_DURATION_MINUTES); Serial.println(" min");
        Serial.print("Target TSS: "); Serial.println(CUSTOM_TARGET_TSS);
    }
    Serial.println("----------------------------------------");
    Serial.println();
}

// Print workout visualization in ASCII art (Zwift-style)
void printWorkoutVisualization() {
    if (TRAINING_MODE == "manual") return; // Skip visualization for manual mode

    Serial.println("WORKOUT PROFILE VISUALIZATION:");
    Serial.println("========================================");

    // Print power scale on the left
    Serial.println("Power |");
    Serial.println("(FTP) |");
    Serial.println("150%  |");
    Serial.println("      |     ##");
    Serial.println("125%  |    ##  ##");
    Serial.println("      |   ##    ##");
    Serial.println("100%  |##############################");
    Serial.println("      |                              ##");
    Serial.println(" 75%  |                               ##");
    Serial.println("      |                                ##");
    Serial.println(" 50%  |                                 ##");
    Serial.println("      |___________________________________");
    Serial.println("      0   60  120 180 240 300 360  Time(min)");

    Serial.println();
    Serial.println("DETAILED SEGMENTS:");
    Serial.println("Seg | Time  | Start | End   | Duration | Type");
    Serial.println("----|-------|-------|-------|----------|----------");

    uint32_t cumulativeTime = 0;
    for (uint8_t i = 0; i < totalWorkoutSegments; i++) {
        uint32_t segmentStart = cumulativeTime / 60; // minutes
        uint32_t segmentEnd = (cumulativeTime + workout[i].duration) / 60; // minutes
        uint32_t segmentDuration = workout[i].duration / 60; // minutes

        char segmentType[15];
        if (i == 0) {
            strcpy(segmentType, "WARMUP");
        } else if (i == totalWorkoutSegments - 1) {
            strcpy(segmentType, "COOLDOWN");
        } else {
            strcpy(segmentType, "INTERVAL");
        }

        Serial.print(i + 1 < 100 ? (i + 1 < 10 ? "  " : " ") : "");
        Serial.print(i + 1);
        Serial.print(" | ");
        Serial.print(segmentStart < 100 ? (segmentStart < 10 ? "   " : "  ") : " ");
        Serial.print(segmentStart);
        Serial.print("m | ");
        Serial.print((int)(workout[i].powerLow * 100) < 100 ? (workout[i].powerLow * 100 < 10 ? "  " : " ") : "");
        Serial.print((int)(workout[i].powerLow * 100));
        Serial.print("% | ");
        Serial.print((int)(workout[i].powerHigh * 100) < 100 ? (workout[i].powerHigh * 100 < 10 ? "  " : " ") : "");
        Serial.print((int)(workout[i].powerHigh * 100));
        Serial.print("% | ");
        Serial.print(segmentDuration < 100 ? (segmentDuration < 10 ? "     " : "    ") : "   ");
        Serial.print(segmentDuration);
        Serial.print("m | ");
        Serial.println(segmentType);

        cumulativeTime += workout[i].duration;
    }

    Serial.println("========================================");
    Serial.println();
}

// Print current workout status with improved full workout visualization
void printWorkoutStatus() {
    if (TRAINING_MODE == "manual") return; // Skip visualization for manual mode

    Serial.println();
    Serial.println("=== FULL WORKOUT PLAN VISUALIZATION ===");

    // Calculate current position in workout
    uint32_t currentTime = millis();
    uint32_t workoutElapsed = (currentTime - workoutStartTime) / 1000;
    uint32_t workoutElapsedMin = workoutElapsed / 60;
    uint32_t totalWorkoutMin = totalWorkoutDuration / 60;

    // Use wider visualization (120 characters instead of 60)
    const uint8_t VIZ_WIDTH = 120;

    // Create segment visualization array
    float* vizPower = new float[VIZ_WIDTH];
    char* vizType = new char[VIZ_WIDTH];

    // Initialize arrays
    for (uint8_t i = 0; i < VIZ_WIDTH; i++) {
        vizPower[i] = 0.0;
        vizType[i] = ' ';
    }

    // Fill visualization arrays
    uint32_t cumulativeTime = 0;
    for (uint8_t seg = 0; seg < totalWorkoutSegments; seg++) {
        uint32_t segmentStart = cumulativeTime;
        uint32_t segmentEnd = cumulativeTime + workout[seg].duration;

        // Map segment to visualization positions
        uint8_t startPos = (segmentStart * VIZ_WIDTH) / totalWorkoutDuration;
        uint8_t endPos = (segmentEnd * VIZ_WIDTH) / totalWorkoutDuration;

        // Ensure endPos doesn't exceed array bounds
        if (endPos >= VIZ_WIDTH) endPos = VIZ_WIDTH - 1;

        // Fill positions for this segment
        for (uint8_t pos = startPos; pos <= endPos; pos++) {
            float progress = (float)(pos - startPos) / (float)(endPos - startPos + 1);
            vizPower[pos] = workout[seg].powerLow + (workout[seg].powerHigh - workout[seg].powerLow) * progress;

            // Set segment type
            if (seg == 0) {
                vizType[pos] = 'W'; // Warmup
            } else if (seg == totalWorkoutSegments - 1) {
                vizType[pos] = 'C'; // Cooldown
            } else {
                vizType[pos] = 'I'; // Interval
            }
        }

        cumulativeTime += workout[seg].duration;
    }

    // Calculate current position marker
    uint8_t currentPos = (workoutElapsed * VIZ_WIDTH) / totalWorkoutDuration;
    if (currentPos >= VIZ_WIDTH) currentPos = VIZ_WIDTH - 1;

    // Print time scale header
    Serial.print("Time: ");
    for (uint8_t i = 0; i < VIZ_WIDTH; i += 20) {
        uint32_t timeAtPos = (i * totalWorkoutMin) / VIZ_WIDTH;
        char timeStr[10];
        sprintf(timeStr, "%-4d", timeAtPos);
        Serial.print(timeStr);
        for (uint8_t j = 4; j < 20 && i + j < VIZ_WIDTH; j++) {
            Serial.print(" ");
        }
    }
    Serial.println();

    // Print power visualization rows
    for (uint8_t powerLevel = 10; powerLevel >= 1; powerLevel--) {
        float powerThreshold = powerLevel * 0.15f; // 0.15 to 1.5 range (15% to 150% FTP)

        // Print power label
        char powerLabel[8];
        sprintf(powerLabel, "%3d%%:", (int)(powerThreshold * 100));
        Serial.print(powerLabel);

        // Print visualization
        for (uint8_t i = 0; i < VIZ_WIDTH; i++) {
            char displayChar = ' ';

            if (vizPower[i] >= powerThreshold) {
                if (vizType[i] == 'W') {
                    displayChar = 'w'; // Warmup (lowercase)
                } else if (vizType[i] == 'C') {
                    displayChar = 'c'; // Cooldown (lowercase)
                } else {
                    displayChar = '#'; // Interval (hash)
                }
            }

            // Mark current position
            if (i == currentPos) {
                displayChar = '|';
            }

            Serial.print(displayChar);
        }
        Serial.println();
    }

    // Print legend and status
    Serial.print("     ");
    for (uint8_t i = 0; i < VIZ_WIDTH; i++) {
        Serial.print("-");
    }
    Serial.println();

    Serial.print("Legend: # = Interval, w = Warmup, c = Cooldown, | = Current Position");
    Serial.print(" | Elapsed: ");
    Serial.print(workoutElapsedMin);
    Serial.print("/");
    Serial.print(totalWorkoutMin);
    Serial.println(" min");

    // Cleanup
    delete[] vizPower;
    delete[] vizType;

    Serial.println("=====================================");
}

// Generate complete workout plan with fixed segment count
void generateWorkout() {
    if (TRAINING_MODE == "manual") {
        // Simple manual mode - single segment for entire duration
        totalWorkoutSegments = 1;
        workout = new WorkoutSegment[1];
        workout[0].duration = MANUAL_DURATION_MINUTES * 60; // Convert to seconds
        workout[0].powerLow = (float)MANUAL_BASE_POWER / FTP;
        workout[0].powerHigh = (float)MANUAL_BASE_POWER / FTP;
        totalWorkoutDuration = MANUAL_DURATION_MINUTES * 60;
        return;
    }

    Serial.println("Generating sophisticated workout plan...");

    // Calculate total workout duration including warmup and cooldown
    uint16_t warmupDuration = 900;  // 15 minutes warmup
    uint16_t cooldownDuration = 600; // 10 minutes cooldown
    uint16_t mainDuration = CUSTOM_DURATION_MINUTES * 60; // Main workout duration in seconds

    totalWorkoutDuration = warmupDuration + mainDuration + cooldownDuration;

    // Allocate memory for the exact number of segments requested
    totalWorkoutSegments = MAX_CUSTOM_SEGMENTS;
    workout = new WorkoutSegment[totalWorkoutSegments];

    Serial.print("Target segments: ");
    Serial.println(totalWorkoutSegments);

    // Calculate segments distribution
    uint8_t customSegments = totalWorkoutSegments - 2; // Subtract warmup and cooldown
    uint16_t avgCustomSegmentDuration = mainDuration / customSegments;

    Serial.print("Average custom segment duration: ");
    Serial.print(avgCustomSegmentDuration / 60.0, 1);
    Serial.println(" minutes");

    // Segment 1: Warmup (15 minutes, 30% to 70% FTP)
    workout[0].duration = warmupDuration;
    workout[0].powerLow = 0.30f;
    workout[0].powerHigh = 0.70f;

    // Generate custom intervals in the middle
    float totalTSS = 0.0;
    uint16_t remainingDuration = mainDuration;

    for (uint8_t i = 1; i < totalWorkoutSegments - 1; i++) {
        WorkoutSegment &segment = workout[i];

        // Calculate segment duration
        if (i == totalWorkoutSegments - 2) {
            // Last custom segment gets all remaining duration
            segment.duration = remainingDuration;
        } else {
            // Random variation around average duration (±30%)
            uint16_t minDuration = avgCustomSegmentDuration * 0.7;
            uint16_t maxDuration = avgCustomSegmentDuration * 1.3;
            segment.duration = randomInt(minDuration, maxDuration);

            // Ensure we don't exceed remaining duration
            if (segment.duration > remainingDuration - (totalWorkoutSegments - i - 2) * 60) {
                segment.duration = remainingDuration - (totalWorkoutSegments - i - 2) * 60;
            }
        }

        remainingDuration -= segment.duration;

        // Generate power profile for this segment
        // Different interval types with varying intensities
        uint8_t intervalType = random(0, 4);

        switch (intervalType) {
            case 0: // Steady state intervals (70-105% FTP)
                segment.powerLow = randomFloat(0.70f, 1.05f);
                segment.powerHigh = segment.powerLow + randomFloat(-0.02f, 0.02f); // Very small variation
                break;

            case 1: // Progressive intervals (60-120% FTP)
                segment.powerLow = randomFloat(0.60f, 0.85f);
                segment.powerHigh = segment.powerLow + randomFloat(0.15f, 0.35f);
                break;

            case 2: // VO2 Max intervals (110-130% FTP)
                segment.powerLow = randomFloat(1.10f, 1.30f);
                segment.powerHigh = segment.powerLow + randomFloat(-0.05f, 0.05f); // Small variation
                break;

            case 3: // Recovery intervals (50-75% FTP)
                segment.powerLow = randomFloat(0.50f, 0.75f);
                segment.powerHigh = segment.powerLow + randomFloat(-0.05f, 0.05f); // Small variation
                break;
        }

        // Ensure power doesn't exceed reasonable limits
        segment.powerLow = constrain(segment.powerLow, 0.30f, 1.50f);
        segment.powerHigh = constrain(segment.powerHigh, 0.30f, 1.50f);

        // Calculate TSS for this segment
        float segmentTSS = calculateSegmentTSS(segment.powerLow, segment.powerHigh, segment.duration);
        totalTSS += segmentTSS;
    }

    // Final segment: Cooldown (10 minutes, 70% to 30% FTP)
    workout[totalWorkoutSegments - 1].duration = cooldownDuration;
    workout[totalWorkoutSegments - 1].powerLow = 0.70f;
    workout[totalWorkoutSegments - 1].powerHigh = 0.30f;

    // Add cooldown TSS
    totalTSS += calculateSegmentTSS(0.70f, 0.30f, cooldownDuration);

    Serial.println();
    Serial.println("WORKOUT GENERATION COMPLETE!");
    Serial.println("=====================================");
    Serial.print("Total segments generated: ");
    Serial.println(totalWorkoutSegments);
    Serial.print("Total workout duration: ");
    Serial.print(totalWorkoutDuration / 60.0, 1);
    Serial.println(" minutes");
    Serial.print("Calculated TSS: ");
    Serial.println(totalTSS, 1);
    Serial.print("Target TSS: ");
    Serial.println(CUSTOM_TARGET_TSS, 1);
    Serial.print("TSS variance: ");
    Serial.print((totalTSS - CUSTOM_TARGET_TSS) / CUSTOM_TARGET_TSS * 100, 1);
    Serial.println("%");
    Serial.println("=====================================");
    Serial.println();
}

// Handle button input for manual mode power control
void handleManualButtons() {
    if (TRAINING_MODE != "manual") return;

    uint32_t currentTime = millis();
    bool buttonDownState = digitalRead(BUTTON_POWER_DOWN);
    bool buttonUpState = digitalRead(BUTTON_POWER_UP);

    // Handle power down button (D13)
    if (buttonDownLastState == HIGH && buttonDownState == LOW) {
        // Button pressed (falling edge)
        if (currentTime - lastButtonPressDown > BUTTON_DEBOUNCE_MS) {
            lastButtonPressDown = currentTime;
        }
    }
    if (buttonDownLastState == LOW && buttonDownState == HIGH) {
        // Button released (rising edge) - this is when we trigger the action
        if (currentTime - lastButtonPressDown > 50 && currentTime - lastButtonPressDown < BUTTON_DEBOUNCE_MS) {
            // Valid button press-release cycle
            if (manualTargetPower > 50) { // Minimum power limit
                manualTargetPower = max(50, (int)manualTargetPower - (int)MANUAL_POWER_STEP);
                Serial.print("Power decreased to: ");
                Serial.print(manualTargetPower);
                Serial.println("W");
            }
        }
    }
    buttonDownLastState = buttonDownState;

    // Handle power up button (D15)
    if (buttonUpLastState == HIGH && buttonUpState == LOW) {
        // Button pressed (falling edge)
        if (currentTime - lastButtonPressUp > BUTTON_DEBOUNCE_MS) {
            lastButtonPressUp = currentTime;
        }
    }
    if (buttonUpLastState == LOW && buttonUpState == HIGH) {
        // Button released (rising edge) - this is when we trigger the action
        if (currentTime - lastButtonPressUp > 50 && currentTime - lastButtonPressUp < BUTTON_DEBOUNCE_MS) {
            // Valid button press-release cycle
            if (manualTargetPower < 500) { // Maximum power limit
                manualTargetPower = min(500, (int)manualTargetPower + (int)MANUAL_POWER_STEP);
                Serial.print("Power increased to: ");
                Serial.print(manualTargetPower);
                Serial.println("W");
            }
        }
    }
    buttonUpLastState = buttonUpState;
}

void setup()
{
    Serial.begin(115200);
    delay(1000); // Wait for serial connection to stabilize
    Serial.println();
    Serial.println("========================================");
    Serial.println("    BLE Cycling Emulator for Zwift");
    Serial.println("========================================");

    // Display current configuration
    printConfiguration();

    // Initialize button pins for manual mode
    if (TRAINING_MODE == "manual") {
        pinMode(BUTTON_POWER_DOWN, INPUT_PULLUP);
        pinMode(BUTTON_POWER_UP, INPUT_PULLUP);
    }

    // Generate workout
    generateWorkout();

    // Print workout visualization
    printWorkoutVisualization();

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
    BLEService *pHeartRateService = pServer->createService(SERVICE_UUID_HEART_RATE);
    pHeartRateChar = pHeartRateService->createCharacteristic(
        CHAR_UUID_HEART_RATE,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY);
    pHeartRateChar->addDescriptor(new BLE2902());

    // Start services
    pPowerService->start();
    pCSCService->start();
    pHeartRateService->start();

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

    Serial.println("========================================");
    Serial.println("BLE emulator ready for Zwift connection!");
    Serial.println("Device name: 'Kickr Core 5638'");
    Serial.println("Services: Power, Cadence, Heart Rate");
    Serial.println("========================================");
    Serial.println();

    // Start workout when device boots
    startWorkout();
}

void loop()
{
    // Handle manual mode buttons
    handleManualButtons();

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

    // Print workout status every minute
    if (workoutActive && millis() - lastVisualizationUpdate > 60000) {
        lastVisualizationUpdate = millis();
        printWorkoutStatus();
    }

    // Small delay
    delay(20);
}

void startWorkout()
{
    workoutStartTime = millis();
    segmentStartTime = millis();
    currentSegment = 0;
    workoutActive = true;
    workoutFinished = false;

    Serial.println("Workout started!");
}

void updateValues()
{
    if (!workoutActive) return;

    uint32_t currentTime = millis();
    uint32_t workoutElapsed = (currentTime - workoutStartTime) / 1000; // Convert to seconds

    // Check if workout is finished
    if (workoutElapsed >= totalWorkoutDuration) {
        if (!workoutFinished) {
            workoutFinished = true;
            cooldownStartTime = currentTime;
            finalHeartRate = heartRate;
            Serial.println("Workout finished! Starting 5-minute recovery cooldown.");
        }

        // Post-workout recovery phase (5 minutes)
        uint32_t recoveryElapsed = (currentTime - cooldownStartTime) / 1000;
        if (recoveryElapsed < 300) { // 5 minutes = 300 seconds
            // Gradually reduce heart rate to 90 BPM over 5 minutes
            heartRate = finalHeartRate - ((finalHeartRate - 90) * recoveryElapsed / 300);
            power = 0;
            cadence = 0;
        } else {
            // Final state
            heartRate = 90;
            power = 0;
            cadence = 0;
        }

        return;
    }

    if (TRAINING_MODE == "manual") {
        // Manual mode: use target power directly without fluctuations
        power = manualTargetPower;

        // Calculate heart rate based on current power
        float powerMultiplier = (float)power / FTP;
        float heartRateFloat = HR_BASE + (powerMultiplier - 0.5) * ((HR_MAX - HR_BASE) / (1.27 - 0.5));
        heartRate = (uint8_t)constrain(heartRateFloat, 90, 180);
    } else {
        // Original auto mode logic

        // Find current segment
        uint32_t segmentElapsed = (currentTime - segmentStartTime) / 1000;
        if (segmentElapsed >= workout[currentSegment].duration) {
            // Move to next segment
            currentSegment++;
            segmentStartTime = currentTime;
            segmentElapsed = 0;

            if (currentSegment < totalWorkoutSegments) {
                Serial.print("Starting segment ");
                Serial.print(currentSegment + 1);
                Serial.print("/");
                Serial.println(totalWorkoutSegments);
            }
        }

        // Calculate current power based on segment
        float powerMultiplier = 0.30f; // Default to minimum power

        if (currentSegment < totalWorkoutSegments) {
            WorkoutSegment &segment = workout[currentSegment];

            // Interpolate between starting and ending power for smooth transitions
            float progress = (float)segmentElapsed / (float)segment.duration;
            powerMultiplier = segment.powerLow + (segment.powerHigh - segment.powerLow) * progress;

            // Ensure minimum power constraint
            if (powerMultiplier < 0.30f) powerMultiplier = 0.30f;
        }

        // Calculate absolute power with realistic power fluctuation (±2%)
        uint16_t basePower = (uint16_t)(FTP * powerMultiplier);

        // Add realistic power fluctuation (±2% random variation)
        float powerVariation = randomFloat(-0.02f, 0.02f); // ±2%
        power = (uint16_t)(basePower * (1.0f + powerVariation));

        // Ensure power doesn't go below reasonable minimum
        if (power < (uint16_t)(FTP * 0.25f)) {
            power = (uint16_t)(FTP * 0.25f);
        }

        // Calculate heart rate based on power multiplier
        // HR = 135 + (multiplier - 0.5) * 58.4
        // This gives HR 135 at 50% FTP and HR 170 at 127% FTP
        float heartRateFloat = HR_BASE + (powerMultiplier - 0.5) * ((HR_MAX - HR_BASE) / (1.27 - 0.5));
        heartRate = (uint8_t)constrain(heartRateFloat, 90, 180);
    }

    // Update cadence (keep original cycling behavior during workout)
    if (!workoutFinished) {
        if (cadenceIncreasing) {
            cadence++;
            if (cadence >= 98)
                cadenceIncreasing = false;
        } else {
            cadence--;
            if (cadence <= 90)
                cadenceIncreasing = true;
        }

        // Update crank revolutions only if cadence > 0
        if (cadence > 0) {
            crankRevolutions++;
            // Rotation time in 1/1024 sec (roughly corresponds to cadence)
            uint16_t timeDiff = 60 * 1024 / cadence;
            lastCrankEventTime = (lastCrankEventTime + timeDiff) % 65536;
        }
    }
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

    // Debug information - clean single line output
    Serial.print("Power: ");
    Serial.print(power);
    Serial.print("W (");
    Serial.print((int)((float)power / FTP * 100));
    Serial.print("%) | HR: ");
    Serial.print(heartRate);
    Serial.print(" BPM | Cadence: ");
    Serial.print(cadence);
    Serial.print(" RPM");

    if (TRAINING_MODE == "manual") {
        Serial.print(" | Target: ");
        Serial.print(manualTargetPower);
        Serial.print("W [MANUAL]");
    } else {
        Serial.print(" | Seg ");
        Serial.print(currentSegment + 1);
        Serial.print("/");
        Serial.print(totalWorkoutSegments);

        if (workoutFinished) {
            Serial.print(" [RECOVERY]");
        } else if (currentSegment == 0) {
            Serial.print(" [WARMUP]");
        } else if (currentSegment == totalWorkoutSegments - 1) {
            Serial.print(" [COOLDOWN]");
        } else {
            Serial.print(" [INTERVAL]");
        }
    }

    Serial.println();
}
