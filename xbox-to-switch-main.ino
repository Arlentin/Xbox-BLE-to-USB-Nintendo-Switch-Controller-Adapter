/**
 * Xbox BLE-to-USB Nintendo Switch Controller Adapter (Pure Mbed OS Native BLE Version)
 * Hardware: Arduino Nano 33 BLE / Nano 33 BLE Sense
 * Target Core: "Arduino Mbed OS Nano Boards" (Official Core)
 * 
 * Safe Recovery Mode:
 * If the board's USB serial port disappears, double-press the physical 
 * reset button on the Arduino to force Bootloader Mode (pulsing yellow LED).
 * Then upload a standard "Blink" sketch to restore factory settings.
 */

#include <events/mbed_events.h>
#include <ble/BLE.h>
#include "SwitchUSB.h"
#include "XboxBLE.h"

// Instantiate the emulated Switch gamepad (spoofs Nintendo Pro Controller VID/PID and string descriptors)
SwitchUSB switchUsb;

// Debug mode flag. Set to true to enable serial diagnostic messages, or false to disable them.
bool debug = false;

// Mbed EventQueue to process background BLE event signals asynchronously
static events::EventQueue eventQueue(16 * EVENTS_EVENT_SIZE);

// Callback that schedules pending BLE stack events onto our main EventQueue
void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context) {
  eventQueue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}

// Function to initialize the native Mbed BLE stack
void startBLE() {
  BLE& ble = BLE::Instance();
  
  // Register the event scheduler
  ble.onEventsToProcess(schedule_ble_events);
  
  // Initialize the BLE radio. Once complete, it invokes onBleInitComplete (defined in XboxBLE.h)
  ble_error_t err = ble.init(onBleInitComplete);
  if (err != BLE_ERROR_NONE) {
    DEBUG_PRINT("ERROR: Failed to initialize BLE stack! Error: ");
    DEBUG_PRINTLN((int)err);
    // Rapid flashing indicates a Bluetooth initialization failure
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  // 2. Initialize Serial (For troubleshooting/debugging on PC)
  if (debug) {
    Serial.begin(115200);
    while (!Serial && millis() < 8000) {
      // Wait up to 8 seconds for serial port to connect (helps see boot logs)
    }
    DEBUG_PRINTLN("================================================================");
    DEBUG_PRINTLN("System Booting: Xbox-to-Switch Native BLE-to-USB Adapter");
    DEBUG_PRINTLN("================================================================");
    DEBUG_PRINTLN("Queueing BLE Stack Initialization...");
  }
  
  // 3. Queue the BLE initialization event
  eventQueue.call(startBLE);
}

void loop() {
  // Poll the USB gamepad interface for incoming Switch handshake / subcommand packets
  switchUsb.process();

  // Update Xbox haptics over BLE using throttled target-state runner
  extern void updateXboxRumble();
  updateXboxRumble();

  // Dispatch pending events from our EventQueue.
  // The dispatch_for() function runs the queue's scheduler in a non-blocking
  // manner for the specified time, processing any scheduled BLE events or callbacks.
  eventQueue.dispatch_for(std::chrono::milliseconds(1));
}
