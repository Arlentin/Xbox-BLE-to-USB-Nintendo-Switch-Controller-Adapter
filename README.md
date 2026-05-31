# Xbox-to-Switch BLE Controller Adapter

*Pure Native Mbed OS BLE & Spoofed USB-HID Pro Controller for Arduino Nano 33 BLE / BLE Sense*

---

This is a high-performance, native hardware adapter that allows you to use an **Xbox Wireless Controller (Bluetooth)** on a **Nintendo Switch** console over a wired USB connection with zero perceptible latency. 

Unlike general-purpose libraries, this project bypasses bulky third-party wrappers and utilizes the official **Mbed OS Native BLE Stack** running on the Arduino Nano 33 BLE. It emulates a physical **Nintendo Switch Pro Controller** by spoofing USB Vendor/Product IDs (VID/PID), overriding product string descriptors, and intercepting complex low-level USB-HID subcommands (such as SPI flash memory reads and calibration profiles).

---

## 🎮 How It Works (Architecture)

The Arduino Nano 33 BLE acts as a bidirectional bridge between Bluetooth Low Energy (BLE) and physical USB-HID:

```
┌────────────────────────┐         ┌────────────────────────┐         ┌────────────────────────┐
│ Xbox Wireless Gamepad  │ ──BLE──>│ Arduino Nano 33 BLE    │ ──USB──>│ Nintendo Switch        │
│ (GATT HID Server)      │         │ (BLE Central + USB-HID)│         │ (USB Host Controller)  │
└────────────────────────┘         └────────────────────────┘         └────────────────────────┘
```

1. **Bluetooth Low Energy (Central Mode)**: The Arduino automatically scans for nearby BLE advertisements, matches the Xbox MAC Address prefix or device name, establishes a secure pairing link, and subscribes to the gamepad's live input notifications.
2. **Protocol Translation**: The Arduino parses raw Xbox GATT input reports (analog sticks, button groups, triggers) and converts them into the precise 12-bit packed binary layout required by the Nintendo Pro Controller protocol.
3. **USB Gamepad Spoofing (USB-HID Device)**: The Arduino registers as a full USB PluggableHID device. It responds to the Switch console's initial USB handshake packets, mocks a factory-calibrated SPI memory bank (color profile, sticks, and IMU sensor data), and sends gamepad updates at a lightning-fast rate.
4. **HD Rumble Translation**: The Switch sends high-frequency (HF) and low-frequency (LF) HD Rumble commands over USB. The adapter extracts these amplitudes, filters out deadzones, scales the intensities, and transmits throttled BLE haptic packets back to the Xbox controller's vibration motors.

---

## 📂 Codebase & File Walkthrough

The project is structured with focused, high-performance files located in the root directory:

### 1. `proud-heisenberg.ino`
The main entry point of the firmware.
* **Instant Boot**: Boots instantly upon power-up, initiating Bluetooth scanning and USB gamepad emulation with zero latency or delay.
* **Mbed EventQueue**: Instantiates an `events::EventQueue` to run asynchronous Bluetooth events on the Mbed OS scheduler rather than blocking the main `loop()`.
* **State Machine Dispatcher**: Directs the `setup()` logic and loops the fast-polling `SwitchUSB.process()` interface and haptic runners.

### 2. [`XboxBLE.h`](file:///Users/antonlundberg/Documents/antigravity/proud-heisenberg/XboxBLE.h)
Handles the central Bluetooth radio stack and input parser.
* **Native GATT Scanner**: Listens for advertisements and filters peers using the Microsoft OUI MAC prefix (`44:16:22`) or an case-insensitive `"xbox"` string match.
* **Link Encrypter**: Invokes Mbed's `SecurityManager` to perform a secure encryption handshake ("Just Works" pairing). This is required before the Xbox controller will accept characteristic descriptor reads.
* **Descriptor Discoverer**: Maps the HID service (`0x1812`) and locates the CCCD descriptors (`0x2902`) to enable data subscriptions.
* **Input Parser**: Maps Xbox 16-bit analog joysticks and button bitfields into Switch button layouts, shifting the layout to match the correct **A/B** and **X/Y** orientation.
* **Throttled Rumble Emitter**: Gathers incoming rumble values and sends BLE write requests to the controller. Features a strict **15ms rate limiter** to avoid buffer overflows while keeping haptic latency completely imperceptible.

### 3. [`SwitchUSB.h`](file:///Users/antonlundberg/Documents/antigravity/proud-heisenberg/SwitchUSB.h)
Directs physical USB spoofing and mimics the Pro Controller's firmware.
* **Descriptor Overrides**: Spoofs Nintendo Co., Ltd. (`VID: 0x057E`) and the Switch Pro Controller (`PID: 0x2009`). Houses the custom 203-byte HID report descriptor.
* **Calibration Mocking**: Emulates the Pro Controller's internal SPI flash storage. When the Switch reads SPI calibration offsets, the adapter returns authentic factory values (IMU parameters, grey/white color details, joystick ranges).
* **Handshake Command Decoder**: Decodes USB subcommand frames starting with `0x01` (Pairing), `0x02` (Device Info), `0x40` (IMU toggling), `0x48` (Haptics configuration), and `0x30` (Player LEDs).
* **HD Rumble Parser**: decodes incoming 10-byte haptic report buffers. Combines high/low frequency amplitudes, applies a custom deadzone, and passes them to the BLE module.

### 4. [`USB-HID-Notes.md`](file:///Users/antonlundberg/Documents/antigravity/proud-heisenberg/USB-HID-Notes.md)
Contains structural research notes on the Nintendo Pro Controller protocol, including:
* Custom UART/USB command identifiers (`0x80` packets).
* Handshake procedures and baud rate switches (3Mbit latency modes).
* Host timeout parameters.

### 5. [`rumble_data_table.md`](file:///Users/antonlundberg/Documents/antigravity/proud-heisenberg/rumble_data_table.md)
A technical reference sheet detailing Joy-Con/Pro Controller linear resonant actuator (LRA) frequency and amplitude formulas, log2 conversion algorithms, byte-swapping procedures, and safe magnitude tables.

---

## ⚙️ Setup & Installation

Follow these steps to compile and flash the adapter:

### Prerequisites
* **Hardware**: Arduino Nano 33 BLE or Arduino Nano 33 BLE Sense.
* **Cable**: A high-quality micro-USB cable capable of both **power and data** (some cheap cables are power-only).
* **Controller**: A Bluetooth-enabled Xbox Wireless Controller (Xbox One S/X, Series S/X, or Elite Series 2) updated to the latest official Microsoft firmware.

### 1. Configure the Arduino IDE
1. Open the **Arduino IDE** (v2.x recommended).
2. Navigate to **Tools > Board > Boards Manager...**
3. Search for **"Mbed OS Nano"** and install the **"Arduino Mbed OS Nano Boards"** core by Arduino.
4. Close the Boards Manager.

### 2. Compile and Upload
1. Connect your Arduino Nano 33 BLE to your computer's USB port.
2. Select **proud-heisenberg.ino** in the IDE.
3. Under **Tools > Board > Arduino Mbed OS Nano Boards**, select **Arduino Nano 33 BLE**.
4. Select the corresponding serial port under **Tools > Port**.
5. Click the **Upload** button (arrow icon in the toolbar).
6. Wait for compilation and flashing to complete. The built-in LED will begin flashing in rapid cycles once the bootloader hands off execution.

---

## ⚠️ Safe Recovery Mode (Crucial)

The Arduino Nano 33 BLE utilizes virtual USB-HID descriptors to communicate with the Switch. Because the same physical port is shared between serial programming and controller emulation, a software freeze could cause the virtual serial port to disappear from your PC.

**Do not panic.** If the virtual serial port completely disappears from your computer because the USB Pro Controller gamepad emulation occupies the interface:

### Double-Tap Reset (Hardware Safeguard)
If you need to re-upload a sketch or recover factory behaviors:
1. Double-press the physical **Reset Button** on the Arduino board in quick succession.
2. The onboard yellow LED will begin pulsing slowly. This indicates the board has entered its hardware **Bootloader Mode**.
3. In the Arduino IDE, select the new COM/Serial port that appears (it will be listed as a bootloader port).
4. Upload a basic sketch (e.g., **File > Examples > 01.Basics > Blink**) to wipe the custom USB descriptors and restore factory-default USB serial behaviors.

---

## 🔧 Technical Protocol Reference

### USB Handshake Subcommands
The adapter intercepts and responds to the following proprietary commands issued by the Nintendo Switch console over USB HID (`0x80` protocol frames):

| Command Byte | Action | Adapter Response Behavior |
| :---: | :--- | :--- |
| `80 01` | USB Handshake Request | Returns status packet containing mock Pro Controller MAC Address in reverse order. |
| `80 02` | UART Initialization | Establishes the virtual serial link. Acknowledged with generic `81 02` frame. |
| `80 03` | Baud Rate Negotiation | Swaps host communication speed to 3Mbit. Acknowledged with generic `81 03` frame. |
| `80 04` | Force USB Only Mode | Instructs the controller to suspend BLE timeouts and communicate exclusively over USB HID. |
| `80 05` | Allow Bluetooth Fallback | Resets connection timers, permitting controller to return to Bluetooth scanning mode. |

### Rumble/Haptics Translation Logic
* **Intensity Scaling**: Grip motor values are mapped to an intensity ceiling via a customizable `#define XBOX_RUMBLE_INTENSITY_SCALE` (configured to `4%` by default for high-comfort tactile feedback).
* **Deadzones**: High-Frequency and Low-Frequency amplitudes are merged using a maximum-value check (`max(left, right)`) and passed through a small deadzone filter ($intensity \le 2\% \rightarrow 0$) to guarantee immediate, crisp vibration cutoff.
