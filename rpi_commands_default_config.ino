/*
This Arduino project aims to integrate an injection moulding machine into MOMAMS:
https://github.com/aviharos/momams

The Arduino reads the PLC signals (using optocouplers),
and sends a JSON data packet to the Raspberry Pi on Serial (USB) for each event.

The Raspberry Pi will then issue the command with the given id.
This way, the Orion broker's objects can be updated.

The Raspberry Pi runs
https://github.com/aviharos/rpi_commands

The JSON sent contains each command_id as a key. Any values are passed on as arguments to the JSON object.
Example:
{"1": null, "5": 3, "6": 8000}
This means that commands 1, 5 and 6 will be issued by the Raspberry Pi.
  Command 1 without any arguments,
  command 5 with argument 3,
  command 6 with argument 8000.

Although rpi_commands can handle parameters,
the current system does not need the use of parameters.

Dependencies:
  ArduinoJson@6.19.4 or higher

Credits:
https://www.arduino.cc/en/Tutorial/BuiltInExamples/StateChangeDetection
*/

#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

#define MAX_MILLIS 4294967295

// constants
const unsigned long DEBOUNCE_DELAY_MILLISECONDS = 50;
const unsigned long RESEND_AVAILABILITY_STATUS_PERIOD_MILLISECONDS = 60e3;
const int BAUD_RATE = 9600;

// pins
const byte availabilityPin = 2;
const byte mouldClosePin = 3;
const byte rejectPin = 4;

// structs
struct inputSignal {
  byte pin;
  bool isSendingCommandNecessary = true;
  bool reading = HIGH;
  bool lastReading = HIGH;
  bool state = HIGH;
  unsigned long lastChangeTimeMilliseconds = millis();
  unsigned long lastTimeCommandWasSentMilliseconds = millis();
  bool hasPositiveEdge = false;
  bool hasNegativeEdge = false;
  bool edgeHasBeenSignalled = true;
};

// initiating global variables
inputSignal availabilitySignal;
inputSignal mouldCloseSignal;
inputSignal rejectSignal;
bool isReject = false;

// functions
void setupInputSignal(inputSignal *signal, byte pin) {
  (*signal).pin = pin;
  pinMode((*signal).pin, INPUT_PULLUP);
  (*signal).isSendingCommandNecessary = false;
  (*signal).reading = HIGH;
  (*signal).lastReading = HIGH;
  (*signal).state = HIGH;
  (*signal).lastChangeTimeMilliseconds = millis();
  (*signal).lastTimeCommandWasSentMilliseconds = millis();
  (*signal).hasNegativeEdge = false;
  (*signal).hasPositiveEdge = false;
  (*signal).edgeHasBeenSignalled = true;
}

void setup() {
  setupInputSignal(&availabilitySignal, availabilityPin);
  setupInputSignal(&mouldCloseSignal, mouldClosePin);
  setupInputSignal(&rejectSignal, rejectPin);

  isReject = false;

  // initialize serial communication:
  Serial.begin(BAUD_RATE);
}

bool isReadingChanged(inputSignal *signal) {
  if ((*signal).lastReading != (*signal).reading)
    return true;
  else
    return false;
}

void updateAfterReadingChanged(inputSignal *signal) {
  (*signal).lastReading = (*signal).reading;
  (*signal).lastChangeTimeMilliseconds = millis();
  (*signal).isSendingCommandNecessary = false;
  (*signal).edgeHasBeenSignalled = false;
  (*signal).hasPositiveEdge = false;
  (*signal).hasNegativeEdge = false;
}

int getTimeSinceLastChange(unsigned long lastChangeTimeMilliseconds) {
  unsigned long currentTime = millis();
  if (lastChangeTimeMilliseconds > currentTime) {
    // overflow happened
    unsigned long firstPart = MAX_MILLIS - lastChangeTimeMilliseconds;
    return firstPart + currentTime;
  } else {
    return currentTime - lastChangeTimeMilliseconds;
  };
}

int getTimeSinceLastSignalChange(struct inputSignal *signal) {
  return getTimeSinceLastChange((*signal).lastChangeTimeMilliseconds);
}

bool isStableLongerThan(inputSignal *signal, unsigned long timeDelta) {
  return getTimeSinceLastSignalChange(signal) > timeDelta;
}

bool isResendCommandDue(inputSignal *signal) {
  if (getTimeSinceLastChange((*signal).lastTimeCommandWasSentMilliseconds) > RESEND_AVAILABILITY_STATUS_PERIOD_MILLISECONDS) {
    return true;
  } else {
    return false;
  }
}

void indicateSendingCommandNecessaryIfNeeded(inputSignal *signal) {
  if (isResendCommandDue(signal)) {
    (*signal).isSendingCommandNecessary = true;
  }
}

void debounceEdges(inputSignal *signal) {
  if (isStableLongerThan(signal, DEBOUNCE_DELAY_MILLISECONDS)) {
    (*signal).state = (*signal).reading;
    if ((*signal).edgeHasBeenSignalled) {
      (*signal).hasPositiveEdge = false;
      (*signal).hasNegativeEdge = false;
    } else { // edge has not been signalled, so set hasPositiveEdge or hasNegativeEdge to true
      (*signal).edgeHasBeenSignalled = true;
      (*signal).isSendingCommandNecessary = true;
      // because of INPUT_PULLUP, signal values are reversed
      if ((*signal).state == LOW) {
        (*signal).hasPositiveEdge = true;
      } else {
        (*signal).hasNegativeEdge = true;
      }
    }
  }
}

void handle(inputSignal *signal) {
  (*signal).reading = digitalRead((*signal).pin);
  if (isReadingChanged(signal)) {
    updateAfterReadingChanged(signal);
  }
  debounceEdges(signal);
  indicateSendingCommandNecessaryIfNeeded(signal);
}

void sendCommandWithoutArgument(char *command_id) {
  StaticJsonDocument<64> command;
  command[command_id] = nullptr;
  // send commands to Raspberry Pi on Serial
  serializeJson(command, Serial);
}

void resendAvailabilityCommand(){
  // inverted because of INPUT_PULLUP
  if (availabilitySignal.state == LOW) {
    sendCommandWithoutArgument("InjectionMouldingMachine1_on");
  } else {  // HIGH
    sendCommandWithoutArgument("InjectionMouldingMachine1_off");
  }
  availabilitySignal.isSendingCommandNecessary = false;
  availabilitySignal.lastTimeCommandWasSentMilliseconds = millis();  
}

void handleAvailability() {
  handle(&availabilitySignal);
  if (availabilitySignal.isSendingCommandNecessary) {
    resendAvailabilityCommand();
  }
}

void handleParts() {
  handle(&rejectSignal);
  if (rejectSignal.hasPositiveEdge) {
    isReject = true;
  }

  handle(&mouldCloseSignal);
  if (mouldCloseSignal.hasNegativeEdge) {
    if (isReject) {
      sendCommandWithoutArgument("InjectionMouldingMachine1_reject_parts_completed");
    } else {
      // good parts
      sendCommandWithoutArgument("InjectionMouldingMachine1_good_parts_completed");      
    }
    isReject = false;
  }
}

void loop() {
  handleAvailability();
  handleParts();
}
