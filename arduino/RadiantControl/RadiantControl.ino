#include <SmartThings.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SimpleTimer.h>
#include "RadiantControl.h"

#define aref_voltage 3.3         // we tie 3.3V to ARef and measure it with a multimeter!

// Data wire is plugged into pin 8 on the Arduino
#define ONE_WIRE_BUS 5
#define PIN_THING_RX 10
#define PIN_THING_TX 2

// Mixing Valve pins
int mixValvePower = 7;  // the pin to turn on the mixing valve
int mixValve = 13;      // the pin for the mixing valve adjustment
int pumpPower = 11;     // pin to turn on circuit pump
int pumpRunningPin = 9;  // pump can be stopped because of DHW priority

//TMP36 Pin Variables
int tempPin = 1;        // the analog pin the TMP36's Vout (sense) pin is connected to
                        // the resolution is 10 mV / degree centigrade with a
                        // 500 mV offset to allow for negative temperatures

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

DeviceAddress mixTempSensor = { 0x28, 0xFF, 0x7C, 0x5A, 0x15, 0x14, 0x00, 0x6F };
DeviceAddress returnTempSensor = { 0x28, 0xFF, 0x46, 0xD2, 0x15, 0x14, 0x00, 0x93 };
SmartThingsCallout_t messageCallout;    // call out function forward decalaration
SmartThings smartthing(PIN_THING_RX, PIN_THING_TX, messageCallout, "GenericShield", false);  // constructor

// the timer object
SimpleTimer timer;

byte mixSetpoint = 120;
int mixSTaddr = 0;
int temperature = 0;
int returnTemp = 0;
int mixTemp = 0;
int mixValvePosition = MV_50PERCENT;
bool isPumpRunning = false;
bool dirty = false;
// unsigned long previousTime = 0;
// unsigned long deltaTime = 0;
// unsigned long interval = 5000;


bool isDebugEnabled;    // enable or disable debug in this example
int stateNetwork;       // state of the network

void setNetworkStateLED()
{
  SmartThingsNetworkState_t tempState = smartthing.shieldGetLastNetworkState();
  if (tempState != stateNetwork) {
    switch (tempState) {
      case STATE_NO_NETWORK:
        if (isDebugEnabled) Serial.println("NO_NETWORK");
        smartthing.shieldSetLED(2, 0, 0); // red
        break;
      case STATE_JOINING:
        if (isDebugEnabled) Serial.println("JOINING");
        smartthing.shieldSetLED(2, 0, 0); // red
        break;
      case STATE_JOINED:
        if (isDebugEnabled) Serial.println("JOINED");
        smartthing.shieldSetLED(0, 0, 2); // blue
        break;
      case STATE_JOINED_NOPARENT:
        if (isDebugEnabled) Serial.println("JOINED_NOPARENT");
        smartthing.shieldSetLED(2, 0, 2); // purple
        break;
      case STATE_LEAVING:
        if (isDebugEnabled) Serial.println("LEAVING");
        smartthing.shieldSetLED(2, 0, 0); // red
        break;
      default:
      case STATE_UNKNOWN:
        if (isDebugEnabled) Serial.println("UNKNOWN");
        smartthing.shieldSetLED(2, 0, 0); // red
        break;
    }
    stateNetwork = tempState;
  }
}

void initSetpoints()
{
  byte value = EEPROM.read(mixSTaddr);
  if (value == 0) {
    EEPROM.write(mixSTaddr, mixSetpoint);
  }
  else {
    mixSetpoint = value;
  }
  Serial.print("Mix Temp Setpoint = ");
  Serial.print(mixSetpoint);

  dirty = true;
}

void updateTemps()
{
  sensors.requestTemperatures();
  smartthing.run();
  int temp = sensors.getTempF(mixTempSensor);
  if (temp != mixTemp) {
    dirty = true;
    mixTemp = temp;
    Serial.print("Mix Temp = ");
    Serial.print(mixTemp);
    Serial.print("\n\r");
  }
  smartthing.run();
  temp = sensors.getTempF(returnTempSensor);
  if (temp != returnTemp) {
    dirty = true;
    returnTemp = temp;
    Serial.print("Return Temp = ");
    Serial.print(returnTemp);
    Serial.print("\n\r");
  }

  temp = getTmp36Temperature();
  if (temp != temperature) {
    dirty = true;
    temperature = temp;
    Serial.print("Temperature = ");
    Serial.print(temperature);
    Serial.print("\n\r");
  }

  bool running = digitalRead(pumpRunningPin) == LOW ? false : true;
  if (running != isPumpRunning) {
    dirty = true;
    isPumpRunning = running;
  }
}

int getTmp36Temperature() {
  int tempReading = analogRead(tempPin);
//  Serial.print("Temp reading = ");
//  Serial.print(tempReading);     // the raw analog reading

  // converting that reading to voltage, which is based off the reference voltage
  float voltage = tempReading * aref_voltage;
  voltage /= 1024.0;

  // print out the voltage
//  Serial.print(" - ");
//  Serial.print(voltage); Serial.println(" volts");

  // now print out the temperature
  float temperatureC = (voltage - 0.5) * 100 ;  //converting from 10 mv per degree wit 500 mV offset
                                               //to degrees ((volatge - 500mV) times 100
  // now convert to Fahrenheight
  int temperatureF = (temperatureC * 9.0 / 5.0) + 32.0;

  return temperatureF;
}

void updateSmartthings()
{
  if (stateNetwork != STATE_JOINED)
    return;

  if (dirty) {
    smartthing.send("temperature " + String(temperature));
    delay(250);
    smartthing.send("mixTemp " + String(mixTemp));
    delay(250);
    smartthing.send("returnTemp " + String(returnTemp));
    delay(250);
    smartthing.send("mixSetpoint " + String(mixSetpoint));
    delay(250);

    float tmp = ((float)mixValvePosition/(float)MV_100PERCENT) * 100.0 + 0.5;
    smartthing.send("mixValvePosition " + String((int) tmp));
    delay(250);
    smartthing.send("isPumpRunning " + String(isPumpRunning));
    delay(250);
    dirty = false;
  }
}

//*****************************************************************************
void messageCallout(String message)
{
  // if debug is enabled print out the received message
  if (isDebugEnabled) {
    Serial.print("Received message: '");
    Serial.print(message);
    Serial.println("' ");
  }

  if (message.equals("refresh")) {
    updateTemps();
    dirty = true;
  }

  if (message.startsWith("mixSetpoint")) {
    int i = message.indexOf(" ");
    String str = message.substring(i);
    mixSetpoint = str.toInt();
    if (isDebugEnabled) {
      Serial.println("Setting mixSetpoint to: " + str);
    }
  }
}

//*****************************************************************************
void setup(void)
{
  // setup default state of global variables
  isDebugEnabled = false;
  stateNetwork = STATE_JOINED;  // set to joined to keep state off if off

  if (isDebugEnabled) {
    // setup debug serial port
    Serial.begin(9600);         // setup serial with a baud rate of 9600
    while (!Serial) {
      ;   // wait for serial port to connect. Needed for Leonardo only
    }
    Serial.println("setup..");  // print out 'setup..' on start
  }

  // Start up the library
  sensors.begin();
  // set the resolution to 10 bit (good enough?)
  sensors.setResolution(mixTempSensor, 9);
  sensors.setResolution(returnTempSensor, 9);
  initSetpoints();
  updateTemps();
  timer.setInterval(5000, updateTemps);
  timer.setInterval(30000, adjustMixValve);

  // declare mixValve to be an output:
  pinMode(mixValvePower, OUTPUT);
  pinMode(mixValve, OUTPUT);
  analogWrite(mixValve, MV_50PERCENT);
  digitalWrite(mixValvePower, LOW);
  digitalWrite(pumpPower, LOW);
  pinMode(tempPin, INPUT);
  pinMode(pumpRunningPin, INPUT);

  // If you want to set the aref to something other than 5v
  analogReference(EXTERNAL);
}


void loop(void)
{
  // run smartthing logic
  smartthing.run();
  timer.run();
  setNetworkStateLED();
  updateSmartthings();
}

void adjustMixValve()
{
  if (digitalRead(pumpRunningPin) == LOW) {
    isPumpRunning = false;
    Serial.println("Pump is stopped -- don't adjust mixing valve");
    return;
  }

  isPumpRunning = true;
  Serial.println("Pump is running - adjust mixing valve");

  if (mixTemp < mixSetpoint) {
      increaseMixingValvePosition();
  }
  else if (mixTemp > mixSetpoint) {
    // lower mixing valve adjustment
    decreaseMixingValvePostion();
  }
}

void increaseMixingValvePosition()
{
  int newPos = mixValvePosition + 5;
  if (newPos > MV_100PERCENT)
    newPos = MV_100PERCENT;
  mixValvePosition = newPos;
  analogWrite(mixValve, mixValvePosition);
  dirty = true;
}

void decreaseMixingValvePostion()
{
  int newPos = mixValvePosition - 5;
  if (newPos < MV_0PERCENT)
    newPos = MV_0PERCENT;
  mixValvePosition = newPos;
  analogWrite(mixValve, mixValvePosition);
  dirty = true;
}
