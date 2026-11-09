# ESP32-P4-NANO speaker volume test

This is an isolated hardware test project. It does not modify the AprilTag/UVC
production project.

The firmware uses the board's ES8311 codec, NS4150B power amplifier and the
2-pin speaker connector. The current build plays a near full-scale 440 Hz tone
for 10 seconds at 100% codec volume, then mutes.

Default audio wiring:

- I2C SDA: GPIO7
- I2C SCL: GPIO8
- I2S MCLK: GPIO13
- I2S BCLK: GPIO12
- I2S WS: GPIO10
- I2S DOUT: GPIO9
- I2S DIN: GPIO11
- Power-amplifier enable: GPIO53

Expected COM4 log:

```text
MAX_VOLUME_TEST level=100 percent duration=10000 ms frequency=440 Hz
MAX_VOLUME_TEST_COMPLETE: final volume muted
```
