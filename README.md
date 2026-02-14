# Arduino Project to Sync USB MIDI to Pre MIDI Drum Machines
### Tested and Working with Leornardo and Micro Pro Boards
If uploaded to an ATMega32u4 board (Ardino boards with built in USB controller)
This will appear as native USB device and will send sync clock signals (square waves)
compatible with pre MIDI drum machines.

It has been tested as working against a LinnDrum and by default generates 48 Pulses Per Quarter Note (ppqn)

These pulses appear on Pin 7, and it is suggested that a small resistor be put in seires with the output (470R)

PPQN supported are 24 (Roland), 48 (Korg, Linn, Sequential), 96 (Oberheim)
Change 	
`gClockSetting=SYNC_48;`
to either
`gClockSetting=SYNC_24;` or `gClockSetting=SYNC_96;`





