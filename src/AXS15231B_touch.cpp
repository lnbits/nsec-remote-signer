#include "AXS15231B_touch.h"



AXS15231B_Touch* AXS15231B_Touch::instance = nullptr;



bool AXS15231B_Touch::begin() {
    instance = this;

    Serial.printf("Touch begin() - SDA:%d, SCL:%d, INT:%d, ADDR:0x%02X\n",
                  sda, scl, int_pin, addr);

    // Configure INT pin and attach interrupt
    pinMode(int_pin, INPUT_PULLUP);                   // INT is usually active-low, open-drain
    detachInterrupt(digitalPinToInterrupt(int_pin));  // just in case
    attachInterrupt(digitalPinToInterrupt(int_pin), isrTouched, CHANGE);
    Serial.printf("Touch interrupt attached to pin %d\n", int_pin);

    // Start I2C
    bool i2c_result = Wire.begin(sda, scl);
    Wire.setClock(400000);  // fast mode
    Serial.printf("I2C begin result: %s\n", i2c_result ? "SUCCESS" : "FAILED");

    // Quick I2C scan to confirm device presence/address
    Serial.println("I2C scan:");
    for (uint8_t a = 0x08; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  - found device at 0x%02X\n", a);
        }
    }

    return i2c_result;
}



ISR_PREFIX
void AXS15231B_Touch::isrTouched() {
    // This ISR gets executed if the display reports a touch interrupt
    if (instance) {
        instance->touch_int = true;
        instance->isr_count++;
    }
}

void AXS15231B_Touch::setRotation(uint8_t rot) {
    rotation = rot;
}

bool AXS15231B_Touch::touched() {
    // Check if the display is touched / got touched
    return update();
}

void AXS15231B_Touch::readData(uint16_t *x, uint16_t *y) {
    // Return the latest data points
    *x = point_X;
    *y = point_Y;
}

void AXS15231B_Touch::enOffsetCorrection(bool en) {
    // Enable offset correction
    en_offset_correction = en;
}

void AXS15231B_Touch::setOffsets(uint16_t x_real_min, uint16_t x_real_max, uint16_t x_ideal_max, uint16_t y_real_min, uint16_t y_real_max, uint16_t y_ideal_max) {
    // Offsets used for offset correction if enabled
    // Offsets should be determinded with rotation = 0
    this->x_real_min = x_real_min;
    this->x_real_max = x_real_max;
    this->y_real_min = y_real_min;
    this->y_real_max = y_real_max;
    this->x_ideal_max = x_ideal_max;
    this->y_ideal_max = y_ideal_max;
}

void AXS15231B_Touch::correctOffset(uint16_t *x, uint16_t *y) {
    // Map values to correct for offset
    *x = map(*x, x_real_min, x_real_max, 0, x_ideal_max);
    *y = map(*y, y_real_min, y_real_max, 0, y_ideal_max);
}

bool AXS15231B_Touch::update() {
    // Read when either: an INT arrived, or our 20ms poll timer fires
    static uint32_t last_poll = 0;
    const uint32_t now = millis();
    bool should_read = false;

    if (touch_int) {                // interrupt-driven
        touch_int = false;
        should_read = true;
    }
    if ((now - last_poll) >= 20) {  // polling fallback
        last_poll = now;
        should_read = true;
    }
    if (!should_read) return false;

    // --- I2C transaction ---
    uint8_t tmp_buf[8] = {0};
    static const uint8_t read_touchpad_cmd[8] =
        {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};

    Wire.beginTransmission(addr);
    Wire.write(read_touchpad_cmd, sizeof(read_touchpad_cmd));
    // use a repeated start to be friendly to some controllers
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    int got = Wire.requestFrom((int)addr, (int)sizeof(tmp_buf));
    if (got != (int)sizeof(tmp_buf)) {
        // bus glitch or no device
        return false;
    }
    for (int i = 0; i < (int)sizeof(tmp_buf); i++) {
        tmp_buf[i] = Wire.read();
    }

    // --- Parse coordinates ---
    uint16_t raw_X = AXS_GET_POINT_X(tmp_buf);
    uint16_t raw_Y = AXS_GET_POINT_Y(tmp_buf);

    // Clamp to valid ranges
    if (raw_X > x_real_max) raw_X = x_real_max;
    if (raw_X < x_real_min) raw_X = x_real_min;
    if (raw_Y > y_real_max) raw_Y = y_real_max;
    if (raw_Y < y_real_min) raw_Y = y_real_min;

    // Optional offset correction
    uint16_t x_max, y_max;
    if (en_offset_correction) {
        correctOffset(&raw_X, &raw_Y);
        x_max = x_ideal_max;
        y_max = y_ideal_max;
    } else {
        x_max = x_real_max;
        y_max = y_real_max;
    }

    // Rotate mapping
    switch (rotation) {
        case 0:
            point_X = raw_X;
            point_Y = raw_Y;
            break;
        case 1:
            point_X = raw_Y;
            point_Y = x_max - raw_X;
            break;
        case 2:
            point_X = x_max - raw_X;
            point_Y = y_max - raw_Y;
            break;
        case 3:
            point_X = y_max - raw_Y;
            point_Y = raw_X;
            break;
        default:
            break;
    }

    // Debug (comment out once verified)
    // Serial.printf("TOUCH x=%u y=%u (raw %u,%u)\n", point_X, point_Y, raw_X, raw_Y);

    return true;
}
