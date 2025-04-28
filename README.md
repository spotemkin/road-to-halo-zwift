# ESP32 BLE Emulator for Zwift

A Bluetooth Low Energy (BLE) emulator for Zwift running on ESP32, simulating a smart trainer and heart rate monitor.

## Features

- Emulates a complete smart trainer ("Kickr Core 5638") with:
  - Cycling Power Service (200-220W range)
  - Cycling Speed and Cadence Service (90-98 RPM)
  - Heart Rate Service (135-155 BPM)
- All services work simultaneously from a single ESP32 device
- Smooth, gradual changes in values for realistic simulation
- Serial monitoring for debugging

## Requirements

- ESP32 development board (ESP32-WROOM, ESP32-DevKitC, etc.)
- Arduino IDE with ESP32 support installed
- BLE-capable device running Zwift (smartphone, tablet, computer)

## Installation

1. Install the Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Add ESP32 board support in Arduino IDE:

   - Go to File → Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to "Additional Board Manager URLs"
   - Go to Tools → Board → Boards Manager
   - Search for "ESP32" and install

3. Install required libraries:

   - Go to Sketch → Include Library → Manage Libraries
   - Search for and install:
     - "BLE" by Neil Kolban

4. Clone this repository or download the main sketch file.
5. Open the sketch in Arduino IDE.
6. Select your ESP32 board from Tools → Board menu.
7. Connect your ESP32 and select the correct port.
8. Upload the sketch to your ESP32.

## Usage

1. After uploading the code, open the Serial Monitor at 115200 baud to see debug information.
2. Open Zwift on your device and go to pair devices screen.
3. The emulator will appear as "Kickr Core 5638" for all three sensors:
   - Power source
   - Cadence source
   - Heart rate monitor
4. Pair with all three services and start your Zwift session.
5. The emulator will provide cycling power, cadence, and heart rate data with realistic variations.

## How It Works

The emulator creates a BLE server with three standard services:

- Cycling Power Service (UUID: 1818)
- Cycling Speed and Cadence Service (UUID: 1816)
- Heart Rate Service (UUID: 180D)

Each service has the appropriate characteristic that sends notifications with simulated data:

- Power cycles between 200-220 watts
- Cadence cycles between 90-98 RPM
- Heart rate cycles between 135-155 BPM

The data format follows the official BLE GATT specifications for each service.

## Customization

You can customize the power, cadence, and heart rate ranges by modifying these variables:

- For power: Change the initial `power` value and the range checks in `updateValues()`
- For cadence: Change the initial `cadence` value and the range checks in `updateValues()`
- For heart rate: Change the initial `heartRate` value and the range checks in `updateValues()`

## Troubleshooting

- **Device not showing up in Zwift**: Make sure your ESP32 is powered and the code is running (check Serial Monitor). Try restarting Bluetooth on your Zwift device.
- **Connection issues**: Ensure your ESP32 is close to your Zwift device. BLE has limited range.
- **Erratic values**: Check the Serial Monitor output to ensure values are changing normally on the ESP32 side.
- **Compilation errors**: Make sure you've installed the correct board support package and libraries.

## License

This project is released under the MIT License. See the LICENSE file for details.

## Credits

- Based on the ESP32 BLE examples
- Developed for use with Zwift virtual cycling platform
