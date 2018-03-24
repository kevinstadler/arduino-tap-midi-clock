#include <ButtonDebounce.h>
// tap button input - any digital input
#define TAP_PIN 14
// ignore switch/pedal bouncing up to this many miliseconds
// (this sets an upper limit to the bpm that can be tapped)
ButtonDebounce button(TAP_PIN, 50);

// all time periods are in MICRO seconds (there's 1.000.000 of those in a second)
// tap intervals longer than 3s are interpreted as onset of a new tap sequence.
// this sets a lower limit on the frequency of tapping: 20bpm
#define MAXIMUM_TAP_INTERVAL 1000L * 3000
// hold for 1s to reset tempo and stop sending clock signals
#define HOLD_RESET_DURATION 1000L * 1000

// how many taps should be remembered? the clock period will be calculated based on
// all remembered taps, i.e. it will be the average of the last TAP_MEMORY-1 periods
#define TAP_MEMORY 4
long tapTimes[TAP_MEMORY];
// counter
long timesTapped = 0;


// to be able to debug via the USB Serial interface, write the MIDI
// messages to another set of digital pins using SoftwareSerial
#include <SoftwareSerial.h>
#define MIDI_RX_PIN 2 // not actually used (should be an interruptable pin in theory)
#define MIDI_TX_PIN 10 // SoftwareSerial output port -- connect to MIDI jack pin 5 via 220 Ohm resistor
// more details on MIDI jack wiring: https://www.arduino.cc/en/uploads/Tutorial/MIDI_bb.png
SoftwareSerial Midi(MIDI_RX_PIN, MIDI_TX_PIN);

// use the PWM-compatible pins 3, 5, 6 or 11 if you want a nice logarithmic fade
// (PWM on pins 9 and 10 is blocked by the hardware timer used for the MIDI message
// interrupt, see below)
#define LED_PIN 6

// counts up to CLOCKS_PER_BEAT to control the status LED indicating the current
// tempo. volatile because it is reset to 0 (full brightness) when button is tapped
volatile int blinkCount = 0;

// use nice (base 12) logarithmic fade-out for the LED blink
const int LED_BRIGHTNESS[24] = { 255, 255, 255, 255, 255, 246, 236, 225, 213, 200, 184, 165, 142, 113, 71, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
// whenever idle, keep LED on some low brightness level to indicate pedal is on
#define READY_BRIGHTNESS 30

#include <TimerOne.h>
// MIDI requires 24 clock pulse messages per beat (quaver)
#define CLOCKS_PER_BEAT 24

// this stores the MIDI clock period, i.e. the calculated average period of the 
// tapping divided by CLOCKS_PER_BEAT
long clockPeriod;

// only start sending clocks once we've been tapped at least twice
bool clockPulseActive = false;

// helper variable that can be set by interrupts, causing the next
// iteration of loop() to do cleanup (and play a nice led animation)
volatile bool reset = false;

void setup() {
  // fire up
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // use hardware serial for debugging
  Serial.begin(9600);
  // software serial to send MIDI clocks
  Midi.begin(31250);

  // set up tap input pin and callback from ButtonDebounce library
  pinMode(TAP_PIN, INPUT_PULLUP);
  button.setCallback(tapped);

  // initialise timer - this breaks PWM (analogWrite) on pins 9+10.
  // the clock pulse really only starts sending once the interrupt callback
  // function is set in setClockPulse() below
  Timer1.initialize();
  // cause first call to loop() to play the LED animation to signal the pedal is ready
  reset = true;
}

// MIDI messages are sent by the interrupt-based timer, only need to read the
// debounced tap inputs in the loop
void loop() {
  if (reset) {
    timesTapped = 0;
    clockPulseActive = false;
    // play nice reset animation
    for (int i = 0; i <= 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(80);
      analogWrite(LED_PIN, READY_BRIGHTNESS);
      delay(80);
    }
    reset = false;
  }
  // don't let the code that starts and stops the interrupts (invoked via
  // callback by button.update()) be interrupted
  noInterrupts();
  button.update();
  interrupts();
}

// update the 
void setClockPulse() {
  clockPeriod = (tapTimes[timesTapped - 1] - tapTimes[0]) / ((timesTapped - 1) * CLOCKS_PER_BEAT);
  Serial.print("New tap period (ms): ");
  Serial.println(clockPeriod * CLOCKS_PER_BEAT / 1000);
  if (clockPulseActive) {
    Timer1.setPeriod(clockPeriod);
  } else {
    clockPulseActive = true;
    Timer1.attachInterrupt(sendClockPulse, clockPeriod);
    // syncing the onset of the arpeggiator with the actual time of the tap (rather
    // than just the tempo of the tapping) would be nice, but a midi start alone
    // sadly doesn't do the trick
    // TODO: try out the Song Position Pointer message to reset arpeggiator:
    //Midi.write(0xF2);
    //Midi.write(0x00);
    //Midi.write(0x00);
  }
}

void stopClockPulse() {
  Timer1.detachInterrupt();
  // could send midi stop as well to kill the arpeggiator
  //Midi.write(0xFC);
  // this function is called from the sendClockPulse() timer interrupt callback,
  // so delegate cleanup (and the led animation) to the main loop
  reset = true;
}

// callback for the debounced button
void tapped(int state) {
  if (!state) {
    // overengineering opportunity: could measure how long the pedal is held
    // down to adapt the duration of the blinking to how the user is tapping..?
    return;
  }
  long now = micros();
  long timeSinceLastTap = now - tapTimes[max(0, timesTapped - 1)];

  // reset led to the (bright) beginning of the blinking cycle
  blinkCount = 0;

  if (timesTapped == 0 or timeSinceLastTap > MAXIMUM_TAP_INTERVAL) {
    // new tap sequence
    tapTimes[0] = now;
    timesTapped = 1;
    if (!clockPulseActive) {
      // first tap: indicate that we're listening by turning on the led for as
      // long as we're listening
      digitalWrite(LED_PIN, HIGH);
      Timer1.attachInterrupt(stopWaiting, MAXIMUM_TAP_INTERVAL);
      // reset timer to beginning, otherwise it will be based on the counter value
      // from before attachInterrupt()
      Timer1.start();
    }
    return;

  } else if (timesTapped > 1) {
    // when the time between 2 taps is less than the maximum tap interval but much
    // different from the average of the tap sequence so far, reset it, as it
    // probably means a new tempo is being tapped
    float ratio = float(timeSinceLastTap) / (clockPeriod * CLOCKS_PER_BEAT);
    if (ratio > 1.5 or ratio < 0.5) {
      // put last of previous tap sequence as first of new one in memory
      tapTimes[0] = tapTimes[timesTapped - 1];
      timesTapped = 1;
    }
  }

  if (timesTapped < TAP_MEMORY) {
    // write to next free slot
    timesTapped++;
  } else {
    // shift memory content forward (drop earliest tap time)
    memcpy(tapTimes, &tapTimes[1], sizeof(long) * (TAP_MEMORY - 1));
  }
  tapTimes[timesTapped - 1] = now;

  // update timer interrupt to new period
  setClockPulse();
}

void stopWaiting() {
  // Timer1.attachInterrupt() doesn't wait for a full cycle of the timer but triggers
  // this callback almost immediately, so we need to check that one cycle has elapsed
  // before turning off the LED (and disconnecting the interrupt again). we divide by
  // two in the comparison to work around a wonderful heisenbug caused by the interrupt
  // actually always being triggered a bit too early unless you put a debugging
  // Serial.println() before micros() of course...
  if (micros() - tapTimes[0] >= MAXIMUM_TAP_INTERVAL / 2) {
    Timer1.detachInterrupt();
    reset = true;
  }
}

// this function is called by the Timer interrupt
void sendClockPulse() {
  // check if lastTapTime has been ages ago and, if the
  // debounced button is still HIGH, stop the clock
  if (button.state() == HIGH and micros() - tapTimes[timesTapped - 1] > HOLD_RESET_DURATION) {
    Serial.println("Reset");
    stopClockPulse();
    return;
  }

  // send MIDI clock
  Midi.write(0xF8);

  analogWrite(LED_PIN, LED_BRIGHTNESS[blinkCount]);
  blinkCount = (blinkCount + 1) % CLOCKS_PER_BEAT;
}

