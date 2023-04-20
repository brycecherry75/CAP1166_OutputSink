/*

  CAP1166 output sink by Bryce Cherry
  Used for converting the touch controls on a Lenovo Pro2840m display for conversion with a 4K HDR TV board having a V-by-One interface for the LCD panel and open collector pins for the pushbutton controls
  Based on a script (c) 2018-2019 Karl-Henrik Henriksson - breakouts*xoblite.net - http://breakouts.xoblite.net/

  CAUTION: The CAP1166 is a +3.3V only device with +5V tolerant communication (I2C/SPI/ALERT) pins and levels on these pins must not exceed +3.6V with respect to Vcc including when Vcc is removed; otherwise, damage may occur

*/

#include <PinsToBus.h> // obtain at http://github.com/brycecherry75/PinsToBus - version 1.1.0 or later which has required open collector operation
#include <Wire.h> // Uno and derivatives: A4 is SDA, A5 is SCL - pull up with 2K2 resistors

#define CAP1166_Address 0x2B // in a Lenovo Pro2840m, ADDR_COMM pin has a 100K pulldown which sets I2C/SMBus mode with address set to to 0x2B as per CAP1166 datasheet
const byte AlertPin = 2; // is latched on interrupt and is reset on disabling interrupt - pull up with 2K2 resistor
const byte OutputPinCount = 6; // CAP1166 has 6 touch inputs
const byte OutputPins[OutputPinCount] PROGMEM = {4, 5, 6, 7, 8, 9}; // for Lenovo Pro2840m: Power/Menu-Enter/Right/Left/Input/Unused - Volume +/- are mechanical switches which short to GND when pressed
const byte IdleTimeout = 5; // seconds
const byte DebounceDelay = 100; // mS - take into account the human senses
const word I2Ctimeout = 50000; // uS
// corresponds to element position in OutputPins - for Lenovo Pro2840m which has a combined Menu/Enter kay
const byte MenuEnterKey = 1;
const byte MenuOutKey = 1; // Menu-Enter position
const byte EnterOutKey = 5; // unused position
const byte MenuKeyPressedTime = 2; // seconds; otherwise, Enter key is pressed

// adjust for your touch controls - Gain = 3/BaseShift = 15/SenstivityMultiplier = 0 works for Lenovo Pro2840m
const byte Gain = 3; // 0-3 for an integer power of 2 - normally 0
const byte BaseShift = 15; // 0-15 for an integer power of 2 - normally 15
const byte SensitivityMultiplier = 0; // 0-7 corresponding to an inverse integer power of 2 e.g. for a Sensitivity of 2, (7 - SensitivityMultiplier) ^ 2 = 32 - normally 2

void WriteRegister(byte address, byte value) {
  Wire.beginTransmission(CAP1166_Address);
  Wire.write(address);
  Wire.write(value);
  Wire.endTransmission(true);
}

byte ReadRegister(byte address) {
  Wire.beginTransmission(CAP1166_Address);
  Wire.write(address);
  Wire.endTransmission(true);
  Wire.requestFrom(CAP1166_Address, 1, true);
  word StartTime = micros();
  bool I2Cfail = false;
  while (true) {
    if (Wire.available() > 0) {
      break;
    }
    if ((micros() - StartTime) > I2Ctimeout) {
      I2Cfail = true;
      break;
    }
  }
  if (I2Cfail == false) {
    return Wire.read();
  }
  else {
    return 0x00;
  }
}

void setup() {
  Wire.begin();
  pinMode(AlertPin, INPUT_PULLUP);
  PinsToBus.writeByte_OC(OutputPins, OutputPinCount, true, true, 0xFF);
  WriteRegister(0x00, (0b00000000 | (Gain << 6))); // Main Control -> Default (active state == power on == no standby, gain 1x), reset /ALERT pin, clear touch memory for sensors not activated
  WriteRegister(0x1F, (0b00000000 | BaseShift | (SensitivityMultiplier << 4))); // Sensitivity control -> Default (32x multiplier)
  WriteRegister(0x20, 0b00100000); // Configuration #1 -> Default
  WriteRegister(0x44, 0b01000000); // Configuration #2 -> Default
  WriteRegister(0x21, 0b00111111); // Sensor Input Enable -> Default (all touch sensors enabled)
  WriteRegister(0x22, 0b10100100); // Sensor Input Configuration #1 -> Default
  WriteRegister(0x23, 0b00001111); // Sensor Input Configuration #2 -> Max amount of time before "press and hold" detected (we don't use it)
  WriteRegister(0x24, 0b00111001); // Averaging and Sampling Configuration -> Default
  WriteRegister(0x27, 0b00111111); // Interrupt Enable -> Enable interrupts from all touch sensors
  WriteRegister(0x28, 0b00000000); // Repeat Rate Enable -> Disable all repeat rates
  WriteRegister(0x2A, 0b10000000); // Multiple Touch Config -> Default (disable multitouch)
  WriteRegister(0x2B, 0b00000000); // Multiple Touch Pattern Config -> Default
  WriteRegister(0x2D, 0b00111111); // Multiple Touch Pattern -> Default
  WriteRegister(0x2F, 0b10001010); // Recalibration Configuration -> Default

  WriteRegister(0x74, 0b00111111); // Turn on all 6 button LEDs
  WriteRegister(0x72, 0b00000000); // Sensor Input LED Linking -> No LEDs linked to their inputs (nb. the actual inputs and LEDs on the Touch pHAT seem to be swapped, so we can't use this anyway)
  WriteRegister(0x77, 0b00000000); // Linked LED Transition Control -> Default
  WriteRegister(0x81, 0b00000000); // LED 4-1 behaviour (2 bits each) -> Programmed active/inactive
  WriteRegister(0x82, 0b00000000); // LED 6-5 behavious (2 bits each) -> Programmed active/inactive
  WriteRegister(0x93, 0b00000000); // LED Direct Duty Cycle (brightness) -> 7% (minimum possible dimming)

  WriteRegister(0x26, 0b00111111); // Calibration Activate -> Recalibrate all sensor inputs CS1-CS6 (takes up to 600 msec but this is not an issue for us)

}

void loop() {
  if (digitalRead(AlertPin) == LOW) {
    WriteRegister(0x93, 0b11110000); // LED Direct Duty Cycle (brightness) -> 100%
    byte touchStatus = ReadRegister(0x03); // Check *which* button was touched... (note: flipped bit order compared to physical order left-right on Enviro pHAT)
    byte KeysTouched = 0;
    byte mask = 0b00000001;
    byte KeyTouched = 255;
    for (byte BitsToCheck = 0; BitsToCheck < OutputPinCount; BitsToCheck++) {
      if ((touchStatus & mask) != 0) {
        KeyTouched = BitsToCheck;
        KeysTouched++;
      }
      mask <<= 1;
    }
    if (KeysTouched == 1) {

      // for Lenovo Pro2840m which has a combined Menu/Enter key
      if (KeyTouched == MenuEnterKey) {
        bool MenuKey = true;
        bool OtherKeyPressed = false;
        for (int i = 0; i < (1000UL * MenuKeyPressedTime); i++) {
          WriteRegister(0x00, (0b00000000 | (Gain << 6))); // Main Control -> Default (active state == power on == no standby, gain 1x), reset /ALERT pin, clear touch memory for sensors not activated
          delayMicroseconds(1000);
          KeyTouched = ReadRegister(0x03);
          if ((KeyTouched & ~(1 << MenuEnterKey)) != 0x00) {
            OtherKeyPressed = true;
            break;
          }
          if (KeyTouched == 0x00) {
            MenuKey = false;
            break;
          }
        }
        if (OtherKeyPressed == false) {
          if (MenuKey == true) {
            PinsToBus.writeByte_OC(OutputPins, OutputPinCount, false, true, (~(1 << MenuOutKey)));
          }
          else {
            PinsToBus.writeByte_OC(OutputPins, OutputPinCount, false, true, (~(1 << EnterOutKey)));
          }
          delay(DebounceDelay);
        }
      }
      else {
        PinsToBus.writeByte_OC(OutputPins, OutputPinCount, false, true, (~touchStatus));
      }
      // end of Lenovo Pro2840m specific section

    }
    while (true) { // wait for key release
      WriteRegister(0x00, (0b00000000 | (Gain << 6))); // Main Control -> Default (active state == power on == no standby, gain 1x), reset /ALERT pin, clear touch memory for sensors not activated
      if (ReadRegister(0x03) == 0x00) {
        delay(DebounceDelay);
        if (ReadRegister(0x03) == 0x00) {
          break;
        }
      }
    }
    PinsToBus.writeByte_OC(OutputPins, OutputPinCount, false, true, 0xFF);
    for (int i = 0; i < (1000UL * IdleTimeout); i++) {
      if (digitalRead(AlertPin) == LOW) {
        break;
      }
      delayMicroseconds(1000);
    }
    if (digitalRead(AlertPin) == HIGH) {
      WriteRegister(0x93, 0b00000000); // LED Direct Duty Cycle (brightness) -> 7% (minimum possible dimming)
    }
  }
}
