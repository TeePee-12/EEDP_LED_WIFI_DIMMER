// UNSW Canberra S2, 2022, ZEIT2209, Principles of Electrical Engineering
// The Group Formerly Known as Prince LED Subsystem code for Lolin D-32 board
// Author: z5349517 Thomas Phelan
//
/************************Copyright Declaration******************************/
// This code is a a modified version of the example Adafruit feed handler code:
// Written by Todd Treece for Adafruit Industries
// Modified by Brent Rubell for Adafruit Industries
// Copyright (c) 2020 Adafruit Industries
// Licensed under the MIT license.
/************************ Code Starts Here *******************************/
#include "config.h"
#define PWR_ON 16          // 3v3 Supply pin to turn on the Op-amp
#define BRIGHTNESS 4       // PWM controlled power to switch on the LED
#define BUTT_BRIGHT_UP 19  // Signal to increase LED brightness
#define BUTT_BRIGHT_DN 21  // Signal to increase LED brightness
#define BUTT_ON_OFF 18     // Signal to turn the light on/off

int const BUTT_DEBOUNCE_TIME = 100;     // Timer for the debouncing of buttons
int const TRANSMIT_TIME = 10000;        // Timer for transmitting to the Adafruit Feed to prevent throttling
int const MAX_BRIGHTNESS = 255;         // Maximum brightness level (PWM max)
int const MIN_BRIGHTNESS = 0;           // Minimum Brihgtness Level (PWM min)
int const INITIAL_BRIGHTNESS = 100;     // Brightness (PWM) level wheneve the system is turned on
int const POWER_SAVER_TIMEOUT = 30000;  // After this many milliseconds of control inactivty the light will power off

unsigned long db_time = 0;            // timer variable for the debounce timer, resets at each button push, used by multiple interrupt service routines
unsigned long timeout_timer = 0;      // timer variable for the power saving timeout, resets to the current time whenever there is a brightness or state update
int brightness = INITIAL_BRIGHTNESS;  // brightness level, 8 bit value for PWM 0-255, used by multiple interrup service routines and message feed handers
bool on_state = true;                 // Overall state of the light, on or off, used by multiple interrupt service routines and message feed handelrs

AdafruitIO_Feed *LED = io.feed("LED");                          // set up the led dimmer brightness feed
AdafruitIO_Feed *wifi_motorState = io.feed("wifi_motorState");  // set up the door state feed
AdafruitIO_Feed *Security_System = io.feed("Security_System");  // set up the Security system feed

//This fucntion performs software debouncing of the physical local buttons on the subsystem board
boolean debouncer() {
  unsigned long now_time = millis();
  if ((now_time - db_time) < BUTT_DEBOUNCE_TIME) {
    return false;
  } else {
    db_time = millis();
    return true;
  }
}

//ISR triggered by push button to turn the light on/off
void IRAM_ATTR pwr_cycle() {
  noInterrupts();
  if (debouncer()) {
    if (!on_state) {
      brightness = INITIAL_BRIGHTNESS;
    } else {
      brightness = 0;
    }
    on_state = !on_state;
  }
}

//ISR triggered by push button to turn the light up
void IRAM_ATTR brightness_up() {
  noInterrupts();
  if (debouncer()) {
    if (!on_state) {
      brightness = 1;
      on_state = true;
    } else {
      int new_brightness = brightness + 1 + (brightness * 0.5);
      if (new_brightness > MAX_BRIGHTNESS) {
        brightness = MAX_BRIGHTNESS;
      } else {
        brightness = new_brightness;
      }
    }
  }
}

//ISR triggered by push button to turn the light down
void IRAM_ATTR brightness_down() {
  noInterrupts();
  if (debouncer()) {
    if (brightness == MIN_BRIGHTNESS) {
      on_state = false;
    } else {
      int new_brightness = brightness - (brightness * .5);
      if (new_brightness < MIN_BRIGHTNESS) {
        brightness = MIN_BRIGHTNESS;
      } else {
        brightness = new_brightness;
      }
    }
  }
}

// Message handler for interfacing with the Adafruit IO LED brightness feed
// The feed is an 8-bit number 0-255, should always be the same as the local brightness variable for the PWM
void handleMessageLED(AdafruitIO_Data *LED) {
  noInterrupts();
  if (LED->toInt() != brightness) {
    brightness = LED->toInt();
    //if feed brightness exceeds max level, correct the feed to max level
    if (brightness > MAX_BRIGHTNESS) {
      brightness = MAX_BRIGHTNESS;
    }
    //if feed brightness is below min level, correct the feed to the min level
    if (brightness <= MIN_BRIGHTNESS) {
      brightness = MIN_BRIGHTNESS;
      on_state = false;
    } else {
      on_state = true;
    }
  }
}


// Message handler for interfacing with the Adafruit IO LED door state
// The light should turn on when the door opens if it is not already on
// If the light is already on, it should maintain the current brightness (do nothing)
void handleMessageDoor(AdafruitIO_Data *wifi_motorState) {
  noInterrupts();
  int const LED_ON_DOOR_STATE = 1;
  if (wifi_motorState->toInt() == LED_ON_DOOR_STATE) {
    timeout_timer = millis();
    if (!on_state) {
      on_state = true;
      brightness = INITIAL_BRIGHTNESS;
    } else {
      return;
    }
  }
}

// Message handler so that the lighting system responds to changes in the Security System status
// Status 1 =  system armed, light should be off
// Status 2 =  alarm tripped, light should turn on at max brightness
void handleMessageSecurity(AdafruitIO_Data *Security_System) {
  noInterrupts();
  int const ARMED = 1;  //Adafruit IO Feed integer level definitions
  int const ALARM = 2;  //Adafruit IO Feed integer level definitions

  if (Security_System->toInt() == ARMED) {  //Turn the light off upon arming of the security system
    on_state = false;
    brightness = 0;
  } else if (Security_System->toInt() == ALARM) {  // Turn the light on at max brightness upon alarm activation
    timeout_timer = millis();
    on_state = true;
    brightness = MAX_BRIGHTNESS;
  } else {
    return;
  }
}

void setup() {

  pinMode(PWR_ON, OUTPUT);                // 3v3 Supply for the driver
  pinMode(BUILTIN_LED, OUTPUT);           // Lolin D-32 blue onboard LED
  digitalWrite(BUILTIN_LED, HIGH);        //Master on/off state for the high power LED current driver
  pinMode(BUTT_ON_OFF, INPUT_PULLUP);     // Local control On/Off button
  pinMode(BUTT_BRIGHT_UP, INPUT_PULLUP);  // Local control brightness up button
  pinMode(BUTT_BRIGHT_DN, INPUT_PULLUP);  // Local control brightness down button
  pinMode(BRIGHTNESS, OUTPUT);            // PWM output pin for brightness control

  attachInterrupt(BUTT_ON_OFF, pwr_cycle, FALLING);
  attachInterrupt(BUTT_BRIGHT_UP, brightness_up, FALLING);
  attachInterrupt(BUTT_BRIGHT_DN, brightness_down, FALLING);

  Serial.begin(115200);
  io.connect();                           //Adafruit IO connection
  while (io.status() < AIO_CONNECTED) {}  // Wait here for an active connection to Adafruit IO API before continuing
  digitalWrite(BUILTIN_LED, LOW);         // D-32 blue LED should remain ON while we have an active connection to the Adafruit IO API

  // Here we attach the Adafruit IO message handlers for the feeds we are using in this module
  LED->onMessage(handleMessageLED);
  wifi_motorState->onMessage(handleMessageDoor);
  Security_System->onMessage(handleMessageSecurity);
  LED->get();
  wifi_motorState->get();
  Security_System->get();
  LED->save(INITIAL_BRIGHTNESS);
}

void loop() {
  static unsigned long transmit_time = 0;  // timer variable for message transmission to prevent flooding the feeds and throttling conenction
  static int previous_brightness = 0;
  static int previously_sent_brightness = 0;

  // if connection lost: turn off D-32 blue LED and attempt to re-establish connection
  if (io.status() < AIO_CONNECTED) {
    digitalWrite(BUILTIN_LED, HIGH);
    io.connect();
  }

  digitalWrite(BUILTIN_LED, LOW);  // D-32 blue LED should remain ON while we have an active connection to the Adafruit IO API

  io.run();
  interrupts();
  Serial.println(brightness);  // Used for debugging purposes
  Serial.println(on_state);    // Used for debugging purposes
  unsigned long now_time = millis();

  //Used for power saving timeout function, each time the brightnes changes reset the timeout timer
  if (previous_brightness != brightness) {
    previous_brightness = brightness;
    timeout_timer = millis();
  }
  // Turn the lighting system off after a predefined amount of time of inactivity to save power
  if ((now_time - timeout_timer) > POWER_SAVER_TIMEOUT) {
    if (brightness > MIN_BRIGHTNESS) {
      brightness = MIN_BRIGHTNESS;
      on_state = false;
      LED->save(brightness);
    }
  }

  // Save the actual brightness to the IO feed at regular intervals to prevent throttling the feed
  // Only update the feed if the brightness has changed since the last feed save
  if ((now_time - transmit_time) > TRANSMIT_TIME) {
    if (previously_sent_brightness != brightness) {
      LED->save(brightness);
      previously_sent_brightness = brightness;
      transmit_time = millis();
    }
  }

  // Here the code that actually changes what the hardware is doing starts,
  // This is where the actual pin outputs are written to turn the master ON/OFF and control PWM for LED brightness
  if (on_state) {
    digitalWrite(PWR_ON, HIGH);
    analogWrite(BRIGHTNESS, brightness);
  } else {
    digitalWrite(PWR_ON, LOW);
    analogWrite(BRIGHTNESS, 0);
  }
}