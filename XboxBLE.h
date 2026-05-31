#ifndef XBOX_BLE_H
#define XBOX_BLE_H

#include <ble/BLE.h>
#include <events/mbed_events.h>
#include "SwitchUSB.h"

// Reference global Switch USB instance
extern SwitchUSB switchUsb;

// Global BLE Variables
static ble::connection_handle_t activeConnectionHandle = 0xFFFF;
static ble::attribute_handle_t reportCharValueHandle = 0xFFFF;
static ble::attribute_handle_t cccdHandle = 0xFFFF;
bool isFullyPaired = false;

// Forward declaration of report parser
static void parseXboxReport(const uint8_t* data, uint16_t length);

// ------------------------------------------------------------------------
// 1. LOW-LEVEL SECURITY MANAGER EVENT HANDLER
// ------------------------------------------------------------------------
// Descriptor discovery state machine variables
static DiscoveredCharacteristic reportChars[4];
static uint8_t reportCharCount = 0;
static uint8_t currentDescriptorDiscoveryIndex = 0;

struct DescMapping {
    ble::attribute_handle_t descHandle;
    ble::attribute_handle_t charValueHandle;
};
static DescMapping descMappings[8];
static uint8_t descMappingCount = 0;
static ble::attribute_handle_t xboxRumbleValueHandle = 0xFFFF;


// ------------------------------------------------------------------------
// 1. LOW-LEVEL SECURITY MANAGER EVENT HANDLER
// ------------------------------------------------------------------------
class NativeSecHandler : public ble::SecurityManager::EventHandler {
public:
    virtual void pairingRequest(ble::connection_handle_t connectionHandle) override {
        DEBUG_PRINTLN(">>> NATIVE BLE EVENT: pairingRequest - Accepting Xbox pairing request...");
        ble::BLE::Instance().securityManager().acceptPairingRequest(connectionHandle);
    }
    
    virtual void pairingResult(ble::connection_handle_t connectionHandle, ble::SecurityManager::SecurityCompletionStatus_t status) override {
        DEBUG_PRINT(">>> NATIVE BLE EVENT: pairingResult - Status: ");
        DEBUG_PRINTLN((int)status);
        if (status == ble::SecurityManager::SEC_STATUS_SUCCESS) {
            DEBUG_PRINTLN(">>> Secure Pairing COMPLETED!");
        } else {
            DEBUG_PRINTLN(">>> Secure Pairing FAILED!");
        }
    }
    
    virtual void linkEncryptionResult(ble::connection_handle_t connectionHandle, ble::link_encryption_t result) override {
        DEBUG_PRINT(">>> NATIVE BLE EVENT: linkEncryptionResult - Encryption State: ");
        if (result == ble::link_encryption_t::ENCRYPTED) {
            DEBUG_PRINTLN("ENCRYPTED");
        } else if (result == ble::link_encryption_t::ENCRYPTED_WITH_MITM) {
            DEBUG_PRINTLN("ENCRYPTED_WITH_MITM");
        } else {
            DEBUG_PRINTLN("NOT_ENCRYPTED");
        }
        
        if (result == ble::link_encryption_t::ENCRYPTED || result == ble::link_encryption_t::ENCRYPTED_WITH_MITM) {
            DEBUG_PRINTLN(">>> Link is SECURELY ENCRYPTED!");
            
            // Reset characteristic discovery states for this new session
            reportCharCount = 0;
            currentDescriptorDiscoveryIndex = 0;
            
            // Once the link is securely encrypted, it is safe to query attributes without being ignored!
            DEBUG_PRINTLN("Launching GATT Service Discovery for HID (0x1812)...");
            ble::BLE::Instance().gattClient().launchServiceDiscovery(
                connectionHandle,
                onServiceDiscovered,
                onCharacteristicDiscovered,
                UUID(0x1812), // HID Service
                UUID(0x2A4D)  // Report Characteristic
            );
        } else {
            DEBUG_PRINTLN(">>> Link remains UNENCRYPTED! Retrying security request...");
            ble::BLE::Instance().securityManager().setLinkSecurity(
                connectionHandle,
                ble::SecurityManager::SECURITY_MODE_ENCRYPTION_NO_MITM
            );
        }
    }

    // Callback invoked when GATT Service and Characteristic discovery has completed
    static void onServiceDiscoveryTerminationCallback(ble::connection_handle_t connectionHandle) {
        DEBUG_PRINTLN("GATT: Service and Characteristic discovery completed!");
        DEBUG_PRINT("GATT: Discovered HOGP characteristics count: ");
        DEBUG_PRINTLN(reportCharCount);
        
        if (reportCharCount > 0) {
            currentDescriptorDiscoveryIndex = 0;
            DEBUG_PRINT("GATT: Launching sequential descriptor discovery for HOGP characteristic at handle: ");
            DEBUG_PRINTLN(reportChars[0].getValueHandle());
            
            ble_error_t err = reportChars[0].discoverDescriptors(
                onDescriptorDiscovered,
                onDescriptorDiscoveryTerminated
            );
            if (err != BLE_ERROR_NONE) {
                DEBUG_PRINT("GATT ERROR: discoverDescriptors failed! Code: ");
                DEBUG_PRINTLN((int)err);
            }
        } else {
            DEBUG_PRINTLN("GATT WARNING: No HOGP Report Characteristics discovered!");
        }
    }

private:
    // Callbacks for GATT Service/Characteristic Discovery
    static void onServiceDiscovered(const DiscoveredService* service) {
        DEBUG_PRINT("GATT: Discovered HID Service at handle range [");
        DEBUG_PRINT(service->getStartHandle());
        DEBUG_PRINT(" - ");
        DEBUG_PRINT(service->getEndHandle());
        DEBUG_PRINTLN("]");
    }

    static void onCharacteristicDiscovered(const DiscoveredCharacteristic* characteristic) {
        DEBUG_PRINT("GATT: Discovered Characteristic UUID: ");
        DEBUG_PRINTLN(characteristic->getUUID().getShortUUID(), HEX);
        
        if (characteristic->getUUID().getShortUUID() == 0x2A4D) {
            DEBUG_PRINT("GATT: Found HOGP Report Characteristic at Value Handle: ");
            DEBUG_PRINTLN(characteristic->getValueHandle());
            
            DiscoveredCharacteristic::Properties_t props = characteristic->getProperties();
            if ((props.write() || props.writeWoResp()) && !props.notify()) {
                xboxRumbleValueHandle = characteristic->getValueHandle();
                DEBUG_PRINT(">>> FOUND XBOX RUMBLE VALUE HANDLE (BY PROPERTIES): ");
                DEBUG_PRINTLN(xboxRumbleValueHandle);
            }
            
            if (reportCharCount < 4) {
                reportChars[reportCharCount++] = *characteristic;
            }
        }
    }

    static void onDescriptorDiscovered(const CharacteristicDescriptorDiscovery::DiscoveryCallbackParams_t* params) {
        DEBUG_PRINT("GATT: Discovered Descriptor UUID: ");
        DEBUG_PRINTLN(params->descriptor.getUUID().getShortUUID(), HEX);
        
        if (params->descriptor.getUUID().getShortUUID() == 0x2902) { // CCCD
            cccdHandle = params->descriptor.getAttributeHandle();
            reportCharValueHandle = params->characteristic.getValueHandle();
            
            DEBUG_PRINT("GATT: Found CCCD Descriptor at Handle: ");
            DEBUG_PRINTLN(cccdHandle);
            DEBUG_PRINT("GATT: Bound active Input Report Value Handle: ");
            DEBUG_PRINTLN(reportCharValueHandle);
            
            // Write 0x0001 to enable notifications asynchronously
            DEBUG_PRINTLN("GATT: Writing 0x0001 to CCCD to enable HID reports...");
            uint16_t cccdValue = 0x0001;
            ble::BLE::Instance().gattClient().write(
                ble::GattClient::WriteOp_t::GATT_OP_WRITE_REQ,
                params->descriptor.getConnectionHandle(),
                cccdHandle,
                sizeof(cccdValue),
                (const uint8_t*)&cccdValue
            );
            
            isFullyPaired = true;
            pinMode(LED_BUILTIN, OUTPUT);
            digitalWrite(LED_BUILTIN, HIGH); // Solid light when paired and subscribed!
            DEBUG_PRINTLN("SUCCESSFULLY INJECTED SECURITY HANDSHAKE AND SUBSCRIBED!");
            DEBUG_PRINTLN("----------------------------------------------------------------");
        }
        else if (params->descriptor.getUUID().getShortUUID() == 0x2908) { // Report Reference
            DEBUG_PRINT("GATT: Found Report Reference Descriptor at Handle: ");
            DEBUG_PRINTLN(params->descriptor.getAttributeHandle());
            
            if (descMappingCount < 8) {
                descMappings[descMappingCount].descHandle = params->descriptor.getAttributeHandle();
                descMappings[descMappingCount].charValueHandle = params->characteristic.getValueHandle();
                descMappingCount++;
            }
            
            // Read Report Reference to find Report ID & Type
            ble::BLE::Instance().gattClient().read(
                params->descriptor.getConnectionHandle(),
                params->descriptor.getAttributeHandle(),
                0
            );
        }
    }

    static void onDescriptorDiscoveryTerminated(const CharacteristicDescriptorDiscovery::TerminationCallbackParams_t* params) {
        DEBUG_PRINT("GATT: Descriptor discovery finished for handle: ");
        DEBUG_PRINTLN(params->characteristic.getValueHandle());
        
        currentDescriptorDiscoveryIndex++;
        if (currentDescriptorDiscoveryIndex < reportCharCount) {
            DEBUG_PRINT("GATT: Launching next descriptor discovery for HOGP characteristic at handle: ");
            DEBUG_PRINTLN(reportChars[currentDescriptorDiscoveryIndex].getValueHandle());
            
            ble_error_t err = reportChars[currentDescriptorDiscoveryIndex].discoverDescriptors(
                onDescriptorDiscovered,
                onDescriptorDiscoveryTerminated
            );
            if (err != BLE_ERROR_NONE) {
                DEBUG_PRINT("GATT ERROR: discoverDescriptors failed! Code: ");
                DEBUG_PRINTLN((int)err);
            }
        } else {
            DEBUG_PRINTLN("GATT: All descriptor discoveries complete!");
        }
    }
};

// ------------------------------------------------------------------------
// 2. LOW-LEVEL GAP EVENT HANDLER (Connection, Scanning)
// ------------------------------------------------------------------------
class NativeGapHandler : public ble::Gap::EventHandler {
public:
    virtual void onAdvertisingReport(const ble::AdvertisingReportEvent &event) override {
        const uint8_t* payload = event.getPayload().data();
        uint16_t size = event.getPayload().size();
        const uint8_t* addr = event.getPeerAddress().data();
        bool foundXbox = false;
        
        // 1. MAC OUI MATCH (Primary): Check if MAC starts with Microsoft's Registered OUI for Xbox controllers (44:16:22)
        if (addr[5] == 0x44 && addr[4] == 0x16 && addr[3] == 0x22) {
            foundXbox = true;
        }
        
        // 2. NAME MATCH (Fallback): Case-insensitive search for "xbox" in payload or scan response
        if (!foundXbox && size >= 4) {
            for (uint16_t i = 0; i <= size - 4; i++) {
                if ((payload[i] == 'X' || payload[i] == 'x') &&
                    (payload[i+1] == 'O' || payload[i+1] == 'o') &&
                    (payload[i+2] == 'B' || payload[i+2] == 'b') &&
                    (payload[i+3] == 'X' || payload[i+3] == 'x')) {
                    foundXbox = true;
                    break;
                }
            }
        }
        
        // Print scanning status once every 3 seconds to show life and help diagnose nearby BLE devices
        static uint32_t lastPrintTime = 0;
        if (debug && (millis() - lastPrintTime > 3000)) {
            DEBUG_PRINT("NATIVE GAP: Scanning... Seen Peer: ");
            for (int i = 5; i >= 0; i--) {
                DEBUG_PRINT(addr[i], HEX);
                if (i > 0) DEBUG_PRINT(":");
            }
            DEBUG_PRINT(" (Payload size: ");
            DEBUG_PRINT(size);
            DEBUG_PRINT(" bytes) | Text: ");
            // Print printable characters to help identify nearby devices
            for (uint16_t i = 0; i < size; i++) {
                if (payload[i] >= 32 && payload[i] <= 126) {
                    DEBUG_PRINT((char)payload[i]);
                } else {
                    DEBUG_PRINT(".");
                }
            }
            DEBUG_PRINTLN();
            lastPrintTime = millis();
        }
        
        if (foundXbox) {
            DEBUG_PRINTLN("----------------------------------------------------------------");
            DEBUG_PRINT("NATIVE GAP: Found Xbox Controller at Peer: ");
            for (int i = 5; i >= 0; i--) {
                DEBUG_PRINT(addr[i], HEX);
                if (i > 0) DEBUG_PRINT(":");
            }
            DEBUG_PRINTLN();
            DEBUG_PRINTLN("Stopping scan and establishing raw connection...");
            
            ble::BLE::Instance().gap().stopScan();
            
            // Connect to Xbox controller using native connection parameters
            ble::ConnectionParameters connParams;
            ble_error_t err = ble::BLE::Instance().gap().connect(
                event.getPeerAddressType(),
                event.getPeerAddress(),
                connParams
            );
            
            if (err != BLE_ERROR_NONE) {
                DEBUG_PRINT("NATIVE GAP ERROR: Connection trigger failed! Error Code: ");
                DEBUG_PRINTLN((int)err);
                ble::BLE::Instance().gap().startScan(); // Resume scan
            }
        }
    }
    
    virtual void onConnectionComplete(const ble::ConnectionCompleteEvent &event) override {
        if (event.getStatus() == BLE_ERROR_NONE) {
            activeConnectionHandle = event.getConnectionHandle();
            DEBUG_PRINT("NATIVE GAP: Raw Connection Completed! Handle: ");
            DEBUG_PRINTLN(activeConnectionHandle);
            
            // Immediately request Mbed OS Security Manager link encryption!
            DEBUG_PRINTLN("NATIVE GAP: Requesting link encryption handshake...");
            ble::BLE::Instance().securityManager().setLinkSecurity(
                activeConnectionHandle,
                ble::SecurityManager::SECURITY_MODE_ENCRYPTION_NO_MITM
            );
        } else {
            DEBUG_PRINT("NATIVE GAP ERROR: Connection failed! Status Code: ");
            DEBUG_PRINTLN((int)event.getStatus());
            ble::BLE::Instance().gap().startScan(); // Resume scan
        }
    }
    
    virtual void onDisconnectionComplete(const ble::DisconnectionCompleteEvent &event) override {
        DEBUG_PRINTLN("----------------------------------------------------------------");
        DEBUG_PRINTLN("NATIVE GAP: Xbox controller disconnected!");
        DEBUG_PRINTLN("----------------------------------------------------------------");
        
        activeConnectionHandle = 0xFFFF;
        reportCharValueHandle = 0xFFFF;
        xboxRumbleValueHandle = 0xFFFF;
        cccdHandle = 0xFFFF;
        descMappingCount = 0;
        isFullyPaired = false;
        
        digitalWrite(LED_BUILTIN, LOW); // Turn off LED
        
        // Reset gamepad values to prevent stuck keys on Switch
        switchUsb.resetState();
        switchUsb.sendState();
        
        // Resume scanning
        ble::BLE::Instance().gap().startScan();
    }
};

// ------------------------------------------------------------------------
// 3. GLOBAL HANDLER INSTANCES & CALLBACKS
// ------------------------------------------------------------------------
static NativeGapHandler nativeGapHandler;
static NativeSecHandler nativeSecHandler;

// Handle Data Read Callback (Receives results of descriptor reads)
static void onDataReadCallback(const GattReadCallbackParams* params) {
    if (params->len >= 2) {
        uint8_t reportId = params->data[0];
        uint8_t reportType = params->data[1];
        DEBUG_PRINT("GATT READ: Descriptor Handle: ");
        DEBUG_PRINT(params->handle);
        DEBUG_PRINT(" -> Report ID: ");
        DEBUG_PRINT(reportId, HEX);
        DEBUG_PRINT(" | Report Type: ");
        DEBUG_PRINTLN(reportType, HEX);
        
        if (reportId == 0x03 && reportType == 0x02) { // Output Report for Rumble
            for (uint8_t i = 0; i < descMappingCount; i++) {
                if (descMappings[i].descHandle == params->handle) {
                    xboxRumbleValueHandle = descMappings[i].charValueHandle;
                    DEBUG_PRINT(">>> FOUND XBOX RUMBLE VALUE HANDLE: ");
                    DEBUG_PRINTLN(xboxRumbleValueHandle);
                    break;
                }
            }
        }
    }
}

// Handle Value Notification (HVX) Callback (Receives real-time Xbox controller reports)
static void onHVXCallback(const GattHVXCallbackParams* params) {
    // Debug telemetry: Print incoming data packets once every second to verify activity
    static uint32_t lastHVXPrintTime = 0;
    if (debug && (millis() - lastHVXPrintTime > 1000)) {
        DEBUG_PRINT("GATT HVX: Live report received on handle: ");
        DEBUG_PRINT(params->handle);
        DEBUG_PRINT(" | Size: ");
        DEBUG_PRINT(params->len);
        DEBUG_PRINT(" | Bound Active Handle: ");
        DEBUG_PRINTLN(reportCharValueHandle);
        lastHVXPrintTime = millis();
    }

    if (params->handle == reportCharValueHandle && params->len >= 15) {
        parseXboxReport(params->data, params->len);
    }
}

// Main BLE Stack Initialization Callback
static void onBleInitComplete(BLE::InitializationCompleteCallbackContext* params) {
    if (params->error != BLE_ERROR_NONE) {
        DEBUG_PRINTLN("NATIVE BLE: Initialization FAILED!");
        return;
    }
    
    BLE& ble = params->ble;
    
    // Register low-level event handlers
    ble.gap().setEventHandler(&nativeGapHandler);
    ble.securityManager().setSecurityManagerEventHandler(&nativeSecHandler);
    ble.gattClient().onHVX(onHVXCallback);
    ble.gattClient().onDataRead(onDataReadCallback);
    ble.gattClient().onServiceDiscoveryTermination(NativeSecHandler::onServiceDiscoveryTerminationCallback);
    
    // Initialize Security Manager (enable bonding, select Just Works pairing)
    ble.securityManager().init(
        true,   // enableBonding
        false,  // requireMITM
        ble::SecurityManager::IO_CAPS_NONE, // No inputs/outputs (forces Just Works)
        NULL    // No static passkey
    );
    
    // Set standard scanning parameters with active scanning enabled to fetch scan responses (where names are located)
    ble::ScanParameters scanParams;
    scanParams.set1mPhyConfiguration(
        ble::scan_interval_t(80), // 50 ms
        ble::scan_window_t(40),   // 25 ms
        true                      // active_scanning = true!
    );
    ble.gap().setScanParameters(scanParams);
    
    // Start scanning indefinitely
    ble.gap().startScan();
    DEBUG_PRINTLN("NATIVE BLE: Stack Initialized! ACTIVE SCANNING enabled. Scanning for Xbox Wireless Controller...");
}

// ------------------------------------------------------------------------
// 4. THE XBOX INPUT PARSER & SWITCH TRANSLATION
// ------------------------------------------------------------------------
static void parseXboxReport(const uint8_t* data, uint16_t length) {
    // Debug helper to print full hex packet when buttons or D-pad change
    static uint8_t lastDpad = 0, lastBtn1 = 0, lastBtn2 = 0, lastShare = 0;
    if (debug && (data[12] != lastDpad || data[13] != lastBtn1 || data[14] != lastBtn2 || data[15] != lastShare)) {
        DEBUG_PRINT("DEBUG HEX REPORT: ");
        for (uint16_t i = 0; i < length; i++) {
            if (data[i] < 0x10) DEBUG_PRINT("0");
            DEBUG_PRINT(data[i], HEX);
            DEBUG_PRINT(" ");
        }
        DEBUG_PRINTLN();
        lastDpad = data[12];
        lastBtn1 = data[13];
        lastBtn2 = data[14];
        lastShare = data[15];
    }

    // --- PARSE JOYSTICKS (16-bit analog values, Little Endian) ---
    uint16_t ls_x = (data[1] << 8) | data[0];
    uint16_t ls_y = (data[3] << 8) | data[2];
    uint16_t rs_x = (data[5] << 8) | data[4];
    uint16_t rs_y = (data[7] << 8) | data[6];

    // --- PARSE TRIGGERS (16-bit analog values, Little Endian) ---
    uint16_t lt = (data[9] << 8) | data[8];
    uint16_t rt = (data[11] << 8) | data[10];

    // --- PARSE BUTTON GROUP 1 (Byte 13) ---
    uint8_t btn1 = data[13];
    bool xbox_a  = btn1 & (1 << 0);
    bool xbox_b  = btn1 & (1 << 1);
    bool xbox_x  = btn1 & (1 << 3);
    bool xbox_y  = btn1 & (1 << 4);
    bool xbox_lb = btn1 & (1 << 6);
    bool xbox_rb = btn1 & (1 << 7);

    // --- PARSE BUTTON GROUP 2 (Byte 14) ---
    uint8_t btn2 = data[14];
    bool xbox_select = btn2 & (1 << 2); // Bit 10 of overall button report
    bool xbox_start  = btn2 & (1 << 3); // Bit 11 of overall button report
    bool xbox_home   = btn2 & (1 << 4); // Bit 12 of overall button report
    bool xbox_l3     = btn2 & (1 << 5); // Bit 13 of overall button report
    bool xbox_r3     = btn2 & (1 << 6); // Bit 14 of overall button report

    // --- PARSE D-PAD (Byte 12) ---
    uint8_t xbox_dpad = data[12] & 0x0F;

    // --- PARSE SHARE BUTTON (Byte 15) ---
    bool xbox_share = data[15] & 0x01;

    // --- TRANSLATE TO NINTENDO SWITCH LAYOUT ---
    uint16_t switch_buttons = 0;
    
    // Swap buttons to match Nintendo's A/B and X/Y layout
    if (xbox_a)      switch_buttons |= SWITCH_B;
    if (xbox_b)      switch_buttons |= SWITCH_A;
    if (xbox_x)      switch_buttons |= SWITCH_Y;
    if (xbox_y)      switch_buttons |= SWITCH_X;
    
    if (xbox_lb)     switch_buttons |= SWITCH_L;
    if (xbox_rb)     switch_buttons |= SWITCH_R;
    
    // Map triggers to digital ZL / ZR at 50% squeeze
    if (lt > 512)    switch_buttons |= SWITCH_ZL;
    if (rt > 512)    switch_buttons |= SWITCH_ZR;

    if (xbox_select) switch_buttons |= SWITCH_MINUS;
    if (xbox_start)  switch_buttons |= SWITCH_PLUS;
    if (xbox_l3)     switch_buttons |= SWITCH_LCLICK;
    if (xbox_r3)     switch_buttons |= SWITCH_RCLICK;
    if (xbox_home)   switch_buttons |= SWITCH_HOME;
    if (xbox_share)  switch_buttons |= SWITCH_CAPTURE;

    switchUsb.setButtons(switch_buttons);

    // Map D-pad (HAT Switch)
    uint8_t switch_hat = 8; // Released
    if (xbox_dpad >= 1 && xbox_dpad <= 8) {
        switch_hat = xbox_dpad - 1;
    }
    switchUsb.setHat(switch_hat);

    // Map Joysticks (Scale 16-bit to 8-bit)
    uint8_t switch_lsx = ls_x / 256;
    uint8_t switch_lsy = ls_y / 256;
    uint8_t switch_rsx = rs_x / 256;
    uint8_t switch_rsy = rs_y / 256;

    switchUsb.setLeftStick(switch_lsx, switch_lsy);
    switchUsb.setRightStick(switch_rsx, switch_rsy);

    // Send the converted states over USB to the Switch console
    switchUsb.sendState();
}

// ------------------------------------------------------------------------
// 5. XBOX BLE RUMBLE EMITTER & THROTTLED DISPATCHER
// ------------------------------------------------------------------------
// Global state tracking variables
static uint8_t targetStrong = 0;
static uint8_t targetWeak = 0;
static uint8_t currentStrong = 0;
static uint8_t currentWeak = 0;
static uint32_t lastRumbleTime = 0;

// Customizable Haptic Intensity Scale (Percentage)
// The Xbox controller grip motors are extremely powerful.
// Setting this to 1 scales the maximum vibration intensity to 1% of its full strength,
// providing comfortable, premium haptic feedback while maintaining analog depth.
#define XBOX_RUMBLE_INTENSITY_SCALE  4

void setXboxRumble(uint8_t strong, uint8_t weak) {
    // Safe bounds check: Xbox haptic logical max magnitude is 100
    if (strong > 100) strong = 100;
    if (weak > 100) weak = 100;
    
    // Scale the haptic magnitudes according to our customizable intensity percentage
    strong = ((uint16_t)strong * XBOX_RUMBLE_INTENSITY_SCALE) / 100;
    weak = ((uint16_t)weak * XBOX_RUMBLE_INTENSITY_SCALE) / 100;
    
    // Simply buffer the target intensities requested by the Switch host
    targetStrong = strong;
    targetWeak = weak;
}

void updateXboxRumble() {
    if (activeConnectionHandle == 0xFFFF || xboxRumbleValueHandle == 0xFFFF) {
        return;
    }
    
    // Do nothing if the controller's active haptics match the requested target haptics
    if (targetStrong == currentStrong && targetWeak == currentWeak) {
        return;
    }
    
    uint32_t now = millis();
    
    // Safety rate limiting: Enforce a strict 15ms interval between BLE packet writes.
    // This perfectly aligns with standard BLE connection intervals and prevents stack queue buffer overflows,
    // while guaranteeing a maximum haptic latency of only 15ms (completely imperceptible).
    if (now - lastRumbleTime < 15) {
        return;
    }
    
    // Lock in the new state and timestamp the write
    lastRumbleTime = now;
    currentStrong = targetStrong;
    currentWeak = targetWeak;
    
    // Centralized Visual Diagnostic: Flicker built-in LED only during active over-the-air haptic transmission
    if (currentStrong > 0 || currentWeak > 0) {
        static bool led_state = false;
        led_state = !led_state;
        digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
    } else {
        // Restore paired indicator solid HIGH when not rumbling
        digitalWrite(LED_BUILTIN, HIGH);
    }

    DEBUG_PRINT("BLE: Writing haptics: strong=");
    DEBUG_PRINT(currentStrong);
    DEBUG_PRINT(", weak=");
    DEBUG_PRINTLN(currentWeak);
    
    uint8_t packet[8];
    packet[0] = 0x0F; // Motor Mask: enable all 4 motors (Trigger L/R & Grip L/R)
    packet[1] = 0x00; // Left Trigger
    packet[2] = 0x00; // Right Trigger
    packet[3] = currentStrong;
    packet[4] = currentWeak;
    packet[5] = (currentStrong > 0 || currentWeak > 0) ? 0xFF : 0x00; // Duration 10ms (0xFF = 2.55s, 0x00 = stop)
    packet[6] = 0x00; // Pulse Release
    packet[7] = (currentStrong > 0 || currentWeak > 0) ? 0xFF : 0x00; // Loop count (0xFF = repeat infinitely, 0x00 = stop)
    
    ble_error_t err = ble::BLE::Instance().gattClient().write(
        ble::GattClient::WriteOp_t::GATT_OP_WRITE_REQ, // Write Request (Acknowledged Write)
        activeConnectionHandle,
        xboxRumbleValueHandle,
        sizeof(packet),
        packet
    );
    
    if (err != BLE_ERROR_NONE) {
        // If the write fails (e.g. BLE buffer full), reset tracking to force a retry on the next loop iteration
        currentStrong = 0xFF;
        currentWeak = 0xFF;
        DEBUG_PRINT("BLE WRITE ERROR: Code ");
        DEBUG_PRINTLN((int)err);
    }
}

#endif // XBOX_BLE_H
