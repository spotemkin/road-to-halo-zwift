# ESP32 BLE Cycling Emulator for Zwift v0.2.46

An advanced Bluetooth Low Energy (BLE) emulator for Zwift running on ESP32, featuring intelligent workout generation with physiological constraints, real-time visualization, and dual operation modes for both structured training and manual power control.

![Road to Halo Screenshot](rth_01.png)

## Features

### Core Functionality
- **Complete Smart Trainer Emulation**: Simulates "Kickr Core 5638" with all three services:
  - Cycling Power Service (dynamic workout-based power with realistic fluctuation)
  - Cycling Speed and Cadence Service (90-98 RPM with realistic behavior)
  - Heart Rate Service (physiologically accurate HR based on power output)

### Dual Operation Modes
- **Auto Mode**: Structured workouts with intelligent TSS-targeted interval generation
- **Manual Mode**: Real-time power control via hardware buttons for interactive training sessions

### Auto Mode - Simplified Workout Generation
- **TSS-Targeted Training**: Generates custom workouts to hit specific Training Stress Score targets
- **Three-Phase Structure**:
  - Warmup: 30% to 100% FTP over 10 minutes
  - Main intervals: Configurable duration with intelligent intensity distribution
  - Cooldown: 60% to 30% FTP over 10 minutes
- **Smart Zone Distribution**: Prevents physiologically impossible interval sequences
- **Realistic Power Variation**: ±2% power fluctuation simulates natural pedaling

### Manual Mode - Interactive Power Control
- **Dual Control Options**: Choose between hardware buttons or analog joystick for power adjustment
  - **Button Control**: D13/D15 for discrete power steps
  - **Joystick Control**: Analog stick with press-release action for power changes
- **Debounce Protection**: Professional input handling prevents accidental multiple triggers
- **Smooth Power Transitions**: No artificial fluctuations - clean, stable power output
- **Configurable Parameters**: Adjustable base power, step size, and duration
- **Physiological HR Response**: Heart rate automatically adjusts to power changes

### Real-Time Visualization (Auto Mode)
- **ASCII Workout Profile**: 120-character wide visualization showing power zones over time
- **Live Progress Tracking**: Current position marker with segment information
- **Power Zone Display**: Visual representation of training zones (Recovery to VO2 Max)
- **Enhanced Legend**: Complete symbol guide for power intensities and workout phases

## Configuration Parameters

### Mode Selection
```cpp
const String TRAINING_MODE = "auto";          // "auto" or "manual" mode selection
```

### Core Training Parameters (Both Modes)
```cpp
const uint16_t FTP = 250;                     // Functional Threshold Power in watts
const uint8_t HR_BASE = 135;                  // Base heart rate at 50% FTP
const uint8_t HR_MAX = 170;                   // Max heart rate at 127% FTP
```

### Auto Mode Parameters
```cpp
const uint16_t CUSTOM_DURATION_MINUTES = 240; // Main interval duration in minutes
const float CUSTOM_TARGET_TSS = 350.0;        // Target Training Stress Score
const uint8_t MAX_CUSTOM_SEGMENTS = 30;       // Exact number of interval segments
```

### Manual Mode Parameters
```cpp
const uint16_t MANUAL_BASE_POWER = 200;       // Starting power in watts
const uint16_t MANUAL_POWER_STEP = 10;        // Power adjustment step in watts
const uint16_t MANUAL_DURATION_MINUTES = 60;  // Session duration in minutes

// Control type selection: "buttons" or "joystick"
const String MANUAL_CONTROL_TYPE = "joystick"; // Control method selection

// Button parameters (used when MANUAL_CONTROL_TYPE = "buttons")
const uint8_t BUTTON_POWER_DOWN = 13;         // GPIO pin for power decrease
const uint8_t BUTTON_POWER_UP = 15;           // GPIO pin for power increase
const uint16_t BUTTON_DEBOUNCE_MS = 200;      // Button debounce delay

// Joystick parameters (used when MANUAL_CONTROL_TYPE = "joystick")
const uint8_t JOYSTICK_PIN = 32;              // GPIO pin for joystick X-axis
const uint8_t JOYSTICK_VCC_PIN = 19;          // GPIO pin for joystick power
const uint8_t JOYSTICK_GND_PIN = 18;          // GPIO pin for joystick ground
const uint16_t JOYSTICK_THRESHOLD_LOW = 1500; // Lower threshold for power decrease
const uint16_t JOYSTICK_THRESHOLD_HIGH = 2500; // Upper threshold for power increase
```

## Parameter Effects

### FTP (Functional Threshold Power)
- **Base for all calculations**: All power targets are calculated as percentages of FTP
- **Heart rate scaling**: Determines HR response curve (50% FTP = HR_BASE, 127% FTP = HR_MAX)
- **Recommended range**: 150-400W depending on fitness level

### Auto Mode - Target TSS
- **Workout intensity**: Higher TSS = more time at higher intensities
- **Duration relationship**: TSS/hour typically ranges from 60 (easy) to 150+ (very hard)
- **Examples**:
  - TSS 200/4h = moderate endurance (IF ~0.63)
  - TSS 350/4h = hard training (IF ~0.76)
  - TSS 500/4h = very hard/racing (IF ~0.91)

### Manual Mode - Power Control
- **Real-time adjustment**: Instant power changes via button presses or joystick movement
- **Control Methods**:
  - **Buttons**: Press-release action for discrete power steps
  - **Joystick**: Deflect left/right and release for power decrease/increase
- **Safety limits**: Built-in minimum (50W) and maximum (500W) power constraints
- **Step size control**: Configurable power increments for fine or coarse adjustments
- **Press-release activation**: Prevents accidental multiple triggers during single input action

## Installation

1. **Arduino IDE Setup**:
   ```
   File → Preferences → Additional Board Manager URLs:
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

   Tools → Board → Boards Manager → Search "ESP32" → Install
   ```

2. **Required Libraries**:
   - ESP32 BLE Arduino (usually included with ESP32 board package)
   - No additional libraries needed

3. **Hardware Setup (Manual Mode Only)**:

   **Option A - Button Control:**
   - Connect momentary push buttons between D13/D15 and GND
   - Internal pull-up resistors are automatically enabled
   - No external resistors required

   **Option B - Joystick Control:**
   - Connect analog joystick to ESP32:
     - VCC → GPIO19 (ESP32 provides power)
     - GND → GPIO18 (ESP32 provides ground)
     - VRx (X-axis) → GPIO32 (analog input)
   - No external components required
   - Joystick automatically calibrates center position on startup

4. **Upload Process**:
   - Connect ESP32 via USB
   - Select board type and port in Arduino IDE
   - Configure TRAINING_MODE parameter
   - Compile and upload the sketch

## Usage

### Mode Selection
Choose operation mode by modifying the TRAINING_MODE parameter:
- `const String TRAINING_MODE = "auto";` - Structured workout mode
- `const String TRAINING_MODE = "manual";` - Interactive power control mode

### Initial Setup
1. Power on ESP32 and open Serial Monitor (115200 baud)
2. Device will display mode configuration and initialize accordingly
3. Look for "BLE emulator ready for Zwift connection!"

### Auto Mode Usage
1. Device generates custom workout based on configured parameters
2. Workout visualization displays in Serial Monitor
3. Training follows structured three-phase progression automatically

### Manual Mode Usage
1. Device starts at configured base power
2. Use hardware controls for real-time power adjustment:

   **Button Control:**
   - **D13 Button**: Decrease power by configured step amount
   - **D15 Button**: Increase power by configured step amount

   **Joystick Control:**
   - **Deflect Left & Release**: Decrease power by configured step amount
   - **Deflect Right & Release**: Increase power by configured step amount
   - Center position = no action (with built-in deadzone)

3. Power changes are logged to Serial Monitor
4. Heart rate adjusts automatically to power changes

### Zwift Pairing
1. Open Zwift and navigate to pairing screen
2. Look for "Kickr Core 5638" in all three categories:
   - Power Source
   - Cadence Source
   - Heart Rate Monitor
3. Pair all three services and start riding

### Workout Monitoring
- **Serial Output**: Real-time power, HR, cadence, and mode-specific information
- **Auto Mode**: ASCII graph updates every minute showing full workout profile
- **Manual Mode**: Power target and adjustment confirmations
- **Progress Tracking**: Current position, remaining time, and zone information

### Workout Lifecycle
- **Automatic Start**: Training begins immediately upon ESP32 boot/reset
- **Completion Behavior**: After session ends:
  - Power and cadence drop to 0 (simulates stopping pedaling)
  - Heart rate gradually decreases to 90 BPM over 5 minutes (realistic recovery)
  - System maintains final state indefinitely
- **New Session**: To start fresh, physically reset ESP32 (reset button or power cycle)
- **No Remote Restart**: No built-in mechanism to restart without hardware reset

## Mode Comparison

| Feature | Auto Mode | Manual Mode |
|---------|-----------|-------------|
| **Power Control** | Pre-programmed intervals | Real-time button/joystick control |
| **Control Options** | N/A | Buttons or analog joystick |
| **Workout Structure** | Warmup → Intervals → Cooldown | Continuous session |
| **Power Variation** | ±2% realistic fluctuation | Stable target power |
| **Visualization** | Full ASCII workout graph | Simple power/target display |
| **Heart Rate** | Follows workout progression | Responds to manual power |
| **Hardware Required** | ESP32 only | ESP32 + buttons OR joystick |
| **Best For** | Structured training plans | Interactive sessions, testing |

## Workout Structure (Auto Mode)

### Simplified Three-Phase Design

#### Phase 1: Warmup (10 minutes)
- Progressive warmup: 30% to 100% FTP
- Smooth linear progression
- Prepares body for main training block

#### Phase 2: Main Intervals (Configurable duration)
- **Exact segment count**: Always generates MAX_CUSTOM_SEGMENTS intervals
- **Intelligent intensity distribution**:
  - 30% recovery/endurance (40-75% FTP)
  - 40% tempo/threshold (75-105% FTP)
  - 20% threshold/VO2 (105-120% FTP)
  - 10% high VO2 (120-140% FTP, max)
- **Physiological constraints**:
  - Mandatory recovery after any >120% FTP interval
  - Prevents consecutive very high intensity efforts
  - Forces moderate intensity after 3+ easy intervals
- **Smooth connections**: Each segment flows seamlessly into the next

#### Phase 3: Cooldown (10 minutes)
- Structured progression: 60% to 30% FTP
- Gradual power reduction
- Ends with easy recovery spinning

## Power Visualization Legend (Auto Mode)

The ASCII workout visualization uses these symbols:
- `#` = VeryHigh (140%+ FTP) - very high intensity
- `H` = High (120%+ FTP) - high intensity
- `M` = ModHigh (90%+ FTP) - moderate-high
- `L` = Moderate (60%+ FTP) - moderate
- `.` = Low (less than 60% FTP) - low/recovery

Workout phases:
- `W` = Warmup
- `I` = Interval
- `C` = Cooldown

## Customization Examples

### Auto Mode Examples

#### Endurance Ride (TSS 180, 3 hours)
```cpp
const String TRAINING_MODE = "auto";
const uint16_t CUSTOM_DURATION_MINUTES = 180;
const float CUSTOM_TARGET_TSS = 180.0;
// Results in mostly Zone 2-3 riding with occasional tempo efforts
```

#### VO2 Max Session (TSS 120, 1.5 hours)
```cpp
const String TRAINING_MODE = "auto";
const uint16_t CUSTOM_DURATION_MINUTES = 90;
const float CUSTOM_TARGET_TSS = 120.0;
// Creates high-intensity interval structure
```

### Manual Mode Examples

#### Power Testing Session
```cpp
const String TRAINING_MODE = "manual";
const uint16_t MANUAL_BASE_POWER = 150;
const uint16_t MANUAL_POWER_STEP = 25;
const uint16_t MANUAL_DURATION_MINUTES = 45;
// Start low, increase by 25W steps for testing
```

#### Fine Power Control
```cpp
const String TRAINING_MODE = "manual";
const uint16_t MANUAL_BASE_POWER = 250;
const uint16_t MANUAL_POWER_STEP = 5;
const uint16_t MANUAL_DURATION_MINUTES = 90;
// Precise 5W adjustments around FTP
```

## Troubleshooting

### Connection Issues
- **Device not visible**: Check ESP32 power and Serial Monitor for startup messages
- **Pairing fails**: Restart Bluetooth on Zwift device, ensure close proximity
- **Disconnections**: Check power supply stability, avoid interference sources

### Auto Mode Issues
- **Unrealistic TSS**: Check if target TSS matches duration (TSS/hour should be 50-150)
- **Too easy/hard**: Adjust FTP setting to match your actual threshold power

### Manual Mode Issues
- **Buttons not responding**: Verify GPIO connections (D13, D15 to GND via momentary switches)
- **Multiple triggers**: Check debounce setting, ensure clean button contacts
- **Joystick not responding**:
  - Verify connections: VCC→GPIO19, GND→GPIO18, VRx→GPIO32
  - Check Serial Monitor for center calibration value (should be ~2000-2100)
  - Ensure joystick is centered during ESP32 startup
- **Joystick too sensitive/not sensitive**: Adjust JOYSTICK_THRESHOLD_LOW/HIGH values in code
- **Power limits**: Default range is 50-500W, modify source code if different limits needed

## Version History

### v0.2.46 (Current)
**Joystick Control Integration: Dual Input Methods for Manual Mode**

#### Major New Features:
- **Joystick Control Option**: Complete analog joystick support for manual power control
  - ESP32-powered joystick connection (no external power required)
  - Press-release action: deflect left/right and return to center for single power adjustment
  - Automatic center position calibration on startup
  - Built-in deadzone prevents accidental triggers near center position

- **Control Type Selection**: Choose between buttons or joystick for manual mode
  - Single parameter switch: `MANUAL_CONTROL_TYPE = "buttons"` or `"joystick"`
  - Automatic hardware initialization based on selected control type
  - Independent configuration parameters for each control method

#### Joystick Implementation Features:
- **Virtual Power Supply**: ESP32 provides both power (GPIO19) and ground (GPIO18) to joystick
- **Simple 3-Wire Connection**: VCC→GPIO19, GND→GPIO18, VRx→GPIO32
- **Press-Release Logic**: Similar to button behavior - deflect and release for single action
- **Configurable Thresholds**: Adjustable sensitivity via JOYSTICK_THRESHOLD_LOW/HIGH parameters
- **State Tracking**: Prevents continuous triggering, ensures single action per deflection cycle

#### Enhanced User Experience:
- **Plug-and-Play Setup**: No external components required for joystick operation
- **Automatic Calibration**: Center position detected and stored during startup
- **Clear Status Indication**: Serial Monitor shows control type and joystick calibration values
- **Troubleshooting Support**: Comprehensive setup and connection guidance

#### Technical Improvements:
- **Minimal Code Impact**: Joystick functionality added without affecting existing button or auto mode operation
- **Efficient Resource Usage**: Joystick variables only initialized when needed
- **Responsive Input Handling**: 50ms polling rate for smooth joystick response
- **Robust State Management**: Proper press-release detection prevents double-triggering

### v0.2.45 (Previous)
**Dual Mode Operation: Auto + Manual Control**

#### Major New Features:
- **Manual Mode Implementation**: Complete interactive power control system
  - Hardware button integration (D13/D15 for power decrease/increase)
  - Professional debounce handling with press-release activation
  - Configurable power step size and base power settings
  - Real-time power adjustment with immediate feedback

- **Mode Selection System**: Easy switching between auto and manual modes via single parameter
  - Automatic hardware initialization based on selected mode
  - Mode-specific visualization and output formatting
  - Independent parameter sets for each operation mode

#### Manual Mode Features:
- **Stable Power Output**: No artificial ±2% fluctuations for clean, consistent power delivery
- **Safety Constraints**: Built-in minimum (50W) and maximum (500W) power limits
- **Intelligent Button Handling**:
  - 200ms debounce protection prevents accidental multiple triggers
  - Press-release activation ensures single action per button press
  - Continuous monitoring in main loop for responsive control
- **Physiological Integration**: Heart rate automatically adjusts to manual power changes using same algorithm as auto mode

#### Technical Improvements:
- **Minimal Code Changes**: Added manual functionality without disrupting existing auto mode operation
- **Shared Core Functions**: Both modes use identical BLE services, cadence simulation, and heart rate calculation
- **Efficient Resource Usage**: Manual mode variables only active when needed
- **Preserved Visualization**: Auto mode retains all existing ASCII workout graphs and progress tracking

#### Enhanced User Experience:
- **Mode-Specific Configuration Display**: Clear indication of active mode and relevant parameters
- **Simplified Manual Setup**: Single-segment workout structure eliminates complex interval logic
- **Real-Time Feedback**: Immediate Serial Monitor confirmation of power adjustments
- **Consistent Cadence/HR**: Both modes maintain identical cadence cycling (90-98 RPM) and physiological HR response

### v0.2.35 (Previous)
**Simplified and Enhanced Workout Generation**

#### Major Simplifications:
- **Removed complex W'bal calculations**: Eliminated anaerobic work capacity modeling that caused generation failures
- **Simplified parameter set**: Reduced from 12+ parameters to 5 core settings
- **Fixed three-phase structure**: Standardized warmup (10min) and cooldown (10min) phases
- **Exact segment control**: MAX_CUSTOM_SEGMENTS now specifies exact number of intervals (not maximum)

#### New Features:
- **Realistic power fluctuation**: Added ±2% random variation to simulate natural pedaling imperfections
- **Enhanced visualization legend**: Complete symbol guide for power intensities and workout phases
- **Smooth segment transitions**: Each interval connects seamlessly to the next for continuous power flow
- **Intelligent zone distribution**: Prevents physiologically impossible sequences (e.g., 4+ consecutive >140% FTP intervals)

#### Improved Physiological Modeling:
- **Strict high-intensity limits**: Mandatory recovery after any >120% FTP effort
- **Realistic intensity distribution**: Weighted toward sustainable power zones for long workouts
- **Consecutive interval prevention**: No more than 2 high-intensity or 3 low-intensity intervals in sequence
- **Power ceiling**: Maximum 140% FTP to prevent unrealistic efforts

#### Technical Improvements:
- **Reliable generation**: Simplified algorithm eliminates workout generation failures
- **Better TSS accuracy**: Improved normalization maintains target within ±5%
- **Enhanced debugging**: Clearer output shows zone distribution and physiological constraints
- **Memory optimization**: Reduced dynamic allocation complexity

### v0.2.32 (Previous)
**Complex W'bal-Constrained System**

#### Features:
- Advanced W'bal anaerobic capacity modeling
- Variable segment structure with physiological constraints
- Complex parameter set with recovery constants
- Multi-attempt generation algorithm

#### Issues Addressed in v0.2.35:
- Generation failures due to over-constrained W'bal system
- Unrealistic interval sequences (4+ consecutive very high intensity)
- Complex parameter tuning requirements
- Inconsistent workout generation success

### v0.1.09 (Original)
**Simple BLE Emulator**

#### Features:
- Basic BLE services (Power, Cadence, Heart Rate)
- Fixed cycling ranges:
  - Power: 200-220W
  - Cadence: 90-98 RPM
  - Heart Rate: 135-155 BPM
- Simple value oscillation
- Basic Zwift compatibility

## License

This project is released under the MIT License.

## Health and Safety Notice

This emulator is designed for training simulation and testing purposes. Always:
- Consult with qualified fitness professionals for training program design
- Listen to your body and adjust intensity based on perceived exertion
- Ensure proper bike fit and safety equipment when training
- Stay hydrated and maintain appropriate training environment
- Seek medical advice before beginning any new exercise program

The generated workouts are mathematical models and may not suit all fitness levels or health conditions.
