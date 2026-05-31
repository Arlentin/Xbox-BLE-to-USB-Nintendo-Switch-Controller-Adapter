#ifndef SWITCH_USB_H
#define SWITCH_USB_H

#include <Arduino.h>
#include "PluggableUSBHID.h"
#include <string.h>

// Debug mode flag (defined in xbox-to-switch.ino)
extern bool debug;

// Debug print macros
#define DEBUG_PRINT(...) do { if (debug) Serial.print(__VA_ARGS__); } while (0)
#define DEBUG_PRINTLN(...) do { if (debug) Serial.println(__VA_ARGS__); } while (0)

// Pro Controller button bitmasks (for packed SwitchReport buttons array)
#define SWITCH_MASK_ZR      (1U << 7)
#define SWITCH_MASK_R       (1U << 6)
#define SWITCH_MASK_A       (1U << 3)
#define SWITCH_MASK_B       (1U << 2)
#define SWITCH_MASK_X       (1U << 1)
#define SWITCH_MASK_Y       (1U << 0)

#define SWITCH_MASK_CAPTURE (1U << 5)
#define SWITCH_MASK_HOME    (1U << 4)
#define SWITCH_MASK_L3      (1U << 3)
#define SWITCH_MASK_R3      (1U << 2)
#define SWITCH_MASK_PLUS    (1U << 1)
#define SWITCH_MASK_MINUS   (1U << 0)

#define SWITCH_MASK_ZL      (1U << 7)
#define SWITCH_MASK_L       (1U << 6)

// Button definitions mapping to the original Pokken report descriptor bitmask
// Keep these exactly identical to prevent compilation failures in XboxBLE.h!
enum SwitchButtons {
    SWITCH_Y       = (1 << 0),
    SWITCH_B       = (1 << 1),
    SWITCH_A       = (1 << 2),
    SWITCH_X       = (1 << 3),
    SWITCH_L       = (1 << 4),
    SWITCH_R       = (1 << 5),
    SWITCH_ZL      = (1 << 6),
    SWITCH_ZR      = (1 << 7),
    SWITCH_MINUS   = (1 << 8),
    SWITCH_PLUS    = (1 << 9),
    SWITCH_LCLICK  = (1 << 10),
    SWITCH_RCLICK  = (1 << 11),
    SWITCH_HOME    = (1 << 12),
    SWITCH_CAPTURE = (1 << 13)
};

class SwitchUSB : public USBHID {
public:
    // Vendor ID: 0x057E (Nintendo Co., Ltd.)
    // Product ID: 0x2009 (Switch Pro Controller)
    // Product Release: 0x0210 (bcdDevice version 2.10)
    // Report size: 64 bytes input, 64 bytes output
    SwitchUSB() : USBHID(true, 64, 64, 0x057E, 0x2009, 0x0210) {
        // Safe mock MAC address matching Nintendo's registration block
        const uint8_t mockMac[] = {0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03};
        memcpy(_addr, mockMac, 6);
        resetState();
    }

    void resetState() {
        current_buttons = 0x0000;
        current_hat = 8; // Released
        current_lsx = 128;
        current_lsy = 128;
        current_rsx = 128;
        current_rsy = 128;

        vibration_enabled = false;
        vibration_report = 0x00;
        vibration_idx = 0;
        imu_enabled = false;
        player_number = 1;
        _timestamp = 0;
        _timer = 0;
    }

    // Setters called by XboxBLE.h
    void setButtons(uint16_t buttons) {
        current_buttons = buttons;
    }

    void setButton(SwitchButtons btn, bool pressed) {
        if (pressed) {
            current_buttons |= btn;
        } else {
            current_buttons &= ~btn;
        }
    }

    void setHat(uint8_t hat) {
        current_hat = hat;
    }

    void setLeftStick(uint8_t x, uint8_t y) {
        current_lsx = x;
        current_lsy = y;
    }

    void setRightStick(uint8_t x, uint8_t y) {
        current_rsx = x;
        current_rsy = y;
    }

    // Sends a standard full input report (0x30) to the Switch
    bool sendState() {
        HID_REPORT out;
        out.length = 64;
        memset(out.data, 0, 64);
        
        out.data[0] = 0x30; // Report ID 0x30 (Full report)
        out.data[1] = getTimer();
        packSwitchReport(&out.data[2]);
        out.data[12] = vibration_report;
        
        if (imu_enabled) {
            static const uint8_t imu_data[36] = {
                0x75, 0xFD, 0xFD, 0xFF, 0x09, 0x10, 0x21, 0x00, 0xD5,
                0xFF, 0xE0, 0xFF, 0x72, 0xFD, 0xF9, 0xFF, 0x0A, 0x10,
                0x22, 0x00, 0xD5, 0xFF, 0xE0, 0xFF, 0x76, 0xFD, 0xFC,
                0xFF, 0x09, 0x10, 0x23, 0x00, 0xD5, 0xFF, 0xE0, 0xFF
            };
            memcpy(&out.data[13], imu_data, 36);
        }
        
        return send_nb(&out);
    }

    // Background parser polled from xbox-to-switch.ino
    void process() {
        HID_REPORT recv;
        if (read_nb(&recv)) {
            handleOutputReport(recv.data, recv.length);
        }
    }

protected:
    // Custom USB Product String Descriptor to spoof Nintendo Switch Pro Controller
    virtual const uint8_t *string_iproduct_desc() override {
        static const uint8_t string[] = {
            30,             // bLength
            0x03,           // bDescriptorType (STRING)
            'P', 0, 'r', 0, 'o', 0, ' ', 0, 'C', 0, 'o', 0, 'n', 0, 't', 0, 'r', 0, 'o', 0, 'l', 0, 'l', 0, 'e', 0, 'r', 0
        };
        return string;
    }

    // Override report descriptor to be the official 203-byte Pro Controller descriptor
    virtual const uint8_t* report_desc() override {
        static const uint8_t desc[] = {
            0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
            0x15, 0x00,  // Logical Minimum (0)
            0x09, 0x04,  // Usage (Joystick)
            0xA1, 0x01,  // Collection (Application)

            0x85, 0x30,  //   Report ID (48 / 0x30)
            0x05, 0x01,  //   Usage Page (Generic Desktop Ctrls)
            0x05, 0x09,  //   Usage Page (Button)
            0x19, 0x01,  //   Usage Minimum (0x01)
            0x29, 0x0A,  //   Usage Maximum (0x0A)
            0x15, 0x00,  //   Logical Minimum (0)
            0x25, 0x01,  //   Logical Maximum (1)
            0x75, 0x01,  //   Report Size (1)
            0x95, 0x0A,  //   Report Count (10)
            0x55, 0x00,  //   Unit Exponent (0)
            0x65, 0x00,  //   Unit (None)
            0x81, 0x02,  //   Input (Data,Var,Abs)
            
            0x05, 0x09,  //   Usage Page (Button)
            0x19, 0x0B,  //   Usage Minimum (0x0B)
            0x29, 0x0E,  //   Usage Maximum (0x0E)
            0x15, 0x00,  //   Logical Minimum (0)
            0x25, 0x01,  //   Logical Maximum (1)
            0x75, 0x01,  //   Report Size (1)
            0x95, 0x04,  //   Report Count (4)
            0x81, 0x02,  //   Input (Data,Var,Abs)
            
            0x75, 0x01,  //   Report Size (1)
            0x95, 0x02,  //   Report Count (2)
            0x81, 0x03,  //   Input (Const,Var,Abs)
            
            0x0B, 0x01, 0x00, 0x01, 0x00,  //   Usage (0x010001)
            0xA1, 0x00,                    //   Collection (Physical)
            0x0B, 0x30, 0x00, 0x01, 0x00,  //     Usage (0x010030)
            0x0B, 0x31, 0x00, 0x01, 0x00,  //     Usage (0x010031)
            0x0B, 0x32, 0x00, 0x01, 0x00,  //     Usage (0x010032)
            0x0B, 0x35, 0x00, 0x01, 0x00,  //     Usage (0x010035)
            0x15, 0x00,                    //     Logical Minimum (0)
            0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
            0x75, 0x10,                    //     Report Size (16)
            0x95, 0x04,                    //     Report Count (4)
            0x81, 0x02,                    //     Input (Data,Var,Abs)
            0xC0,                          //   End Collection
            
            0x0B, 0x39, 0x00, 0x01, 0x00,  //   Usage (0x010039)
            0x15, 0x00,                    //   Logical Minimum (0)
            0x25, 0x07,                    //   Logical Maximum (7)
            0x35, 0x00,                    //   Physical Minimum (0)
            0x46, 0x3B, 0x01,              //   Physical Maximum (315)
            0x65, 0x14,                    //   Unit (English Rotation)
            0x75, 0x04,                    //   Report Size (4)
            0x95, 0x01,                    //   Report Count (1)
            0x81, 0x02,                    //   Input (Data,Var,Abs)
            
            0x05, 0x09,  //   Usage Page (Button)
            0x19, 0x0F,  //   Usage Minimum (0x0F)
            0x29, 0x12,  //   Usage Maximum (0x12)
            0x15, 0x00,  //   Logical Minimum (0)
            0x25, 0x01,  //   Logical Maximum (1)
            0x75, 0x01,  //   Report Size (1)
            0x95, 0x04,  //   Report Count (4)
            0x81, 0x02,  //   Input (Data,Var,Abs)
            
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x34,  //   Report Count (52)
            0x81, 0x03,  //   Input (Const,Var,Abs)
            
            0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
            0x85, 0x21,        //   Report ID (33 / 0x21)
            0x09, 0x01,        //   Usage (0x01)
            0x75, 0x08,        //   Report Size (8)
            0x95, 0x3F,        //   Report Count (63)
            0x81, 0x03,        //   Input (Const,Var,Abs)

            0x85, 0x81,  //   Report ID (-127 / 0x81)
            0x09, 0x02,  //   Usage (0x02)
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x3F,  //   Report Count (63)
            0x81, 0x03,  //   Input (Const,Var,Abs)

            0x85, 0x01,  //   Report ID (1 / 0x01)
            0x09, 0x03,  //   Usage (0x03)
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x3F,  //   Report Count (63)
            0x91, 0x83,  //   Output (Const,Var,Abs,Volatile)

            0x85, 0x10,  //   Report ID (16 / 0x10)
            0x09, 0x04,  //   Usage (0x04)
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x3F,  //   Report Count (63)
            0x91, 0x83,  //   Output (Const,Var,Abs,Volatile)

            0x85, 0x80,  //   Report ID (-128 / 0x80)
            0x09, 0x05,  //   Usage (0x05)
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x3F,  //   Report Count (63)
            0x91, 0x83,  //   Output (Const,Var,Abs,Volatile)

            0x85, 0x82,  //   Report ID (-126 / 0x82)
            0x09, 0x06,  //   Usage (0x06)
            0x75, 0x08,  //   Report Size (8)
            0x95, 0x3F,  //   Report Count (63)
            0x91, 0x83,  //   Output (Const,Var,Abs,Volatile)
            
            0xC0         // End Collection
        };
        return desc;
    }

    virtual uint16_t report_desc_length() override {
        return 203;
    }

private:
    // Gamepad state variables
    uint16_t current_buttons;
    uint8_t current_hat;
    uint8_t current_lsx;
    uint8_t current_lsy;
    uint8_t current_rsx;
    uint8_t current_rsy;

    // Handshake and protocol variables
    uint8_t _addr[6];
    bool vibration_enabled;
    uint8_t vibration_report;
    uint8_t vibration_idx;
    bool imu_enabled;
    uint8_t player_number;
    uint32_t _timer;
    uint32_t _timestamp;

    static const uint8_t VIB_OPTS[4];

    // Maps standard D-pad values (0-7, 8 for Released) to Pro Controller HAT bits
    uint8_t translateHat(uint8_t pokkenHat) {
        switch (pokkenHat) {
            case 0: return 0x2; // Up
            case 1: return 0x6; // Up-Right
            case 2: return 0x4; // Right
            case 3: return 0x5; // Down-Right
            case 4: return 0x1; // Down
            case 5: return 0x9; // Down-Left
            case 6: return 0x8; // Left
            case 7: return 0xa; // Up-Left
            default: return 0x0; // Nothing/Released
        }
    }

    // Maps 8-bit stick inputs (0..255) to 12-bit Pro Controller coordinates (0..4095)
    uint16_t scaleTo12BitX(uint8_t val) {
        if (val == 128) return 2047;
        if (val < 128) {
            return (uint16_t)((uint32_t)val * 2047 / 128);
        } else {
            return (uint16_t)(2047 + (uint32_t)(val - 128) * 2048 / 127);
        }
    }

    uint16_t scaleTo12BitY(uint8_t val) {
        if (val == 128) return 2047;
        if (val < 128) {
            // UP: val from [128, 0] to [2047, 4095]
            return (uint16_t)(2047 + (uint32_t)(128 - val) * 2048 / 128);
        } else {
            // DOWN: val from [128, 255] to [2047, 0]
            return (uint16_t)(2047 - (uint32_t)(val - 128) * 2047 / 127);
        }
    }

    // Formats the 10-byte Pro Controller standard report layout
    void packSwitchReport(uint8_t* buf) {
        buf[0] = 0x91; // High battery connection state

        // Unpack digital buttons from XboxBLE layout
        bool y = current_buttons & SWITCH_Y;
        bool b = current_buttons & SWITCH_B;
        bool a = current_buttons & SWITCH_A;
        bool x = current_buttons & SWITCH_X;
        bool l = current_buttons & SWITCH_L;
        bool r = current_buttons & SWITCH_R;
        bool zl = current_buttons & SWITCH_ZL;
        bool zr = current_buttons & SWITCH_ZR;
        bool minus = current_buttons & SWITCH_MINUS;
        bool plus = current_buttons & SWITCH_PLUS;
        bool lclick = current_buttons & SWITCH_LCLICK;
        bool rclick = current_buttons & SWITCH_RCLICK;
        bool home = current_buttons & SWITCH_HOME;
        bool capture = current_buttons & SWITCH_CAPTURE;

        // Pack buttons[0] (Y, X, B, A, R, ZR)
        uint8_t btn0 = 0;
        if (y) btn0 |= SWITCH_MASK_Y;
        if (x) btn0 |= SWITCH_MASK_X;
        if (b) btn0 |= SWITCH_MASK_B;
        if (a) btn0 |= SWITCH_MASK_A;
        if (r) btn0 |= SWITCH_MASK_R;
        if (zr) btn0 |= SWITCH_MASK_ZR;
        buf[1] = btn0;

        // Pack buttons[1] (Minus, Plus, R3, L3, Home, Capture)
        uint8_t btn1 = 0;
        if (minus) btn1 |= SWITCH_MASK_MINUS;
        if (plus) btn1 |= SWITCH_MASK_PLUS;
        if (rclick) btn1 |= SWITCH_MASK_R3;
        if (lclick) btn1 |= SWITCH_MASK_L3;
        if (home) btn1 |= SWITCH_MASK_HOME;
        if (capture) btn1 |= SWITCH_MASK_CAPTURE;
        buf[2] = btn1;

        // Pack buttons[2] (L, ZL, HAT switch)
        uint8_t btn2 = 0;
        if (l) btn2 |= SWITCH_MASK_L;
        if (zl) btn2 |= SWITCH_MASK_ZL;
        btn2 |= (translateHat(current_hat) & 0x0F);
        buf[3] = btn2;

        // Scale analog joysticks (12-bit)
        uint16_t lx = scaleTo12BitX(current_lsx);
        uint16_t ly = scaleTo12BitY(current_lsy);
        uint16_t rx = scaleTo12BitX(current_rsx);
        uint16_t ry = scaleTo12BitY(current_rsy);

        // Pack Left stick X/Y (3 bytes)
        buf[4] = lx & 0xFF;
        buf[5] = ((ly & 0x0F) << 4) | (lx >> 8);
        buf[6] = ly >> 4;

        // Pack Right stick X/Y (3 bytes)
        buf[7] = rx & 0xFF;
        buf[8] = ((ry & 0x0F) << 4) | (rx >> 8);
        buf[9] = ry >> 4;
    }

    uint8_t getTimer() {
        uint32_t now = millis();
        if (_timestamp == 0) {
            _timestamp = now;
            _timer = 0;
            return 0;
        }
        uint32_t delta_t = now - _timestamp;
        uint32_t elapsed_ticks = delta_t * 4;
        _timer = (_timer + elapsed_ticks) & 0xFF;
        _timestamp = now;
        return _timer;
    }

    // SPI read handler (emulates Pro Controller's factory calibration memory)
    void handleSPIRead(uint8_t* data) {
        uint8_t addr_bottom = data[11];
        uint8_t addr_top = data[12];
        uint8_t read_length = data[15];
        
        HID_REPORT reply;
        reply.length = 64;
        memset(reply.data, 0, 64);
        reply.data[0] = 0x21;
        reply.data[1] = getTimer();
        packSwitchReport(&reply.data[2]);
        reply.data[12] = vibration_report;
        
        reply.data[13] = 0x90; // ACK SPI Read
        reply.data[14] = 0x10; // Subcommand ID
        reply.data[15] = addr_bottom;
        reply.data[16] = addr_top;
        reply.data[17] = 0x00;
        reply.data[18] = 0x00;
        reply.data[19] = read_length;
        
        static const uint8_t stick_params[18] = {
            0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3, 0xD4, 0x14,
            0x54, 0x41, 0x15, 0x54, 0xC7, 0x79, 0x9C, 0x33,
            0x36, 0x63
        };
        
        if (addr_top == 0x60 && addr_bottom == 0x00) { // Serial number (none)
            memset(&reply.data[20], 0xFF, 16);
        }
        else if (addr_top == 0x60 && addr_bottom == 0x50) { // Body/button colors
            memset(&reply.data[20], 0x32, 3); // Grey body
            memset(&reply.data[23], 0xFF, 3); // White buttons
            memset(&reply.data[26], 0xFF, 7); // Grip colors
        }
        else if (addr_top == 0x60 && addr_bottom == 0x80) { // Six-Axis Factory Parameters
            reply.data[20] = 0x50;
            reply.data[21] = 0xFD;
            reply.data[22] = 0x00;
            reply.data[23] = 0x00;
            reply.data[24] = 0xC6;
            reply.data[25] = 0x0F;
            memcpy(&reply.data[26], stick_params, 18);
        }
        else if (addr_top == 0x60 && addr_bottom == 0x98) { // Stick Device Parameters 2
            memcpy(&reply.data[20], stick_params, 18);
        }
        else if (addr_top == 0x80 && addr_bottom == 0x10) { // User Calibration
            memset(&reply.data[20], 0xFF, 3);
        }
        else if (addr_top == 0x60 && addr_bottom == 0x3D) { // Factory Calibration
            static const uint8_t l_calibration[9] = {0xD4, 0x75, 0x61, 0xE5, 0x87, 0x7C, 0xEC, 0x55, 0x61};
            static const uint8_t r_calibration[9] = {0x5D, 0xD8, 0x7F, 0x18, 0xE6, 0x61, 0x86, 0x65, 0x5D};
            memcpy(&reply.data[20], l_calibration, 9);
            memcpy(&reply.data[29], r_calibration, 9);
            reply.data[38] = 0xFF;
            memset(&reply.data[39], 0x32, 3); // Grey body color
            memset(&reply.data[42], 0xFF, 3); // White buttons color
        }
        else if (addr_top == 0x60 && addr_bottom == 0x20) { // Six-Axis Sensor Calibration
            static const uint8_t sa_calibration[24] = {
                0xcc, 0x00, 0x40, 0x00, 0x91, 0x01,
                0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
                0xe7, 0xff, 0x0e, 0x00, 0xdc, 0xff,
                0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34
            };
            memcpy(&reply.data[20], sa_calibration, 24);
        }
        else {
            memset(&reply.data[20], 0xFF, read_length);
        }
        
        send_nb(&reply);
    }

    void handleOutputReport(uint8_t* data, uint16_t length) {
        if (length == 0) return;

        // Process Switch rumble command packets (Reports 0x01, 0x10, 0x11)
        if (data[0] == 0x01 || data[0] == 0x10 || data[0] == 0x11) {
            // Extract Low-Frequency (LF) amplitude (0..63)
            uint8_t lf_l = data[5] & 0x3F;
            uint8_t lf_r = data[9] & 0x3F;
            
            // Extract High-Frequency (HF) amplitude (0..63)
            uint8_t hf_l = data[3] & 0x3F;
            uint8_t hf_r = data[7] & 0x3F;
            
            // Combine by taking the maximum to support both LF and HF haptics
            uint8_t left_raw = (lf_l > hf_l) ? lf_l : hf_l;
            uint8_t right_raw = (lf_r > hf_r) ? lf_r : hf_r;
            
            // Scale 0..63 to the Xbox controller's 0..100 magnitude range
            uint8_t strong = ((uint32_t)left_raw * 100) / 63;
            uint8_t weak = ((uint32_t)right_raw * 100) / 63;
            
            // Apply a small haptic deadzone to ensure the Switch's neutral/stop
            // baseline (which parses to strong=1) drops to absolute 0 instantly
            if (strong <= 2) strong = 0;
            if (weak <= 2) weak = 0;
            
            // Acknowledge the rumble packet to keep the Switch console's transmission in sync.
            // Real controllers alternate the vibration report byte to signal successful processing.
            vibration_idx = (vibration_idx + 1) % 4;
            vibration_report = VIB_OPTS[vibration_idx];

            // Print diagnostic telemetry to Serial when rumble starts/stops or changes
            static uint8_t last_strong = 0;
            static uint8_t last_weak = 0;
            if (debug && (strong != last_strong || weak != last_weak)) {
                Serial.print("USB output report (");
                Serial.print(data[0], HEX);
                Serial.print(") received! Parsed HD Rumble: strong=");
                Serial.print(strong);
                Serial.print(", weak=");
                Serial.print(weak);
                Serial.print(", raw bytes: ");
                for (int i = 0; i < 10; i++) {
                    if (data[i] < 0x10) Serial.print("0");
                    Serial.print(data[i], HEX);
                    Serial.print(" ");
                }
                Serial.println(")");
                last_strong = strong;
                last_weak = weak;
            }
            
            extern void setXboxRumble(uint8_t strong, uint8_t weak);
            setXboxRumble(strong, weak);
        }

        // 1. USB Connection Handshakes (Starts with 0x80)
        if (data[0] == 0x80) {
            uint8_t cmd = data[1];
            HID_REPORT reply;
            reply.length = 64;
            memset(reply.data, 0, 64);
            reply.data[0] = 0x81;
            reply.data[1] = cmd;

            if (cmd == 0x01) { // USB Handshake request
                reply.data[2] = 0x00;
                reply.data[3] = 0x03;
                for (int i = 0; i < 6; i++) {
                    reply.data[4 + i] = _addr[5 - i]; // Reverse address
                }
            } else if (cmd == 0x02 || cmd == 0x03) {
                // Return simple ACK with no extra bytes
            }
            send_nb(&reply);
            return;
        }

        // 2. Subcommands (Starts with 0x01 or 0x10)
        if (data[0] == 0x01 || data[0] == 0x10) {
            uint8_t cmd = data[10]; // Subcommand ID

            if (cmd == 0x01) { // Pair Request
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x81; // ACK
                reply.data[14] = 0x01;
                reply.data[15] = 0x03;
                send_nb(&reply);
            }
            else if (cmd == 0x02) { // Device Info Query
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x82; // ACK
                reply.data[14] = 0x02;
                reply.data[15] = 0x03; // Firmware major
                reply.data[16] = 0x48; // Firmware minor
                reply.data[17] = 0x03; // Pro Controller
                reply.data[18] = 0x02;
                memcpy(&reply.data[19], _addr, 6);
                reply.data[25] = 0x01;
                reply.data[26] = 0x01;
                send_nb(&reply);
            }
            else if (cmd == 0x08) { // Set Shipment
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x08;
                send_nb(&reply);
            }
            else if (cmd == 0x10) { // SPI Read
                handleSPIRead(data);
            }
            else if (cmd == 0x03) { // Set Input Mode
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x03;
                send_nb(&reply);
            }
            else if (cmd == 0x04) { // Trigger buttons
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x83;
                reply.data[14] = 0x04;
                send_nb(&reply);
            }
            else if (cmd == 0x40) { // Enable IMU (6-axis)
                imu_enabled = (data[11] == 0x01);
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x40;
                send_nb(&reply);
            }
            else if (cmd == 0x41) { // IMU Sensitivity
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x41;
                send_nb(&reply);
            }
            else if (cmd == 0x48) { // Enable Rumble
                vibration_enabled = true;
                vibration_idx = 0;
                vibration_report = VIB_OPTS[0];
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x48;
                send_nb(&reply);
            }
            else if (cmd == 0x30) { // Set Player LEDs
                uint8_t bitfield = data[11];
                if (bitfield == 0x01 || bitfield == 0x10) player_number = 1;
                else if (bitfield == 0x03 || bitfield == 0x30) player_number = 2;
                else if (bitfield == 0x07 || bitfield == 0x70) player_number = 3;
                else if (bitfield == 0x0F || bitfield == 0xF0) player_number = 4;

                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x30;
                send_nb(&reply);
            }
            else if (cmd == 0x22) { // Set NFC/IR State
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = 0x22;
                send_nb(&reply);
            }
            else if (cmd == 0x21) { // Set NFC/IR Config
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0xA0; // ACK
                reply.data[14] = 0x21;
                
                static const uint8_t params[8] = {0x01, 0x00, 0xFF, 0x00, 0x08, 0x00, 0x1B, 0x01};
                memcpy(&reply.data[15], params, 8);
                reply.data[48] = 0xC8;
                send_nb(&reply);
            }
            else {
                // Unknown command fallback
                HID_REPORT reply;
                reply.length = 64;
                memset(reply.data, 0, 64);
                reply.data[0] = 0x21;
                reply.data[1] = getTimer();
                packSwitchReport(&reply.data[2]);
                reply.data[12] = vibration_report;
                reply.data[13] = 0x80;
                reply.data[14] = cmd;
                send_nb(&reply);
            }
        }
    }
};

const uint8_t SwitchUSB::VIB_OPTS[4] = {0x0a, 0x0c, 0x0b, 0x09};

#endif // SWITCH_USB_H
