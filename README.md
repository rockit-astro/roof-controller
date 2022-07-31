# SuperWASP Roof Controller
Firmware for an Arduino Micro that controls the SuperWASP roof.

An Arduino micro is connected to to the buffer/relay board inside the Roof Controller Interface box, replacing the original CSIMC interface.

The wiring of the original interface box has been reconfigured:
* The radio alarm board has been removed, and its relay channel repurposed for a pre-movement siren (12v).
* The Battery relay channel (which actually controlled the main pump) has been disconnected, and the wires repurposed to directly measure the battery voltage.
* The 5v output originally used for the radio alarm is now wired directly to power the Arduino.
* The CSIMC connector on the front panel has been replaced with wires to the Arduino pins, as follows:

| Pin Label | Port | Direction | Function |
| --------- | ---- | --------- | -------- |
| A5        | PF0  | Output    | Activates the Open relay when pulled low. |
| A4        | PF1  | Output    | Activates the Close relay when pulled low. |
| A3        | PF4  | Output    | Activates the Alarm relay when pulled low. |
| A2        | PF5  | Output    | Activates the Aux relay when pulled low. |
| A1        | PF6  | Input     | Pulls low when the Opened limit switch is triggered. |
| A0        | PF7  | Input     | Pulls low when the Closed limit switch is triggered. |

See the photos in the docs directory for more details on the hardware modifications.

### Compilation/Installation

Requires a working `avr-gcc` installation.
* On macOS add the `osx-cross/avr` Homebrew tap then install the `avr-gcc`, `avr-libc`, `avr-binutils`, `avrdude` packages.
* On Ubuntu install the `gcc-avr`, `avr-libc`, `binutils-avr`, `avrdude` packages.
* On Windows WinAVR should work.

Compile using `make`. Press the reset button on the Arduino once or twice (depending on bootloader version) to reboot into the bootloader and run `make install` in the first couple of seconds (while the LED is fading in and out rather thank blinking).

### Software Protocol

Connect to USB port, 9600 8N1 Bps/Par/Bits, no hardware or software flow control.

The unit will passively report its status once per second, in the form `S,HHH,+VV.VV\r\n`

* `S` is a status byte:
   | Value | Meaning                                                                 |
   | ----- | ----------------------------------------------------------------------- |
   | 0     | Partially open (neither Opened nor Closed limit switches are triggered) |
   | 1     | Closed (Closed limit switch is triggered)                               |
   | 2     | Open (Opened limit switch is triggered)                                 |
   | 3     | Closing (Close relay is activated)                                      |
   | 4     | Opening (Open relay is activated)                                       |
   | 5     | Force Closing (Close relay is activated due to heartbeat timeout)       |
   | 6     | Force Closed (Closed limit switch is triggered, heartbeat timed out)    |

* `HHH` is the number of seconds (0-240) remaining until the heartbeat times out and force-closes the roof.
* `+VV.VV` is the voltage (positive or negative) of the roof battery.

The unit can be controlled by sending a single status byte:

   | Value       | Action                                |
   | ----------- | ------------------------------------- |
   | 0x01 - 0xF0 | Set heartbeat timer to 1-240 seconds  |
   | 0x00        | Disable heartbeat timer               |
   | 0xF1        | Open the roof                         |
   | 0xF2        | Close the roof                        |
   | 0xF3        | Close the roof with AUX motor enabled |
   | 0xFE        | Enable the siren for 5 seconds        |
   | 0xFF        | Stop roof movement and siren          |

### Configuration

The maximum runtimes for open/close, and the length of the siren alarm can be configured by changing the lines near the top of the Makefile and reflashing the Arduino.