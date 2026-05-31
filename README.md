# Xbox-to-Switch BLE Controller Adapter

This is a simple hobby project that turns an Arduino Nano 33 BLE into a USB adapter, allowing you to use a wireless Xbox controller on a Nintendo Switch. 

It uses Mbed OS native Bluetooth to pair with the Xbox controller and emulates a wired Switch Pro Controller over the USB port.

---

## 🎮 How It Works

The Arduino acts as a bridge between the Bluetooth connection and the Switch's USB port:

1. **Bluetooth**: The Arduino scans for a nearby Xbox controller (filtering by MAC address prefix or name), pairs with it, and subscribes to its input reports.
2. **Mapping**: It parses the Xbox buttons and analog sticks, translating them to match the Switch layout (swapping A/B and X/Y buttons).
3. **USB Emulation**: It registers as a wired Switch Pro Controller, responding to standard USB handshakes, player LED setup commands, and mock calibration data queries.
4. **Rumble**: It intercepts rumble packets sent by the Switch, scales the haptic magnitudes, and writes them to the Xbox controller's vibration motors.

---

## 📂 File Walkthrough

* **`xbox-to-switch.ino`**: The main sketch. It initializes the BLE stack and runs the fast-polling update loop.
* **`XboxBLE.h`**: Handles Bluetooth scanning, secure pairing (using Security Manager), parsing Xbox inputs, and writing haptic feedback.
* **`SwitchUSB.h`**: Emulates the Pro Controller's USB interface (VID 0x057E, PID 0x2009), mimics factory SPI calibration values, and parses incoming rumble data.

---

## ⚙️ Setup & Flashing

### Requirements
* Arduino Nano 33 BLE or BLE Sense.
* Micro-USB cable (must support data transfer, not just power).
* A Bluetooth-enabled Xbox Wireless Controller (updated to latest firmware).

### 1. Arduino IDE Setup
1. In the Arduino IDE, go to **Tools > Board > Boards Manager...**
2. Search for **"Mbed OS Nano"** and install the **"Arduino Mbed OS Nano Boards"** core by Arduino.

### 2. Flashing
1. Connect the Arduino to your computer.
2. Open **`xbox-to-switch.ino`**.
3. Under **Tools > Board**, select **Arduino Nano 33 BLE**. Select the correct COM port under **Tools > Port**.
4. Click **Upload**. The onboard LED will pulse when flashing is complete.

---

## ⚠️ Recovery Mode

Because the board spoofs a USB controller, the default serial COM port may sometimes disappear from your PC during emulation. 

If this happens and you cannot upload a new sketch:
1. Double-press the physical **Reset Button** on the Arduino board.
2. The onboard yellow LED will begin pulsing slowly, indicating it is in hardware **Bootloader Mode**.
3. Select the newly appeared COM port in the Arduino IDE.
4. Upload a basic sketch (like **File > Examples > 01.Basics > Blink**) to clean the USB port and restore standard serial behavior.

---

## 🔧 Protocol Reference

### Handshake Subcommands
The adapter intercepts and responds to the following standard USB HID (`0x80` frame) command sequences:

| Command Byte | Action | Adapter Behavior |
| :---: | :--- | :--- |
| `80 01` | USB Handshake Request | Returns status packet with a mock Pro Controller MAC Address. |
| `80 02` | UART Init | Establishes communication. Acknowledged with an `81 02` frame. |
| `80 03` | Baud Rate Switch | Swaps speed to 3Mbit. Acknowledged with an `81 03` frame. |
| `80 04` | Force USB Mode | Stops BLE timeouts on the Switch side. |
| `80 05` | Allow Bluetooth Fallback | Resets connection timers. |

### Rumble Scaling
The Switch sends vibration instructions as 10-byte packets containing frequency and amplitude bands. Because the Xbox grip motors are strong, the haptic intensities are scaled:
* **Intensity Scale**: Grip motor strength is capped at `4%` by default (adjustable via `#define XBOX_RUMBLE_INTENSITY_SCALE` in `XboxBLE.h`).
* **Deadzone**: Any combined strength value below `2%` is forced to `0%` to ensure instant rumble cutoffs.
