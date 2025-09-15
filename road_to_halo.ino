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
// Training parameters
const uint16_t FTP = 250;                     // Functional Threshold Power in watts
const uint8_t HR_BASE = 135;                  // Base heart rate at 50% FTP
const uint8_t HR_MAX = 170;                   // Max heart rate at 127% FTP

// Custom workout generation parameters
const uint16_t CUSTOM_DURATION_MINUTES = 240; // Training duration for 10-90% range in minutes
const float CUSTOM_TARGET_TSS = 350.0;        // Target TSS for the custom interval
const float START_BOUNDARY_POWER = 1.20;      // Power at start of custom range (from 10% mark)
const float END_BOUNDARY_POWER = 0.76;        // Power at end of custom range (to 90% mark)

// W'bal parameters
const float CRITICAL_POWER_RATIO = 1.06;      // CP as ratio of FTP (CP typically 6% higher than FTP)
const uint32_t W_PRIME_JOULES = 30000;        // W' anaerobic work capacity in joules (30kJ)
const float MIN_POWER_RATIO = 0.3;            // Minimum power as ratio of FTP (no zero power zones)

// Recovery constants for W'bal model (from Skiba et al.)
const float TAU_CONSTANT_A = 546.0;           // Time constant coefficient A
const float TAU_CONSTANT_B = 316.0;           // Time constant coefficient B
const float TAU_CONSTANT_C = 0.01;            // Time constant coefficient C

// Fixed segments for first and last 10%
const uint8_t FIXED_START_SEGMENTS = 11;      // Number of fixed segments at start
const uint8_t FIXED_END_SEGMENTS = 7;         // Number of fixed segments at end
const uint8_t MAX_CUSTOM_SEGMENTS = 30;       // Maximum custom segments to generate
// ============================ END CONFIGURABLE PARAMETERS ============================

// Workout segment structure
struct WorkoutSegment {
    uint16_t duration;    // Duration in seconds
    float powerLow;       // Starting power multiplier
    float powerHigh;      // Ending power multiplier
};

// Fixed start segments (first 10% of workout)
WorkoutSegment fixedStartSegments[] = {
    {110, 0.5, 0.56},
    {231, 0.56, 0.0},
    {65, 0.0, 0.51},
    {285, 0.51, 0.0},
    {74, 0.0, 1.27},
    {120, 1.27, 0.67},
    {89, 0.67, 0.46},
    {78, 0.46, 1.1},
    {89, 1.1, 1.14},
    {62, 1.14, 1.27},
    {188, 1.27, START_BOUNDARY_POWER}
};

// Fixed end segments (last 10% of workout)
WorkoutSegment fixedEndSegments[] = {
    {491, END_BOUNDARY_POWER, 0.72},
    {179, 0.72, 0.81},
    {415, 0.81, 0.93},
    {406, 0.93, 0.86},
    {178, 0.86, 0.95},
    {107, 0.95, 0.95},
    {71, 0.95, 0.0}
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

// Calculate W'bal depletion/recovery for a power level over duration
float calculateWbalChange(float powerRatio, uint16_t duration) {
    float criticalPowerRatio = CRITICAL_POWER_RATIO;

    if (powerRatio <= criticalPowerRatio) {
        // Recovery: exponential recovery below CP
        float dcp = criticalPowerRatio - powerRatio;
        float tau = TAU_CONSTANT_A * exp(-TAU_CONSTANT_B * dcp) + TAU_CONSTANT_C;
        // Simplified recovery: approximate exponential recovery
        return W_PRIME_JOULES * (1.0 - exp(-duration / tau));
    } else {
        // Depletion: linear depletion above CP
        float excessPower = (powerRatio - criticalPowerRatio) * FTP; // watts above CP
        return -(excessPower * duration); // negative = depletion in joules
    }
}

// Check if segment is physiologically feasible given current W'bal
bool isSegmentFeasible(float powerStart, float powerEnd, uint16_t duration, float currentWbal) {
    // Check maximum depletion during segment
    float maxPower = (powerStart > powerEnd) ? powerStart : powerEnd;
    if (maxPower <= CRITICAL_POWER_RATIO) {
        return true; // Recovery or maintenance - always feasible
    }

    // Estimate worst-case depletion (assuming max power for full duration)
    float worstCaseDepletion = -calculateWbalChange(maxPower, duration);

    // Need some safety margin (20% of W')
    float safetyMargin = W_PRIME_JOULES * 0.2;

    return (currentWbal - worstCaseDepletion) > safetyMargin;
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
    Serial.print("Critical Power: "); Serial.print((uint16_t)(CRITICAL_POWER_RATIO * FTP)); Serial.println(" W");
    Serial.print("W' Capacity: "); Serial.print(W_PRIME_JOULES / 1000.0); Serial.println(" kJ");
    Serial.print("Min Power: "); Serial.print((uint16_t)(MIN_POWER_RATIO * FTP)); Serial.println(" W");
    Serial.print("Heart Rate Range: "); Serial.print(HR_BASE); Serial.print("-"); Serial.print(HR_MAX); Serial.println(" BPM");
    Serial.println();
    Serial.print("Target Duration: "); Serial.print(CUSTOM_DURATION_MINUTES); Serial.println(" min");
    Serial.print("Target TSS: "); Serial.println(CUSTOM_TARGET_TSS);
    Serial.println("----------------------------------------");
    Serial.println();
}

// Print workout visualization in ASCII art (Zwift-style)
void printWorkoutVisualization() {
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
        if (i < FIXED_START_SEGMENTS) {
            strcpy(segmentType, "WARMUP");
        } else if (i >= FIXED_START_SEGMENTS && i < FIXED_START_SEGMENTS + (totalWorkoutSegments - FIXED_START_SEGMENTS - FIXED_END_SEGMENTS)) {
            strcpy(segmentType, "W'BAL-CUSTOM");
        } else {
            strcpy(segmentType, "COOLDOWN");
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

        // Ensure at least 1 character width for very short segments
        if (endPos == startPos) endPos = startPos + 1;
        if (endPos >= VIZ_WIDTH) endPos = VIZ_WIDTH - 1;

        // Fill positions with interpolated power
        for (uint8_t pos = startPos; pos <= endPos && pos < VIZ_WIDTH; pos++) {
            float segProgress = (float)(pos - startPos) / (float)(endPos - startPos + 1);
            vizPower[pos] = workout[seg].powerLow +
                           (workout[seg].powerHigh - workout[seg].powerLow) * segProgress;

            // Set segment type
            if (seg < FIXED_START_SEGMENTS) {
                vizType[pos] = 'W'; // Warmup
            } else if (seg >= FIXED_START_SEGMENTS &&
                      seg < FIXED_START_SEGMENTS + (totalWorkoutSegments - FIXED_START_SEGMENTS - FIXED_END_SEGMENTS)) {
                vizType[pos] = 'T'; // Training (W'bal constrained)
            } else {
                vizType[pos] = 'C'; // Cooldown
            }
        }

        cumulativeTime += workout[seg].duration;
    }

    // Print enhanced time scale (every 30 minutes with markers)
    Serial.print("Time: ");
    for (uint8_t pos = 0; pos < VIZ_WIDTH; pos++) {
        uint32_t timeAtPos = (pos * totalWorkoutDuration) / VIZ_WIDTH / 60; // minutes

        // Print time labels at specific positions
        if (pos % 20 == 0 && timeAtPos % 30 == 0) {
            if (timeAtPos < 100) Serial.print(" ");
            if (timeAtPos < 10) Serial.print(" ");
            Serial.print(timeAtPos);
            Serial.print("m");
            pos += 3; // Skip next few positions to avoid overlap
        } else {
            Serial.print(" ");
        }
    }
    Serial.println();

    // Print tick marks
    Serial.print("      ");
    for (uint8_t pos = 0; pos < VIZ_WIDTH; pos++) {
        uint32_t timeAtPos = (pos * totalWorkoutDuration) / VIZ_WIDTH / 60; // minutes
        if (timeAtPos % 30 == 0 && pos % 20 == 0) {
            Serial.print("|");
        } else if (timeAtPos % 15 == 0) {
            Serial.print(":");
        } else if (timeAtPos % 5 == 0) {
            Serial.print(".");
        } else {
            Serial.print(" ");
        }
    }
    Serial.println();

    // Print power levels with improved visualization
    const float powerLevels[] = {1.5, 1.25, 1.0, 0.75, 0.5, 0.25};
    const char* levelLabels[] = {"150%: ", "125%: ", "100%: ", " 75%: ", " 50%: ", " 25%: "};

    for (uint8_t level = 0; level < 6; level++) {
        Serial.print(levelLabels[level]);

        for (uint8_t pos = 0; pos < VIZ_WIDTH; pos++) {
            float power = vizPower[pos];

            if (power >= powerLevels[level]) {
                // Use ASCII characters for different intensities
                if (power >= 1.4) {
                    Serial.print("#"); // Hash for very high intensity
                } else if (power >= 1.2) {
                    Serial.print("H"); // H for high intensity
                } else if (power >= 0.9) {
                    Serial.print("M"); // M for moderate-high
                } else if (power >= 0.6) {
                    Serial.print("L"); // L for moderate
                } else {
                    Serial.print("."); // Dot for low intensity
                }
            } else {
                Serial.print(" ");
            }
        }
        Serial.println();
    }

    // Print current position marker with improved accuracy
    Serial.print("NOW:  ");
    uint8_t currentPos = (workoutElapsed * VIZ_WIDTH) / totalWorkoutDuration;
    for (uint8_t i = 0; i < VIZ_WIDTH; i++) {
        if (i == currentPos) {
            Serial.print("V"); // V for current position
        } else if (abs((int)i - (int)currentPos) <= 1) {
            Serial.print("^"); // Caret near current position
        } else {
            Serial.print(" ");
        }
    }
    Serial.println();

    // Print segment type indicators
    Serial.print("Type: ");
    for (uint8_t pos = 0; pos < VIZ_WIDTH; pos++) {
        Serial.print(vizType[pos]);
    }
    Serial.println();

    // Print power zone indicators
    Serial.print("Zone: ");
    for (uint8_t pos = 0; pos < VIZ_WIDTH; pos++) {
        float power = vizPower[pos];
        char zoneChar = ' ';

        if (power >= 1.2) zoneChar = '6';      // VO2 Max
        else if (power >= 1.05) zoneChar = '5'; // Threshold
        else if (power >= 0.9) zoneChar = '4';  // Lactate Threshold
        else if (power >= 0.75) zoneChar = '3'; // Tempo
        else if (power >= 0.55) zoneChar = '2'; // Endurance
        else if (power >= 0.0) zoneChar = '1';  // Recovery

        Serial.print(zoneChar);
    }
    Serial.println();

    Serial.println("      W=Warmup, T=Training, C=Cooldown");
    Serial.println("      Zones: 1=Recovery, 2=Endurance, 3=Tempo, 4=LT, 5=Threshold, 6=VO2Max");

    // Enhanced current status with more details
    Serial.print("Progress: ");
    Serial.print(workoutElapsedMin);
    Serial.print("/");
    Serial.print(totalWorkoutMin);
    Serial.print("min (");
    Serial.print((workoutElapsed * 100) / totalWorkoutDuration);
    Serial.print("%) | Current: ");
    Serial.print(power);
    Serial.print("W (");
    Serial.print((int)((float)power / FTP * 100));
    Serial.print("%) | HR: ");
    Serial.print(heartRate);
    Serial.print(" BPM | Cadence: ");
    Serial.print(cadence);
    Serial.print(" RPM");

    // Show current segment info
    if (!workoutFinished && currentSegment < totalWorkoutSegments) {
        Serial.print(" | Segment: ");
        Serial.print(currentSegment + 1);
        Serial.print("/");
        Serial.print(totalWorkoutSegments);

        uint32_t segmentElapsed = (currentTime - segmentStartTime) / 1000;
        uint32_t segmentRemaining = workout[currentSegment].duration - segmentElapsed;
        Serial.print(" (");
        Serial.print(segmentRemaining / 60);
        Serial.print(":");
        if ((segmentRemaining % 60) < 10) Serial.print("0");
        Serial.print(segmentRemaining % 60);
        Serial.print(" left)");
    }

    Serial.println();

    // Cleanup
    delete[] vizPower;
    delete[] vizType;

    Serial.println("=======================================");
    Serial.println();
}

// Generate physiologically feasible custom workout with W'bal constraints
void generateCustomWorkout() {
    Serial.println("Generating W'bal-constrained custom workout...");
    Serial.print("Target duration: ");
    Serial.print(CUSTOM_DURATION_MINUTES);
    Serial.println(" minutes");
    Serial.print("Target TSS: ");
    Serial.println(CUSTOM_TARGET_TSS);
    Serial.print("W' capacity: ");
    Serial.print(W_PRIME_JOULES / 1000.0);
    Serial.println(" kJ");
    Serial.print("Critical Power: ");
    Serial.print(CRITICAL_POWER_RATIO * FTP);
    Serial.println(" W");
    Serial.print("Min Power: ");
    Serial.print(MIN_POWER_RATIO * FTP);
    Serial.println(" W");

    // Set random seed based on current time
    randomSeed(millis());

    // Calculate target parameters
    uint32_t targetDurationSeconds = CUSTOM_DURATION_MINUTES * 60;

    // Calculate average IF needed to achieve target TSS
    float targetAverageIF = sqrt(CUSTOM_TARGET_TSS * 3600.0 / (targetDurationSeconds * 100.0));

    Serial.print("Target average IF: ");
    Serial.print(targetAverageIF, 3);
    Serial.print(" (");
    Serial.print(targetAverageIF * 100, 1);
    Serial.println("% FTP)");

    // Generate random number of segments (15-30)
    uint8_t numCustomSegments = randomInt(15, MAX_CUSTOM_SEGMENTS);
    Serial.print("Generating ");
    Serial.print(numCustomSegments);
    Serial.println(" W'bal-constrained segments");

    // Calculate total segments needed
    totalWorkoutSegments = FIXED_START_SEGMENTS + numCustomSegments + FIXED_END_SEGMENTS;

    // Allocate memory for complete workout
    if (workout != nullptr) {
        free(workout);
    }
    workout = (WorkoutSegment*)malloc(totalWorkoutSegments * sizeof(WorkoutSegment));

    // Copy fixed start segments
    for (uint8_t i = 0; i < FIXED_START_SEGMENTS; i++) {
        workout[i] = fixedStartSegments[i];
    }

    // Copy fixed end segments
    for (uint8_t i = 0; i < FIXED_END_SEGMENTS; i++) {
        workout[FIXED_START_SEGMENTS + numCustomSegments + i] = fixedEndSegments[i];
    }

    // Generate random segment durations that sum to target duration
    uint16_t* segmentDurations = (uint16_t*)malloc(numCustomSegments * sizeof(uint16_t));
    uint32_t remainingDuration = targetDurationSeconds;

    for (uint8_t i = 0; i < numCustomSegments - 1; i++) {
        // Each segment: 2-30 minutes (shorter for W'bal management)
        uint16_t minDuration = 120;  // 2 minutes
        uint32_t remainingForOthers = (numCustomSegments - i - 1) * minDuration;
        uint32_t availableDuration = remainingDuration - remainingForOthers;
        uint16_t maxDuration = (availableDuration > 1800) ? 1800 : (uint16_t)availableDuration; // 30 minutes max
        segmentDurations[i] = randomInt(minDuration, maxDuration);
        remainingDuration -= segmentDurations[i];
    }
    // Last segment gets remaining duration
    segmentDurations[numCustomSegments - 1] = (uint16_t)remainingDuration;

    // Generate W'bal-constrained power levels
    float* segmentPowers = (float*)malloc((numCustomSegments + 1) * sizeof(float));
    segmentPowers[0] = START_BOUNDARY_POWER; // First power level (boundary)

    // Simulate W'bal through segments to ensure feasibility
    float currentWbal = W_PRIME_JOULES; // Start with full W'
    uint8_t attempts = 0;
    const uint8_t maxAttempts = 50;

    do {
        attempts++;
        currentWbal = W_PRIME_JOULES; // Reset W'bal for each attempt
        bool allSegmentsFeasible = true;

        // Generate intermediate power levels with W'bal constraints
        for (uint8_t i = 1; i < numCustomSegments; i++) {
            float targetPower;
            uint8_t powerAttempts = 0;

            do {
                powerAttempts++;
                // Vary around target average IF, but respect physiological limits
                float lowerBound = targetAverageIF * 0.6f;
                float upperBound = targetAverageIF * 1.4f;

                // Ensure minimum power constraint
                if (lowerBound < MIN_POWER_RATIO) lowerBound = MIN_POWER_RATIO;

                // Limit maximum power based on segment duration to prevent W'bal depletion
                uint16_t segmentDuration = segmentDurations[i - 1]; // Previous segment duration
                if (segmentDuration > 600) { // > 10 minutes
                    if (upperBound > CRITICAL_POWER_RATIO * 1.1f) upperBound = CRITICAL_POWER_RATIO * 1.1f;
                } else if (segmentDuration > 300) { // > 5 minutes
                    if (upperBound > CRITICAL_POWER_RATIO * 1.2f) upperBound = CRITICAL_POWER_RATIO * 1.2f;
                }

                targetPower = randomFloat(lowerBound, upperBound);

            } while (!isSegmentFeasible(segmentPowers[i-1], targetPower, segmentDurations[i-1], currentWbal) &&
                     powerAttempts < 20);

            if (powerAttempts >= 20) {
                allSegmentsFeasible = false;
                break;
            }

            segmentPowers[i] = targetPower;

            // Update W'bal simulation
            float avgPower = (segmentPowers[i-1] + segmentPowers[i]) / 2.0f;
            float wbalChange = calculateWbalChange(avgPower, segmentDurations[i-1]);
            currentWbal += wbalChange;

            // Clamp W'bal to valid range
            if (currentWbal > W_PRIME_JOULES) currentWbal = W_PRIME_JOULES;
            if (currentWbal < 0) currentWbal = 0;
        }

        if (allSegmentsFeasible) break;

        Serial.print("W'bal constraint attempt ");
        Serial.print(attempts);
        Serial.println(" - retrying...");

    } while (attempts < maxAttempts);

    if (attempts >= maxAttempts) {
        Serial.println("Warning: Could not satisfy all W'bal constraints, using best attempt");
    }

    segmentPowers[numCustomSegments] = END_BOUNDARY_POWER; // Last power level (boundary)

    // Create custom segments
    for (uint8_t i = 0; i < numCustomSegments; i++) {
        uint8_t workoutIndex = FIXED_START_SEGMENTS + i;
        workout[workoutIndex].duration = segmentDurations[i];
        workout[workoutIndex].powerLow = segmentPowers[i];
        workout[workoutIndex].powerHigh = segmentPowers[i + 1];
    }

    // Calculate current TSS for generated segments
    float currentTSS = 0.0;
    for (uint8_t i = FIXED_START_SEGMENTS; i < FIXED_START_SEGMENTS + numCustomSegments; i++) {
        currentTSS += calculateSegmentTSS(workout[i].powerLow, workout[i].powerHigh, workout[i].duration);
    }

    Serial.print("Generated TSS: ");
    Serial.println(currentTSS, 2);

    // Normalize power levels to achieve target TSS while maintaining W'bal constraints
    if (currentTSS > 0) {
        float normalizationFactor = sqrt(CUSTOM_TARGET_TSS / currentTSS);
        Serial.print("TSS normalization factor: ");
        Serial.println(normalizationFactor, 4);

        // Apply careful normalization to intermediate power levels
        for (uint8_t i = 1; i < numCustomSegments; i++) {
            segmentPowers[i] *= normalizationFactor;

            // Ensure constraints are still met after normalization
            if (segmentPowers[i] < MIN_POWER_RATIO) segmentPowers[i] = MIN_POWER_RATIO;
            if (segmentPowers[i] > 1.5f) segmentPowers[i] = 1.5f;
        }

        // Update workout segments with normalized power levels
        for (uint8_t i = 0; i < numCustomSegments; i++) {
            uint8_t workoutIndex = FIXED_START_SEGMENTS + i;
            workout[workoutIndex].powerLow = segmentPowers[i];
            workout[workoutIndex].powerHigh = segmentPowers[i + 1];
        }

        // Verify final TSS
        float finalTSS = 0.0;
        for (uint8_t i = FIXED_START_SEGMENTS; i < FIXED_START_SEGMENTS + numCustomSegments; i++) {
            finalTSS += calculateSegmentTSS(workout[i].powerLow, workout[i].powerHigh, workout[i].duration);
        }

        Serial.print("Final TSS: ");
        Serial.println(finalTSS, 2);
        Serial.print("TSS error: ");
        Serial.print(((finalTSS - CUSTOM_TARGET_TSS) / CUSTOM_TARGET_TSS * 100.0), 2);
        Serial.println("%");
    }

    // Clean up temporary arrays
    free(segmentDurations);
    free(segmentPowers);

    Serial.print("W'bal generation completed in ");
    Serial.print(attempts);
    Serial.println(" attempts!");

    // Print workout summary
    Serial.print("Total segments: ");
    Serial.println(totalWorkoutSegments);
    Serial.print("W'bal-constrained segments: ");
    Serial.print(FIXED_START_SEGMENTS + 1);
    Serial.print(" to ");
    Serial.println(FIXED_START_SEGMENTS + numCustomSegments);
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

    // Generate custom workout with W'bal constraints
    generateCustomWorkout();

    // Calculate total workout duration
    for (uint8_t i = 0; i < totalWorkoutSegments; i++) {
        totalWorkoutDuration += workout[i].duration;
    }

    Serial.print("Total workout duration: ");
    Serial.print(totalWorkoutDuration / 60);
    Serial.println(" minutes");
    Serial.println();

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
    float powerMultiplier = MIN_POWER_RATIO; // Default to minimum power

    if (currentSegment < totalWorkoutSegments) {
        WorkoutSegment &segment = workout[currentSegment];

        // Interpolate between starting and ending power for smooth transitions
        float progress = (float)segmentElapsed / (float)segment.duration;
        powerMultiplier = segment.powerLow + (segment.powerHigh - segment.powerLow) * progress;

        // Ensure minimum power constraint
        if (powerMultiplier < MIN_POWER_RATIO) powerMultiplier = MIN_POWER_RATIO;
    }

    // Calculate absolute power
    power = (uint16_t)(FTP * powerMultiplier);

    // Calculate heart rate based on power multiplier
    // HR = 135 + (multiplier - 0.5) * 58.4
    // This gives HR 135 at 50% FTP and HR 170 at 127% FTP
    float heartRateFloat = HR_BASE + (powerMultiplier - 0.5) * ((HR_MAX - HR_BASE) / (1.27 - 0.5));
    heartRate = (uint8_t)constrain(heartRateFloat, 90, 180);

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
    Serial.print(" RPM | Seg ");
    Serial.print(currentSegment + 1);
    Serial.print("/");
    Serial.print(totalWorkoutSegments);

    if (workoutFinished) {
        Serial.print(" [RECOVERY]");
    } else if (currentSegment >= FIXED_START_SEGMENTS &&
               currentSegment < FIXED_START_SEGMENTS + (totalWorkoutSegments - FIXED_START_SEGMENTS - FIXED_END_SEGMENTS)) {
        Serial.print(" [W'BAL]");
    } else if (currentSegment < FIXED_START_SEGMENTS) {
        Serial.print(" [WARMUP]");
    } else {
        Serial.print(" [COOLDOWN]");
    }

    Serial.println();
}
