# T-Stick Interface

This module will interface with the T Stick digital musical instrument over OSC. Set up the module to receive OSC
messages from the T-Stick. A Protokol output of a sample T-Stick stream of messages is included in protokol.txt.

The module should have the following outputs:

- squeeze: the values in /TStick_581/instrument/squeeze, mapped from 0-1 to 0-10V
- Touch Gate: 10V if /TStick_581/instrument/touch/all value >0, otherwise 0V
- Touch CV low, Touch CV high: The lowest and highest indexes of nonzero values in /TStick_581/instrument/touch/discrete, 
remapped to a scale 1V/oct scale (major, minor, major pentatonic, minor pentatonic, chromatic). Create a knob for scale selection.
- Angular change (4 outputs, pitch, yaw, roll, total). The /TStick_581/ypr output has raw radian values of yaw, pitch, and
roll for the device. However, in a performance context, these have the issue of jumping sharply between +pi to -pi. For this output,
output the amount of change since Touch Gate last went high. These angles should be unwrapped, to avoid these sudden jumps.
Rescale pi/2 radians to 1V. The total should give the total distance of rotation.

There should be a toggle button to enable/disable listening for the OSC messages.

In the context menu, there should be options for changing the port and IP to listen to for the device.