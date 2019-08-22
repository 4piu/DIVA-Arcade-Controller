#pragma GCC optimize ("-Ofast")
#pragma GCC push_options

#include <SPI.h>

//SPI for ATMEGA328P !CANNOT CHANGE!
#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_SCK 13

//Pin of LEDs & touch sensors
#define PIN_SENS_LOAD 2 // parallel read in, active low
#define PIN_SENS_CE 3   // clock enable pin, active low
#define PIN_LED_LOAD 4  // latch register data, rising edge triggered
#define PIN_L1 5
#define PIN_R1 6

//Pin of the analog stick control
#define PIN_LEFT_STICK_X_0 A0
#define PIN_LEFT_STICK_X_1 A1
#define PIN_LEFT_STICK_X_2 A2
#define PIN_RIGHT_STICK_X_0 A3
#define PIN_RIGHT_STICK_X_1 A4
#define PIN_RIGHT_STICK_X_2 A5

// Configurations
const uint8_t TAP_RELEASE = 50;  // ms
const uint8_t LED_AMOUNT = 40;
const uint8_t LED_BRIGHTNESS = 0x0F; // from 0x00 to 0x0F
const uint8_t SENSOR_AMOUNT = 40;
const uint8_t MOVE_TOLERANCE = 1;
const uint8_t EDGE_AREA = 6;

enum Gesture {
    NOT_PRESENT, MOVING_LEFT, MOVING_RIGHT, HOLDING, TAPPED
};

// Global variables
SPISettings sensor_settings(16000000, MSBFIRST, SPI_MODE0); //16MHz max, Din at rising edge
SPISettings led_settings(10000000, MSBFIRST, SPI_MODE0);  //10MHz max, Din at rising edge
uint8_t sensor_data[5];
bool L1_pressed = false;
bool R1_pressed = false;
uint32_t L1_press_time;
uint32_t R1_press_time;
uint8_t touch_position[2] = {0, 0};
enum Gesture motion[2] = {NOT_PRESENT, NOT_PRESENT};

void setup() {
    pinMode(PIN_SENS_LOAD, OUTPUT);
    pinMode(PIN_SENS_CE, OUTPUT);
    pinMode(PIN_LED_LOAD, OUTPUT);
    pinMode(PIN_L1, OUTPUT);
    pinMode(PIN_R1, OUTPUT);
    pinMode(PIN_LEFT_STICK_X_0, OUTPUT);
    pinMode(PIN_LEFT_STICK_X_1, OUTPUT);
    pinMode(PIN_LEFT_STICK_X_2, OUTPUT);
    pinMode(PIN_RIGHT_STICK_X_0, OUTPUT);
    pinMode(PIN_RIGHT_STICK_X_1, OUTPUT);
    pinMode(PIN_RIGHT_STICK_X_2, OUTPUT);
    initLED();
    bootAnimation();
}

void loop() {
    readTouchSensor();
    updateTouchPosition();
    if (calculateGesture()) {
        setControl();
        setLED();
    }
    // Release LR press
    if (L1_pressed || R1_pressed) {
        uint32_t time_millis = millis();
        if (L1_pressed && (time_millis - L1_press_time > TAP_RELEASE)) {
            digitalWrite(PIN_L1, LOW);
            L1_pressed = false;
        }
        if (R1_pressed && (time_millis - R1_press_time > TAP_RELEASE)) {
            digitalWrite(PIN_R1, LOW);
            R1_pressed = false;
        }
    }
}

void readTouchSensor() {
    digitalWrite(PIN_SENS_LOAD, LOW);
    asm("nop\n nop\n");
    digitalWrite(PIN_SENS_LOAD, HIGH);

    SPI.beginTransaction(sensor_settings);
    digitalWrite(PIN_SENS_CE, LOW);
    //    asm("nop\n nop\n");
    for (uint8_t i = 0; i < 5; i++) {
        sensor_data[i] = SPI.transfer(0);
    }
    digitalWrite(PIN_SENS_CE, HIGH);
    SPI.endTransaction();
}

void updateTouchPosition() {
    bool expect_ending = false;
    uint8_t point_index = 0;
    //Reset touch position
    uint8_t touch_position[2] = {0, 0};

    for (uint8_t i = 0; i < SENSOR_AMOUNT; i++) {
        if (point_index > 1) break;
        bool isTouching = sensor_data[i / 8] >> i % 8 & true;
        if (isTouching && !expect_ending) {
            touch_position[point_index] = i + 1;
            expect_ending = true;
        } else if (!isTouching && expect_ending) {
            touch_position[point_index] += i;
            expect_ending = false;
            point_index++;
        }
        if (i == (SENSOR_AMOUNT - 1) && isTouching) {
            touch_position[point_index] += SENSOR_AMOUNT;
            //expect_ending = 0;
            //point_index++;
        }
    }
}

bool calculateGesture() {
    static uint8_t touch_position_prev[2] = {0, 0};
    static enum AutoSwap {
        DEACTIVATED, MODE_APPEAR, MODE_DISAPPEAR
    } auto_swap = DEACTIVATED;
    bool state_changed = 0;

    if (touch_position_prev[0] != 0 &&
            touch_position_prev[1] == 0 &&
            touch_position[1] != 0 &&
            abs(touch_position[1] - touch_position_prev[0]) <= 2) {    //Left point present after the right point
        auto_swap = MODE_APPEAR;
    } else if (touch_position_prev[0] != 0 &&
               touch_position_prev[1] != 0 &&
               touch_position[1] == 0 &&
               abs(touch_position[0] - touch_position_prev[1]) <= 2 &&
               motion[0] != HOLDING) { //Left point leaves before the right point
        auto_swap = MODE_DISAPPEAR;
    }

    if (auto_swap == MODE_DISAPPEAR) {
        if (touch_position[0] == 0 || touch_position[1] != 0) {  //Exit condition: number of the point changed
            auto_swap = DEACTIVATED;
        } else {
            //Swap the position of the points
            touch_position[1] = touch_position[0];
            touch_position[0] = 0;
        }
    } else if (auto_swap == MODE_APPEAR) {
        //Swap position record and motion of the two points
        touch_position_prev[1] = touch_position_prev[0];
        touch_position_prev[0] = 0;
        motion[1] = motion[0];
        motion[0] = NOT_PRESENT;
        auto_swap = DEACTIVATED;   //One time job
    }

    for (uint8_t i = 0; i < 2; i++) {
        if (touch_position[i] == touch_position_prev[i]) {
            //Moved=0;
            if (touch_position[i] == 0) {
                //Moved=0; Touched=0;
                if (motion[i] == TAPPED) {
                    //Moved=0; Touched=0; PreviousTap=1;
                    motion[i] = NOT_PRESENT;
                    state_changed = 1;
                } else {
                    //Moved=0; Touched=0; PreviousTap=0;
                    motion[i] = NOT_PRESENT;
                }
            } else if (motion[i] != MOVING_LEFT && motion[i] != MOVING_RIGHT) {
                //Moved=0; Touched=1; PrevLEFTorRight=0;
                motion[i] = HOLDING;
            }
        } else if (touch_position[i] == 0) {
            //Moved=1; Touched=0;
            if (motion[i] == HOLDING) {
                //Moved=1; Touched=0; PrevHold=1;
                if (touch_position_prev[i] <= EDGE_AREA * 2) {
                    //Moved=1; Touched=0; PrevHold=1; LeftEdge=1;
                    motion[0] = TAPPED;
                } else if (touch_position_prev[i] >= (SENSOR_AMOUNT - EDGE_AREA) * 2) {
                    //Moved=1; Touched=0; PrevHold=1; RightEdge=1;
                    motion[i] = NOT_PRESENT;
                    motion[1] = TAPPED;
                    if (touch_position[1] == 0) i++;
                } else {
                    //Moved=1; Touched=0; PrevHold=1; WithinEdge=0;
                    motion[i] = NOT_PRESENT;
                }
            } else {
                //Moved=1; Touched=0; PrevHold=0;
                motion[i] = NOT_PRESENT;
            }
            state_changed = 1;
            touch_position_prev[i] = touch_position[i];
        } else {
            //Moved=1; Touched=1;
            if (touch_position[i] - touch_position_prev[i] < -MOVE_TOLERANCE) {
                if (motion[i] != NOT_PRESENT && motion[i] != TAPPED) {
                    //Moved=1; Touched=1; LeftShift=1; PrevTouched=1;
                    motion[i] = MOVING_LEFT;
                    state_changed = 1;
                    touch_position_prev[i] = touch_position[i];
                } else {
                    //Moved=1; Touched=1; LeftShift=1; PrevTouched=0;
                    motion[i] = HOLDING;
                    state_changed = 1;
                    touch_position_prev[i] = touch_position[i];
                }
            } else if (touch_position[i] - touch_position_prev[i] > MOVE_TOLERANCE) {
                if (motion[i] != NOT_PRESENT && motion[i] != TAPPED) {
                    //Moved=1; Touched=1; RightShift=1; PrevTouched=1;
                    motion[i] = MOVING_RIGHT;
                    state_changed = 1;
                    touch_position_prev[i] = touch_position[i];
                } else {
                    //Moved=1; Touched=1; LeftShift=1; PrevTouched=0;
                    motion[i] = HOLDING;
                    state_changed = 1;
                    touch_position_prev[i] = touch_position[i];
                }
            } else if (motion[i] != MOVING_LEFT && motion[i] != MOVING_RIGHT) {
                //Moved=1; Touched=1; PrevLEFTorRIGHT=0; Shift=0; PrevNotTouched=0;
                motion[i] = HOLDING;
            }
        }

    }
    return state_changed;
}

void setControl() {
    switch (motion[0]) {
        case MOVING_LEFT:
            digitalWrite(PIN_LEFT_STICK_X_0, HIGH);
            digitalWrite(PIN_LEFT_STICK_X_1, HIGH);
            digitalWrite(PIN_LEFT_STICK_X_2, LOW);
            break;
        case MOVING_RIGHT:
            digitalWrite(PIN_LEFT_STICK_X_0, HIGH);
            digitalWrite(PIN_LEFT_STICK_X_1, LOW);
            digitalWrite(PIN_LEFT_STICK_X_2, LOW);
            break;
        case TAPPED:
            digitalWrite(PIN_L1, HIGH);
            L1_press_time = millis();
            L1_pressed = true;
            break;
        default:
            digitalWrite(PIN_LEFT_STICK_X_0, LOW);
            digitalWrite(PIN_LEFT_STICK_X_1, LOW);
            digitalWrite(PIN_LEFT_STICK_X_2, LOW);
    }
    switch (motion[1]) {
        case MOVING_LEFT:
            digitalWrite(PIN_RIGHT_STICK_X_0, HIGH);
            digitalWrite(PIN_RIGHT_STICK_X_1, HIGH);
            digitalWrite(PIN_RIGHT_STICK_X_2, LOW);
            break;
        case MOVING_RIGHT:
            digitalWrite(PIN_RIGHT_STICK_X_0, HIGH);
            digitalWrite(PIN_RIGHT_STICK_X_1, LOW);
            digitalWrite(PIN_RIGHT_STICK_X_2, LOW);
            break;
        case TAPPED:
            digitalWrite(PIN_R1, HIGH);
            R1_press_time = millis();
            R1_pressed = true;
            break;
        default:
            digitalWrite(PIN_RIGHT_STICK_X_0, LOW);
            digitalWrite(PIN_RIGHT_STICK_X_1, LOW);
            digitalWrite(PIN_RIGHT_STICK_X_2, LOW);
    }
}

void initLED() {
    SPI.beginTransaction(led_settings);
    digitalWrite(PIN_LED_LOAD, LOW);
    SPI.transfer16(0x0900);  // No BCD decode for all digit
    digitalWrite(PIN_LED_LOAD, HIGH);
    // Brightness is set in bootAnimation
//    digitalWrite(PIN_LED_LOAD, LOW);
//    SPI.transfer(0x0A);  // Set brightness
//    SPI.transfer(LED_BRIGHTNESS);
//    digitalWrite(PIN_LED_LOAD, HIGH);
    digitalWrite(PIN_LED_LOAD, LOW);
    SPI.transfer16(0x0B04);  // Set scan limit to 4
    digitalWrite(PIN_LED_LOAD, HIGH);
    digitalWrite(PIN_LED_LOAD, LOW);
    SPI.transfer16(0x0C01);  // Normal Operation mode
    digitalWrite(PIN_LED_LOAD, HIGH);
    SPI.endTransaction();
}

void setLED() {
    SPI.beginTransaction(led_settings);
    if (motion[0] == NOT_PRESENT && motion[1] == NOT_PRESENT) {
        for (uint8_t i = 0x01; i < 6; i++) {
            digitalWrite(PIN_LED_LOAD, LOW);
            SPI.transfer(i);
            SPI.transfer(0xFF);
            digitalWrite(PIN_LED_LOAD, HIGH);
        }
    } else {
        for (uint8_t i = 0x01; i < 6; i++) {
            digitalWrite(PIN_LED_LOAD, LOW);
            SPI.transfer(i);
            SPI.transfer(sensor_data[i - 1]);
            digitalWrite(PIN_LED_LOAD, HIGH);
        }
    }
    SPI.endTransaction();
}

void bootAnimation() {
    memset(&sensor_data, 0, sizeof(sensor_data));
    SPI.beginTransaction(led_settings);
    digitalWrite(PIN_LED_LOAD, LOW);
    SPI.transfer16(0x0A0F);   // Maximum brightness
    digitalWrite(PIN_LED_LOAD, HIGH);
    SPI.endTransaction();
    for (uint8_t i = 0; i < LED_AMOUNT / 2; i++) {
        uint8_t led_index;
        led_index = LED_AMOUNT / 2 - i - 1;
        sensor_data[led_index / 8] |= 0b1 << led_index % 8;
        led_index = LED_AMOUNT / 2 + i;
        sensor_data[led_index / 8] |= 0b1 << led_index % 8;
        setLED();
        delay(40);
    }
    for (uint8_t i = 0x0E; i >= LED_BRIGHTNESS; i--) {
        SPI.beginTransaction(led_settings);
        digitalWrite(PIN_LED_LOAD, LOW);
        SPI.transfer(0x0A);
        SPI.transfer(i);
        digitalWrite(PIN_LED_LOAD, HIGH);
        SPI.endTransaction();
        delay(40);
    }
    memset(&sensor_data, 0, sizeof(sensor_data));
}

#pragma GCC pop_options
