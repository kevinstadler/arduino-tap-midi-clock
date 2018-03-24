## arduino tap midi clock

This sketch implements a [MIDI beat clock](https://en.wikipedia.org/wiki/MIDI_beat_clock) driver which can be controlled by tapping a button or pedal (such as a digital keyboard sustain pedal) connected to the Arduino. Holding the button/pedal down for 1 second stops the beat messages until a new tempo is tapped in.

The default pin setting I used for building a compact unit out of an Arduino Nano is as follows:

* External power supply (5-20V) to pins `VIN` and `GND`
* Button or pedal to pins `GND` and `14` aka `A0` (used in `INPUT_PULLUP` mode)
* MIDI DIN connector pin 2 to Arduino `GND`
* MIDI DIN connector pin 4 to Arduino pin `5V` *through a 220 Ohm resistor*
* MIDI DIN connector pin 5 to Arduino pin `10` *through a 220 Ohm resistor*
* Status LED to Arduino pin `6` (through a suitable resistor)

<img src="https://raw.githubusercontent.com/kevinstadler/arduino-tap-midi-clock/master/arduino-tap-midi-clock.jpg" alt="Arduino Tap Midi Clock hardware" title="Arduino Tap Midi Clock hardware" align="center" width="100%" />

### Dependencies

This sketch requires two libraries, both available for download from within Arduino:

* [ButtonDebounce](https://github.com/maykon/ButtonDebounce)
* [TimerOne](https://github.com/PaulStoffregen/TimerOne)
