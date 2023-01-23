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

// pins
const byte availabilityPin = 2;
const byte goodPartPin = 3;
const byte rejectPartPin = 4;

// structs
struct inputSignal {
  byte pin;
  bool isSendingCommandNecessary = true;
  bool reading = LOW;
  bool lastState = LOW;
  unsigned long lastChangeTimeMilliseconds = millis();
  unsigned long lastTimeCommandWasSentMilliseconds = millis();
};

// initiating global variables
inputSignal availabilitySignal;
inputSignal goodPartSignal;
inputSignal rejectPartSignal;

// functions
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

bool isStateChanged(inputSignal *signal) {
  if ((*signal).lastState != (*signal).reading)
    return 1;
  else
    return 0;
}

void update(inputSignal *signal) {
  (*signal).lastState = (*signal).reading;
  (*signal).lastChangeTimeMilliseconds = millis();
  (*signal).isSendingCommandNecessary = true;
}

void sendCommandWithoutArgument(char *command_id) {
  StaticJsonDocument<64> command;
  command[command_id] = nullptr;
  // send commands to Raspberry Pi on Serial
  serializeJson(command, Serial);
}

bool isResendCommandDue(inputSignal *signal) {
  if (getTimeSinceLastChange((*signal).lastTimeCommandWasSentMilliseconds) > RESEND_AVAILABILITY_STATUS_PERIOD_MILLISECONDS) {
    return 1;
  } else {
    return 0;
  }
}

void indicateSendingCommandNecessaryIfNeeded(inputSignal *signal) {
  if (isResendCommandDue(signal)) {
    (*signal).isSendingCommandNecessary = true;
  }
}

void resendAvailabilityCommand(){
  if (availabilitySignal.lastState == LOW) {
    // Injection moulder automatic
    sendCommandWithoutArgument("InjectionMouldingMachine1_on");
  } else {  // HIGH
    // Injection moulder not automatic
    sendCommandWithoutArgument("InjectionMouldingMachine1_off");
  }
}

void setup() {
  availabilitySignal.pin = availabilityPin;
  availabilitySignal.lastState = HIGH;
  availabilitySignal.reading = HIGH;
  pinMode(availabilitySignal.pin, INPUT_PULLUP);

  goodPartSignal.pin = goodPartPin;
  goodPartSignal.lastState = HIGH;
  goodPartSignal.reading = HIGH;
  pinMode(goodPartSignal.pin, INPUT_PULLUP);

  rejectPartSignal.pin = rejectPartPin;
  rejectPartSignal.lastState = HIGH;
  rejectPartSignal.reading = HIGH;
  pinMode(rejectPartSignal.pin, INPUT_PULLUP);

  // initialize serial communication:
  Serial.begin(9600);
}

void handleAvailability() {
  availabilitySignal.reading = digitalRead(availabilitySignal.pin);
  if (isStateChanged(&availabilitySignal)) {
    update(&availabilitySignal);
  } else {
    // availability signal unchanged since last loop
    indicateSendingCommandNecessaryIfNeeded(&availabilitySignal);
    if (isStableLongerThan(&availabilitySignal, DEBOUNCE_DELAY_MILLISECONDS)
        && availabilitySignal.isSendingCommandNecessary) {
      resendAvailabilityCommand();
      availabilitySignal.isSendingCommandNecessary = false;
      availabilitySignal.lastTimeCommandWasSentMilliseconds = millis();
    }
  }
}

void handlePartSignal(inputSignal *signal, char *messageAtPositiveEdge) {
  (*signal).reading = digitalRead((*signal).pin);
  if (isStateChanged(signal)) {
    update(signal);
  } else {
    // signal is unchanged since last loop
    if (isStableLongerThan(signal, DEBOUNCE_DELAY_MILLISECONDS)
        && (*signal).lastState == LOW
        && (*signal).isSendingCommandNecessary) {
      sendCommandWithoutArgument(messageAtPositiveEdge);
      (*signal).isSendingCommandNecessary = false;
    }
  }
}

void handleGoodParts() {
  handlePartSignal(&goodPartSignal, "InjectionMouldingMachine1_good_parts_completed");
}

void handleRejectParts() {
  handlePartSignal(&rejectPartSignal, "InjectionMouldingMachine1_reject_parts_completed");
}

void loop() {
  handleAvailability();
  handleGoodParts();
  handleRejectParts();
}
