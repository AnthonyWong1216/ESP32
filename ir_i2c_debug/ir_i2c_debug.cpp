// 10 kHz is deliberately slow: it works better with only ESP32 internal pull-ups.
static constexpr uint32_t I2C_CLOCK_HZ = 10000;