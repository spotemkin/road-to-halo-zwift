# ESP32 BLE Cycling Emulator for Zwift v0.2.35

An advanced Bluetooth Low Energy (BLE) emulator for Zwift running on ESP32, featuring intelligent workout generation with physiological constraints and real-time visualization.

![Road to Halo Screenshot](rth_01.png)

## Features

### Core Functionality
- **Complete Smart Trainer Emulation**: Simulates "Kickr Core 5638" with all three services:
  - Cycling Power Service (dynamic workout-based power with realistic fluctuation)
  - Cycling Speed and Cadence Service (90-98 RPM with realistic behavior)
  - Heart Rate Service (physiologically accurate HR based on power output)

### Simplified Workout Generation
- **TSS-Targeted Training**: Generates custom workouts to hit specific Training Stress Score targets
- **Three-Phase Structure**:
  - Warmup: 30% to 100% FTP over 10 minutes
  - Main intervals: Configurable duration with intelligent intensity distribution
  - Cooldown: 60% to 30% FTP over 10 minutes
- **Smart Zone Distribution**: Prevents physiologically impossible interval sequences
- **Realistic Power Variation**: ±2% power fluctuation simulates natural pedaling

### Real-Time Visualization
- **ASCII Workout Profile**: 120-character wide visualization showing power zones over time
- **Live Progress Tracking**: Current position marker with segment information
- **Power Zone Display**: Visual representation of training zones (Recovery to VO2 Max)
- **Enhanced Legend**: Complete symbol guide for power intensities and workout phases

## Configuration Parameters

### Core Training Parameters (Simplified)
```cpp
const uint16_t FTP = 250;                     // Functional Threshold Power in watts
const uint8_t HR_BASE = 135;                  // Base heart rate at 50% FTP
const uint8_t HR_MAX = 170;                   // Max heart rate at 127% FTP
const uint16_t CUSTOM_DURATION_MINUTES = 240; // Main interval duration in minutes
const float CUSTOM_TARGET_TSS = 350.0;        // Target Training Stress Score
const uint8_t MAX_CUSTOM_SEGMENTS = 30;       // Exact number of interval segments
```

## Parameter Effects

### FTP (Functional Threshold Power)
- **Base for all calculations**: All power targets are calculated as percentages of FTP
- **Heart rate scaling**: Determines HR response curve (50% FTP = HR_BASE, 127% FTP = HR_MAX)
- **Recommended range**: 150-400W depending on fitness level

### Target TSS
- **Workout intensity**: Higher TSS = more time at higher intensities
- **Duration relationship**: TSS/hour typically ranges from 60 (easy) to 150+ (very hard)
- **Examples**:
  - TSS 200/4h = moderate endurance (IF ~0.63)
  - TSS 350/4h = hard training (IF ~0.76)
  - TSS 500/4h = very hard/racing (IF ~0.91)

### Interval Segments
- **Fixed quantity**: Exactly MAX_CUSTOM_SEGMENTS intervals generated
- **Equal duration**: Main interval time divided evenly among segments
- **Smooth transitions**: Each segment connects seamlessly to the next

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

3. **Upload Process**:
   - Connect ESP32 via USB
   - Select board type and port in Arduino IDE
   - Compile and upload the sketch

## Usage

### Initial Setup
1. Power on ESP32 and open Serial Monitor (115200 baud)
2. Device will generate custom workout and display configuration
3. Look for "BLE emulator ready for Zwift connection!"

### Zwift Pairing
1. Open Zwift and navigate to pairing screen
2. Look for "Kickr Core 5638" in all three categories:
   - Power Source
   - Cadence Source
   - Heart Rate Monitor
3. Pair all three services and start riding

### Workout Monitoring
- **Serial Output**: Real-time power, HR, cadence, and segment information
- **Workout Visualization**: ASCII graph updates every minute showing full workout profile
- **Progress Tracking**: Current position, remaining time, and zone information

### Workout Lifecycle
- **Automatic Start**: Workout begins immediately upon ESP32 boot/reset
- **Completion Behavior**: After workout ends:
  - Power and cadence drop to 0 (simulates stopping pedaling)
  - Heart rate gradually decreases to 90 BPM over 5 minutes (realistic recovery)
  - System maintains final state indefinitely
- **New Workout**: To start fresh workout, physically reset ESP32 (reset button or power cycle)
- **No Remote Restart**: No built-in mechanism to restart workout without hardware reset

## Workout Structure

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

## Power Visualization Legend

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

### Endurance Ride (TSS 180, 3 hours)
```cpp
const uint16_t CUSTOM_DURATION_MINUTES = 180;
const float CUSTOM_TARGET_TSS = 180.0;
// Results in mostly Zone 2-3 riding with occasional tempo efforts
```

### VO2 Max Session (TSS 120, 1.5 hours)
```cpp
const uint16_t CUSTOM_DURATION_MINUTES = 90;
const float CUSTOM_TARGET_TSS = 120.0;
// Creates high-intensity interval structure
```

### Race Simulation (TSS 280, 2 hours)
```cpp
const uint16_t CUSTOM_DURATION_MINUTES = 120;
const float CUSTOM_TARGET_TSS = 280.0;
// Generates race-like variable power with high average
```

## Troubleshooting

### Connection Issues
- **Device not visible**: Check ESP32 power and Serial Monitor for startup messages
- **Pairing fails**: Restart Bluetooth on Zwift device, ensure close proximity
- **Disconnections**: Check power supply stability, avoid interference sources

### Workout Issues
- **Unrealistic TSS**: Check if target TSS matches duration (TSS/hour should be 50-150)
- **Too easy/hard**: Adjust FTP setting to match your actual threshold power

## Version History

### v0.2.35 (Current)
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
