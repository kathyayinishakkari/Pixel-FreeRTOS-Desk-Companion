#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// =====================================================
// PINS
// =====================================================

#define OLED_SDA 21
#define OLED_SCL 22

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define BUTTON1_PIN 18
#define BUTTON2_PIN 19

#define LDR_PIN 34

#define TRIG_PIN 13
#define ECHO_PIN 14

#define DARK_THRESHOLD 1000

#define PRESENCE_NEAR_THRESHOLD 40
#define PRESENCE_AWAY_THRESHOLD 60

// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// =====================================================
// DHT
// =====================================================

DHT dht(DHT_PIN, DHT_TYPE);

// =====================================================
// MPU6050
// =====================================================

#define MPU_ADDR 0x68
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B
#define WHO_AM_I 0x75

// =====================================================
// MOODS
// =====================================================

enum Mood {

  MOOD_NEUTRAL,
  MOOD_HAPPY,
  MOOD_SAD,
  MOOD_HUNGRY,
  MOOD_SLEEPY,
  MOOD_DIZZY
};

// =====================================================
// DISPLAY STATES
// =====================================================

enum DisplayState {

  DISPLAY_NORMAL,
  DISPLAY_PET,
  DISPLAY_FEED,
  DISPLAY_APPROACH,
  DISPLAY_DIZZY,
  DISPLAY_INFO
};

// =====================================================
// INPUT EVENTS
// =====================================================

enum InputEvent {

  INPUT_PET,
  INPUT_FEED,
  INPUT_INFO
};

// =====================================================
// SENSOR DATA
// =====================================================

struct SensorData {

  float temperature;
  float humidity;

  int light;

  long distance;

  float ax;
  float ay;
  float az;

  bool tilted;
  bool dark;
};

// =====================================================
// DISPLAY DATA
// =====================================================

struct DisplayData {

  Mood mood;
  DisplayState state;
  float temperature;
  float humidity;
};

// =====================================================
// QUEUES
// =====================================================

QueueHandle_t sensorQueue;
QueueHandle_t inputQueue;
QueueHandle_t displayQueue;
SemaphoreHandle_t i2cMutex;

// =====================================================
// PIXEL STATE
// =====================================================

int happiness = 50;
int hunger = 30;
int energy = 80;

Mood currentMood = MOOD_NEUTRAL;

DisplayState currentDisplayState =
  DISPLAY_NORMAL;

unsigned long reactionUntil = 0;

// Mood to restore after the OH! HI! reaction.
Mood approachReturnMood = MOOD_NEUTRAL;
bool restoreApproachMood = false;

bool ldrDark = false;
unsigned long darkSince = 0;
unsigned long lightSince = 0;

const unsigned long LDR_CONFIRM_TIME = 3000;

bool personNearby = false;
unsigned long nearSince = 0;
unsigned long awaySince = 0;
const unsigned long PRESENCE_NEAR_CONFIRM_TIME = 1000;
const unsigned long PRESENCE_AWAY_CONFIRM_TIME = 2000;
bool wasTilted = false;


// Eye animation state
int eyeOffsetX = 0;
bool eyesBlink = false;
unsigned long nextEyeAction = 0;
unsigned long eyeActionUntil = 0;
int eyeDirection = 0;
int greetingEyeStyle = 2;

// =====================================================
// MPU FUNCTIONS
// =====================================================

void writeMPU(byte reg, byte value) {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);
  Wire.write(value);

  Wire.endTransmission();
}


byte readMPU(byte reg) {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);

  Wire.endTransmission(false);

  Wire.requestFrom(
    MPU_ADDR,
    (byte)1
  );

  if (Wire.available()) {
    return Wire.read();
  }

  return 0;
}


int16_t readMPU16(byte reg) {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);

  Wire.endTransmission(false);

  Wire.requestFrom(
    MPU_ADDR,
    (byte)2
  );

  if (Wire.available() >= 2) {

    byte highByte = Wire.read();
    byte lowByte = Wire.read();

    return (int16_t)(
      (highByte << 8) | lowByte
    );
  }

  return 0;
}


// =====================================================
// READ MPU
// =====================================================

void readMPU(
  float &ax,
  float &ay,
  float &az,
  bool &tilted
) {

  int16_t rawX =
    readMPU16(ACCEL_XOUT_H);

  int16_t rawY =
    readMPU16(ACCEL_XOUT_H + 2);

  int16_t rawZ =
    readMPU16(ACCEL_XOUT_H + 4);


  ax = rawX / 16384.0;
  ay = rawY / 16384.0;
  az = rawZ / 16384.0;


  if (
    abs(ax) > 0.70 ||
    abs(ay) > 0.70 ||
    abs(az) < 0.65
  ) {

    tilted = true;

  } else {

    tilted = false;
  }
}


// =====================================================
// HC-SR04
// =====================================================

long readDistance() {

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  delayMicroseconds(2);

  digitalWrite(
    TRIG_PIN,
    HIGH
  );

  delayMicroseconds(10);

  digitalWrite(
    TRIG_PIN,
    LOW
  );


  long duration =
    pulseIn(
      ECHO_PIN,
      HIGH,
      30000
    );


  if (duration == 0) {
    return -1;
  }


  return duration * 0.0343 / 2;
}


// =====================================================
// MOOD NAME
// =====================================================

const char* moodName(Mood mood) {

  switch (mood) {

    case MOOD_NEUTRAL:
      return "NEUTRAL";

    case MOOD_HAPPY:
      return "HAPPY";

    case MOOD_SAD:
      return "SAD";

    case MOOD_HUNGRY:
      return "HUNGRY";

    case MOOD_SLEEPY:
      return "SLEEPY";

    case MOOD_DIZZY:
      return "DIZZY";
  }

  return "NEUTRAL";
}


// =====================================================
// DETERMINE MOOD
// =====================================================

Mood determineMood() {

  // If it is confirmed dark, darkness keeps Pixel sleepy.
  if (ldrDark) {
    return MOOD_SLEEPY;
  }

  if (hunger >= 80) {
    return MOOD_HUNGRY;
  }

  if (energy <= 20) {
    return MOOD_SLEEPY;
  }

  if (happiness <= 25) {
    return MOOD_SAD;
  }

  if (happiness >= 80) {
    return MOOD_HAPPY;
  }

  return MOOD_NEUTRAL;
}


// =====================================================
// SEND DISPLAY COMMAND
// =====================================================

void sendDisplayCommand(
  Mood mood,
  DisplayState state
) {

  DisplayData data;

  data.mood = mood;
  data.state = state;
  data.temperature = 0;
  data.humidity = 0;

  xQueueOverwrite(
    displayQueue,
    &data
  );
}


void sendInfoCommand(
  float temperature,
  float humidity
) {

  DisplayData data;

  data.mood = currentMood;
  data.state = DISPLAY_INFO;
  data.temperature = temperature;
  data.humidity = humidity;

  xQueueOverwrite(
    displayQueue,
    &data
  );
}


// =====================================================
// SENSOR TASK
// =====================================================

void SensorTask(void *parameter) {

  Serial.println(
    "[Sensor Task] Started"
  );


  // Keep the most recent valid DHT reading.
  float lastTemperature = 0;
  float lastHumidity = 0;
  unsigned long lastDHTRead = 0;

  while (true) {

    SensorData data = {};

    // DHT11 is updated every 2 seconds.
    if (
      lastDHTRead == 0 ||
      millis() - lastDHTRead >= 2000
    ) {

      lastDHTRead = millis();

      float temp =
        dht.readTemperature();

      float hum =
        dht.readHumidity();

      // Only replace the cached values when the reading is valid.
      if (!isnan(temp)) {
        lastTemperature = temp;
      }

      if (!isnan(hum)) {
        lastHumidity = hum;
      }
    }

    data.temperature = lastTemperature;
    data.humidity = lastHumidity;


    data.light =
      analogRead(LDR_PIN);

    data.dark =
      (data.light < DARK_THRESHOLD);


    data.distance =
      readDistance();


    // OLED and MPU6050 share the same I2C bus.
    // Protect the bus while reading the MPU.
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {

      readMPU(
        data.ax,
        data.ay,
        data.az,
        data.tilted
      );

      xSemaphoreGive(i2cMutex);

    } else {

      // If the bus is busy, keep the previous/default
      // MPU values for this sensor cycle.
      data.ax = 0;
      data.ay = 0;
      data.az = 1;
      data.tilted = false;
    }


    xQueueOverwrite(
      sensorQueue,
      &data
    );


    Serial.print(
      "[SENSOR] Light:"
    );

    Serial.print(
      data.light
    );

    Serial.print(
      " Distance:"
    );

    Serial.print(
      data.distance
    );

    Serial.print(
      " Tilt:"
    );

    Serial.print(
      data.tilted ? "YES" : "NO"
    );

    Serial.println(
      ""
    );


    vTaskDelay(
      pdMS_TO_TICKS(1000)
    );
  }
}


// =====================================================
// INPUT TASK
// =====================================================

void InputTask(void *parameter) {

  Serial.println(
    "[Input Task] Started"
  );


  bool button1Pressed = false;

  bool feedTriggered = false;

  unsigned long button1Start = 0;


  bool previousButton2 = HIGH;


  while (true) {

    // Pixel is asleep in darkness.
    // Reset input state so button activity during sleep
    // cannot create delayed interactions after waking.
    if (ldrDark) {

      button1Pressed = false;
      feedTriggered = false;
      button1Start = 0;
      previousButton2 = HIGH;

      vTaskDelay(
        pdMS_TO_TICKS(30)
      );

      continue;
    }

    bool button1 =
      digitalRead(BUTTON1_PIN);

    bool button2 =
      digitalRead(BUTTON2_PIN);


    // =================================================
    // BUTTON 1 PRESSED
    // =================================================

    if (
      !button1Pressed &&
      button1 == LOW
    ) {

      button1Pressed = true;

      feedTriggered = false;

      button1Start = millis();

      Serial.println(
        "[INPUT] BUTTON 1 DOWN"
      );
    }


    // =================================================
    // LONG PRESS
    // =================================================

    if (
      button1Pressed &&
      button1 == LOW
    ) {

      if (
        millis() - button1Start >= 1000 &&
        !feedTriggered
      ) {

        feedTriggered = true;

        InputEvent event =
          INPUT_FEED;


        xQueueSend(
          inputQueue,
          &event,
          0
        );


        Serial.println(
          "[INPUT] FEED"
        );
      }
    }


    // =================================================
    // BUTTON RELEASE
    // =================================================

    if (
      button1Pressed &&
      button1 == HIGH
    ) {

      unsigned long duration =
        millis() - button1Start;


      if (
        duration < 1000 &&
        !feedTriggered
      ) {

        InputEvent event =
          INPUT_PET;


        xQueueSend(
          inputQueue,
          &event,
          0
        );


        Serial.println(
          "[INPUT] PET"
        );
      }


      button1Pressed = false;

      feedTriggered = false;
    }


    // =================================================
    // BUTTON 2
    // =================================================

    if (
      previousButton2 == HIGH &&
      button2 == LOW
    ) {

      InputEvent event =
        INPUT_INFO;


      xQueueSend(
        inputQueue,
        &event,
        0
      );


      Serial.println(
        "[INPUT] INFO"
      );
    }


    previousButton2 = button2;


    vTaskDelay(
      pdMS_TO_TICKS(30)
    );
  }
}


// =====================================================
// MOOD TASK
// =====================================================

void MoodTask(void *parameter) {

  Serial.println(
    "[Mood Task] Started"
  );

  SensorData sensor = {};
  InputEvent input;

  unsigned long lastDecay = millis();

  while (true) {

    // =================================================
    // SENSOR DATA
    // =================================================

    if (
      xQueueReceive(
        sensorQueue,
        &sensor,
        0
      ) == pdTRUE
    ) {

      // =================================================
      // LDR DARK / LIGHT BEHAVIOR
      // =================================================

      if (sensor.dark) {

        if (!ldrDark) {

          if (darkSince == 0) {
            darkSince = millis();
          }

          if (millis() - darkSince >= LDR_CONFIRM_TIME) {

            ldrDark = true;
            lightSince = 0;

            if (millis() >= reactionUntil) {

              currentMood = MOOD_SLEEPY;

              sendDisplayCommand(
                currentMood,
                DISPLAY_NORMAL
              );

              Serial.println(
                "[LDR] DARK -> PIXEL SLEEPY"
              );
            }
          }
        }
      }

      else {

        darkSince = 0;

        if (ldrDark) {

          if (lightSince == 0) {
            lightSince = millis();
          }

          if (millis() - lightSince >= LDR_CONFIRM_TIME) {

            ldrDark = false;
            lightSince = 0;

            if (millis() >= reactionUntil) {

              Mood newMood =
                determineMood();

              currentMood =
                newMood;

              sendDisplayCommand(
                currentMood,
                DISPLAY_NORMAL
              );

              Serial.println(
                "[LDR] LIGHT -> PIXEL AWAKE"
              );
            }
          }
        }
      }


      // =================================================
      // SLEEP MODE
      // Darkness means Pixel is sleeping.
      // While asleep, Pixel ignores interactions.
      // =================================================

      if (ldrDark) {

        currentMood = MOOD_SLEEPY;

        if (currentDisplayState != DISPLAY_NORMAL) {
          sendDisplayCommand(
            currentMood,
            DISPLAY_NORMAL
          );
        }

        // Ignore tilt and presence while sleeping.
        wasTilted = sensor.tilted;
        personNearby = false;
      }

      else {

        // =================================================
        // Strong tilt -> DIZZY
        // Trigger only when entering the tilted state.
        // =================================================

        if (
          sensor.tilted &&
          !wasTilted &&
          millis() >= reactionUntil
        ) {

          sendDisplayCommand(
            currentMood,
            DISPLAY_DIZZY
          );

          restoreApproachMood = false;
          reactionUntil = millis() + 2000;

          Serial.println(
            "[MOOD] DIZZY"
          );
        }

        wasTilted = sensor.tilted;

        // -------------------------------------------------
        // Presence detection with wide hysteresis
        //
        // < 40 cm:
        //   Person is close / approaching.
        //   Require 1 second of valid readings before greeting.
        //
        // 40-60 cm:
        //   Transition zone. Keep the current presence state.
        //
        // > 60 cm:
        //   Person is considered away only after 2 seconds.
        //
        // -1:
        //   Invalid reading. Completely ignore it.
        // -------------------------------------------------

        if (
          sensor.distance > 0 &&
          sensor.distance < PRESENCE_NEAR_THRESHOLD
        ) {

          awaySince = 0;

          if (!personNearby) {

            // Trigger immediately on a valid near reading.
            if (millis() >= reactionUntil) {

              personNearby = true;
              nearSince = 0;

              // Remember the normal mood before OH! HI!
              // so Pixel returns to exactly that mood afterward.
              approachReturnMood = currentMood;
              restoreApproachMood = true;

              // Keep the confirmed OH! HI! eye expression every time.
              greetingEyeStyle = 2;

              sendDisplayCommand(
                currentMood,
                DISPLAY_APPROACH
              );

              reactionUntil = millis() + 3000;

              Serial.println(
                "[MOOD] PERSON ARRIVED -> OH! HI!"
              );
            }
          }
        }

        else if (
          sensor.distance >= PRESENCE_NEAR_THRESHOLD &&
          sensor.distance <= PRESENCE_AWAY_THRESHOLD
        ) {

          // Transition zone: keep the previous presence state.
          // Also keep a near candidate alive if Pixel has not
          // greeted yet, so small distance fluctuations do not
          // restart the arrival timer.
          awaySince = 0;
        }

        else if (
          sensor.distance > PRESENCE_AWAY_THRESHOLD
        ) {

          nearSince = 0;

          // Require the person to remain away before resetting
          // personNearby. This prevents false re-entry greetings.
          if (awaySince == 0) {
            awaySince = millis();
          }

          if (
            personNearby &&
            millis() - awaySince >= PRESENCE_AWAY_CONFIRM_TIME
          ) {

            personNearby = false;
            awaySince = 0;

            Serial.println(
              "[MOOD] PERSON LEFT"
            );
          }
        }

        else {
          // -1 (no echo): ignore completely.
          // Do not change personNearby or timing state.
        }
      }
    }

    // =================================================
    // INPUT EVENTS
    // =================================================

    // Pixel is asleep in darkness, so discard any
    // button events instead of processing them later.
    if (ldrDark) {

      while (
        xQueueReceive(
          inputQueue,
          &input,
          0
        ) == pdTRUE
      ) {
        // Ignore all interactions while sleeping.
      }
    }

    else if (
      xQueueReceive(
        inputQueue,
        &input,
        0
      ) == pdTRUE
    ) {

      // PET
      if (input == INPUT_PET) {

        happiness += 10;
        happiness = constrain(happiness, 0, 100);

        sendDisplayCommand(
          currentMood,
          DISPLAY_PET
        );

        restoreApproachMood = false;
        reactionUntil = millis() + 2000;

        Serial.println(
          "[MOOD] PET"
        );
      }

      // FEED
      else if (input == INPUT_FEED) {

        hunger -= 20;
        hunger = constrain(hunger, 0, 100);

        happiness += 5;
        happiness = constrain(happiness, 0, 100);

        sendDisplayCommand(
          currentMood,
          DISPLAY_FEED
        );

        restoreApproachMood = false;
        reactionUntil = millis() + 2000;

        Serial.println(
          "[MOOD] FEED"
        );
      }

      // INFO
      else if (input == INPUT_INFO) {

        // Use the latest DHT reading.
        sendInfoCommand(
          sensor.temperature,
          sensor.humidity
        );

        reactionUntil = millis() + 5000;

        Serial.print(
          "[MOOD] INFO -> Temp: "
        );
        Serial.print(sensor.temperature, 1);
        Serial.print(" C  Humidity: ");
        Serial.print(sensor.humidity, 0);
        Serial.println(" %");
      }
    }

    // =================================================
    // NATURAL DECAY
    // =================================================

    if (
      millis() - lastDecay >= 30000
    ) {

      lastDecay = millis();

      hunger += 2;
      energy -= 1;
      happiness -= 1;

      hunger = constrain(hunger, 0, 100);
      energy = constrain(energy, 0, 100);
      happiness = constrain(happiness, 0, 100);
    }

    // =================================================
    // RETURN TO NORMAL
    // =====================================================

    if (
      reactionUntil != 0 &&
      millis() >= reactionUntil
    ) {

      Mood newMood;

      // OH! HI! returns to the exact mood Pixel had before
      // the greeting. Other reactions recalculate normally.
      if (restoreApproachMood) {

        newMood = approachReturnMood;
        restoreApproachMood = false;

      } else {

        newMood = determineMood();
      }

      currentMood = newMood;

      if (currentDisplayState != DISPLAY_NORMAL) {

        sendDisplayCommand(
          currentMood,
          DISPLAY_NORMAL
        );
      }

      reactionUntil = 0;

      Serial.print(
        "[MOOD] NORMAL -> "
      );
      Serial.println(
        moodName(currentMood)
      );
    }

    vTaskDelay(
      pdMS_TO_TICKS(50)
    );
  }
}


// =====================================================
// DRAW EYES
// =====================================================

void drawNormalEyes() {

  if (eyesBlink) {

    display.drawLine(
      20, 35,
      54, 35,
      SSD1306_WHITE
    );

    display.drawLine(
      74, 35,
      108, 35,
      SSD1306_WHITE
    );

    return;
  }

  // White eye shapes
  display.fillRoundRect(
    16, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  // Moving pupils
  display.fillCircle(
    37 + eyeOffsetX,
    35,
    6,
    SSD1306_BLACK
  );

  display.fillCircle(
    91 + eyeOffsetX,
    35,
    6,
    SSD1306_BLACK
  );
}



void drawHappyEyes() {

  // Happy face keeps the same basic eye shape as the normal face.
  display.fillRoundRect(
    16, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  // Small pupils, like the normal expression.
  display.fillCircle(
    37, 35,
    6,
    SSD1306_BLACK
  );

  display.fillCircle(
    91, 35,
    6,
    SSD1306_BLACK
  );

  // ✨-like four-point sparkles near the eyes.
  display.drawLine(10, 18, 10, 26, SSD1306_WHITE);
  display.drawLine(6, 22, 14, 22, SSD1306_WHITE);
  display.drawLine(8, 20, 12, 24, SSD1306_WHITE);
  display.drawLine(8, 24, 12, 20, SSD1306_WHITE);

  display.drawLine(118, 18, 118, 26, SSD1306_WHITE);
  display.drawLine(114, 22, 122, 22, SSD1306_WHITE);
  display.drawLine(116, 20, 120, 24, SSD1306_WHITE);
  display.drawLine(116, 24, 120, 20, SSD1306_WHITE);

  // Blush: small diagonal marks under both eyes.
  display.drawLine(18, 53, 22, 50, SSD1306_WHITE);
  display.drawLine(24, 54, 28, 51, SSD1306_WHITE);
  display.drawLine(100, 51, 104, 54, SSD1306_WHITE);
  display.drawLine(106, 50, 110, 53, SSD1306_WHITE);
}

void drawSadEyes() {

  display.fillRoundRect(
    18, 27,
    38, 20,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    72, 27,
    38, 20,
    8,
    SSD1306_WHITE
  );


  display.drawLine(
    18, 27,
    30, 22,
    SSD1306_WHITE
  );

  display.drawLine(
    98, 22,
    110, 27,
    SSD1306_WHITE
  );
}


void drawHungryEyes() {

  display.fillCircle(
    37, 35,
    13,
    SSD1306_WHITE
  );

  display.fillCircle(
    91, 35,
    13,
    SSD1306_WHITE
  );


  display.fillCircle(
    37, 35,
    5,
    SSD1306_BLACK
  );

  display.fillCircle(
    91, 35,
    5,
    SSD1306_BLACK
  );
}


void drawSleepyEyes() {

  display.drawLine(
    18, 35,
    55, 35,
    SSD1306_WHITE
  );

  display.drawLine(
    72, 35,
    109, 35,
    SSD1306_WHITE
  );
}



void drawPetEyes() {

  display.drawLine(
    18, 34,
    37, 42,
    SSD1306_WHITE
  );

  display.drawLine(
    37, 42,
    56, 34,
    SSD1306_WHITE
  );

  display.drawLine(
    72, 34,
    91, 42,
    SSD1306_WHITE
  );

  display.drawLine(
    91, 42,
    110, 34,
    SSD1306_WHITE
  );
}


void drawSurprisedEyes() {

  display.drawCircle(
    37, 35,
    15,
    SSD1306_WHITE
  );

  display.drawCircle(
    91, 35,
    15,
    SSD1306_WHITE
  );

  display.fillCircle(
    37, 35,
    6,
    SSD1306_WHITE
  );

  display.fillCircle(
    91, 35,
    6,
    SSD1306_WHITE
  );
}



void drawWavingHand() {

  // Original Pixel-style waving hand.

  // Palm
  display.fillRoundRect(
    108, 38,
    18, 16,
    5,
    SSD1306_WHITE
  );

  // Fingers
  display.fillRoundRect(
    109, 24,
    4, 17,
    2,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    114, 20,
    4, 21,
    2,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    119, 23,
    4, 18,
    2,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    124, 28,
    4, 14,
    2,
    SSD1306_WHITE
  );

  // Thumb
  display.fillRoundRect(
    103, 38,
    9, 5,
    2,
    SSD1306_WHITE
  );
}



void drawGreetingEyes() {

  // Fixed confirmed OH! HI! expression.
  // Filled eyes similar to the normal face, with larger pupils/highlights.
  display.fillRoundRect(
    20, 23,
    34, 25,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    74, 23,
    34, 25,
    8,
    SSD1306_WHITE
  );

  display.fillCircle(
    37, 36,
    7,
    SSD1306_BLACK
  );

  display.fillCircle(
    91, 36,
    7,
    SSD1306_BLACK
  );

  // Small white highlights.
  display.fillCircle(
    34, 33,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    88, 33,
    2,
    SSD1306_WHITE
  );
}

void drawEatingEyes() {

  display.fillCircle(
    37, 35,
    12,
    SSD1306_WHITE
  );

  display.fillCircle(
    91, 35,
    12,
    SSD1306_WHITE
  );


  display.fillCircle(
    37, 35,
    4,
    SSD1306_BLACK
  );

  display.fillCircle(
    91, 35,
    4,
    SSD1306_BLACK
  );
}

void drawDizzyEyes() {

  // Same eye shape as normal, but the pupils wander around inside the eyes.
  display.fillRoundRect(
    16, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70, 20,
    42, 29,
    9,
    SSD1306_WHITE
  );

  // Continuous dizzy-looking pupil movement.
  float t = millis() * 0.009f;

  int leftX  = 37 + (int)round(7.0f * sin(t));
  int leftY  = 35 + (int)round(4.0f * cos(t * 1.35f));

  int rightX = 91 + (int)round(7.0f * sin(t + 1.8f));
  int rightY = 35 + (int)round(4.0f * cos(t * 1.25f + 1.0f));

  display.fillCircle(
    leftX,
    leftY,
    6,
    SSD1306_BLACK
  );

  display.fillCircle(
    rightX,
    rightY,
    6,
    SSD1306_BLACK
  );
}

void drawInfoScreen(
  float temperature,
  float humidity
) {

  display.clearDisplay();

  // Compact filled sun icon
  display.fillCircle(25, 21, 5, SSD1306_WHITE);

  display.drawLine(25, 13, 25, 10, SSD1306_WHITE);
  display.drawLine(25, 29, 25, 32, SSD1306_WHITE);
  display.drawLine(17, 21, 14, 21, SSD1306_WHITE);
  display.drawLine(33, 21, 36, 21, SSD1306_WHITE);
  display.drawLine(19, 15, 17, 13, SSD1306_WHITE);
  display.drawLine(31, 15, 33, 13, SSD1306_WHITE);
  display.drawLine(19, 27, 17, 29, SSD1306_WHITE);
  display.drawLine(31, 27, 33, 29, SSD1306_WHITE);

  // Temperature
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(48, 17);
  display.print(temperature, 1);
  display.print(" C");

  // Compact filled cloud icon
  display.fillCircle(21, 49, 4, SSD1306_WHITE);
  display.fillCircle(27, 46, 5, SSD1306_WHITE);
  display.fillCircle(33, 49, 4, SSD1306_WHITE);
  display.fillRect(20, 49, 15, 6, SSD1306_WHITE);

  // Humidity text to the right of cloud
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(48, 44);
  display.print(humidity, 0);
  display.print(" % RH");

  display.display();
}


// =====================================================
// DRAW CURRENT PIXEL
// =====================================================

void drawPixel(
  Mood mood,
  DisplayState state
) {

  display.clearDisplay();


  // -------------------------
  // MESSAGE
  // -------------------------

  display.setTextSize(1);

  display.setTextColor(
    SSD1306_WHITE
  );


  const char* message = "...";


  if (
    state == DISPLAY_PET
  ) {

    message = "I LIKE IT!";

  }

  else if (
    state == DISPLAY_FEED
  ) {

    message = "YUMMY!";

  }

  else if (
    state == DISPLAY_APPROACH
  ) {

    message = "OH! HI!";

  }

  else if (
    state == DISPLAY_DIZZY
  ) {

    message = "WHOA...";

  }

  else {

    switch (mood) {

      case MOOD_HAPPY:
        message = "I'M HAPPY";
        break;

      case MOOD_HUNGRY:
        message = "I'M HUNGRY";
        break;

      case MOOD_SLEEPY:
        message = "Zzz...";
        break;

      default:
        message = "...";
        break;
    }
  }


  int16_t x1;
  int16_t y1;

  uint16_t w;
  uint16_t h;


  display.getTextBounds(
    message,
    0,
    0,
    &x1,
    &y1,
    &w,
    &h
  );


  display.setCursor(
    (128 - w) / 2,
    2
  );


  display.println(
    message
  );


  // -------------------------
  // EYES
  // -------------------------

  if (
    state == DISPLAY_PET
  ) {

    drawPetEyes();
  }

  else if (
    state == DISPLAY_FEED
  ) {

    drawEatingEyes();
  }

  else if (
    state == DISPLAY_APPROACH
  ) {

    drawGreetingEyes();
  }

  else if (
    state == DISPLAY_DIZZY
  ) {

    drawDizzyEyes();
  }

  else {

    switch (mood) {

      case MOOD_HAPPY:
        drawHappyEyes();
        break;

      case MOOD_SAD:
        drawSadEyes();
        break;

      case MOOD_HUNGRY:
        drawHungryEyes();
        break;

      case MOOD_SLEEPY:
        drawSleepyEyes();
        break;

      default:
        drawNormalEyes();
        break;
    }
  }


  display.display();
}


// =====================================================
// DISPLAY TASK
// =====================================================

void updateEyeAnimation() {

  unsigned long now = millis();

  if (nextEyeAction == 0) {
    nextEyeAction = now + random(1800, 4000);
  }

  // Finish a blink / look action
  if (eyeActionUntil != 0 && now >= eyeActionUntil) {

    eyesBlink = false;
    eyeOffsetX = 0;
    eyeDirection = 0;
    eyeActionUntil = 0;
    nextEyeAction = now + random(1800, 4000);
  }

  // Start a new small eye movement or blink
  if (eyeActionUntil == 0 && now >= nextEyeAction) {

    int action = random(0, 4);

    if (action == 0) {
      // Blink
      eyesBlink = true;
      eyeActionUntil = now + 140;
    }
    else {
      // Look left or right briefly
      eyeDirection = (action == 1) ? -1 : 1;
      eyeOffsetX = eyeDirection * 5;
      eyeActionUntil = now + random(450, 900);
    }
  }
}


void DisplayTask(void *parameter) {

  Serial.println(
    "[Display Task] Started"
  );

  DisplayData data;

  Mood displayMood = MOOD_NEUTRAL;
  DisplayState displayState = DISPLAY_NORMAL;
  float displayTemperature = 0;
  float displayHumidity = 0;

  randomSeed(micros());
  nextEyeAction = millis() + 2000;

  sendDisplayCommand(
    MOOD_NEUTRAL,
    DISPLAY_NORMAL
  );

  while (true) {

    if (
      xQueueReceive(
        displayQueue,
        &data,
        pdMS_TO_TICKS(20)
      ) == pdTRUE
    ) {

      displayMood = data.mood;
      displayState = data.state;
      displayTemperature = data.temperature;
      displayHumidity = data.humidity;

      currentDisplayState = data.state;
    }

    // Animate only the normal face.
    if (displayState == DISPLAY_NORMAL) {
      updateEyeAnimation();
    }
    else {
      eyesBlink = false;
      eyeOffsetX = 0;
      eyeActionUntil = 0;
      nextEyeAction = millis() + 1500;
    }

    if (displayState == DISPLAY_INFO) {

      // Draw the info screen without sending it yet.
      // drawInfoScreen() normally calls display.display(),
      // so use the existing function only under the I2C lock.
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {

        drawInfoScreen(
          displayTemperature,
          displayHumidity
        );

        xSemaphoreGive(i2cMutex);
      }

    }
    else {

      // drawPixel() includes display.display(), so protect
      // the complete OLED update from concurrent MPU I2C access.
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {

        drawPixel(
          displayMood,
          displayState
        );

        xSemaphoreGive(i2cMutex);
      }
    }

    vTaskDelay(
      pdMS_TO_TICKS(40)
    );
  }
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println(
    "=============================="
  );

  Serial.println(
    "       PIXEL STARTING"
  );

  Serial.println(
    "=============================="
  );


  // =================================================
  // I2C
  // =================================================

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  // OLED and MPU6050 share this I2C bus.
  i2cMutex = xSemaphoreCreateMutex();

  if (i2cMutex == NULL) {

    Serial.println(
      "I2C MUTEX CREATION FAILED"
    );

    while (true) {
      delay(1000);
    }
  }


  // =================================================
  // OLED
  // =================================================

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {

    Serial.println(
      "OLED ERROR"
    );

    while (true) {
      delay(1000);
    }
  }


  display.clearDisplay();

  display.display();


  // =================================================
  // DHT
  // =================================================

  dht.begin();


  // =================================================
  // BUTTONS
  // =================================================

  pinMode(
    BUTTON1_PIN,
    INPUT_PULLUP
  );

  pinMode(
    BUTTON2_PIN,
    INPUT_PULLUP
  );


  // =================================================
  // LDR
  // =================================================

  pinMode(
    LDR_PIN,
    INPUT
  );


  // =================================================
  // HC-SR04
  // =================================================

  pinMode(
    TRIG_PIN,
    OUTPUT
  );

  pinMode(
    ECHO_PIN,
    INPUT
  );


  // =================================================
  // MPU
  // =================================================

  byte whoAmI =
    readMPU(WHO_AM_I);


  Serial.print(
    "MPU WHO_AM_I: 0x"
  );

  Serial.println(
    whoAmI,
    HEX
  );


  writeMPU(
    PWR_MGMT_1,
    0x00
  );


  delay(100);


  // =================================================
  // QUEUES
  // =================================================

  sensorQueue =
    xQueueCreate(
      1,
      sizeof(SensorData)
    );


  inputQueue =
    xQueueCreate(
      5,
      sizeof(InputEvent)
    );


  displayQueue =
    xQueueCreate(
      1,
      sizeof(DisplayData)
    );


  if (
    sensorQueue == NULL ||
    inputQueue == NULL ||
    displayQueue == NULL
  ) {

    Serial.println(
      "QUEUE CREATION FAILED"
    );

    while (true) {
      delay(1000);
    }
  }


  // =================================================
  // TASKS
  // =================================================

  xTaskCreate(
    SensorTask,
    "Sensor Task",
    4096,
    NULL,
    2,
    NULL
  );


  xTaskCreate(
    InputTask,
    "Input Task",
    4096,
    NULL,
    2,
    NULL
  );


  xTaskCreate(
    MoodTask,
    "Mood Task",
    4096,
    NULL,
    2,
    NULL
  );


  xTaskCreate(
    DisplayTask,
    "Display Task",
    4096,
    NULL,
    1,
    NULL
  );


  Serial.println();
  Serial.println(
    "All Pixel tasks created."
  );

  Serial.println(
    "Display Engine ready."
  );

  Serial.println(
    "=============================="
  );
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );
}