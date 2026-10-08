# Pixel — FreeRTOS Desk Companion Robot

Pixel is an ESP32-based interactive virtual pet and desk companion robot developed using FreeRTOS.

Pixel interacts with the user through sensors and push buttons, while a 128×64 OLED display acts as Pixel's face. Different sensor conditions and user interactions produce different moods, facial expressions, and behaviors.

## Project Status

**Completed — Final Version: v26**

## Features

- Interactive virtual pet behavior
- OLED-based facial expressions
- Normal blinking and eye movement
- Happy expression with sparkles and blush
- Dizzy expression with moving pupils
- Sleepy expression based on surrounding light
- Petting interaction
- Feeding interaction
- Temperature and humidity display
- Person detection using an ultrasonic sensor
- "OH! HI!" greeting when a person approaches
- Mood-based behavior
- Automatic hunger, happiness, and energy changes
- FreeRTOS multitasking
- FreeRTOS queues for communication between tasks
- I2C mutex for safe sharing of the OLED and MPU6050

## Hardware Used

| Component | Purpose |
|---|---|
| ESP32 | Main microcontroller |
| 128×64 OLED Display | Pixel's face and information display |
| MPU6050 | Tilt detection |
| DHT11 | Temperature and humidity measurement |
| LDR | Ambient light detection |
| 10kΩ Resistor | Used with LDR as a voltage divider |
| HC-SR04 Ultrasonic Sensor | Person and approach detection |
| Push Button 1 | Petting and feeding |
| Push Button 2 | Temperature and humidity display |

## Pin Connections

| Component | ESP32 Pin |
|---|---|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| MPU6050 SDA | GPIO 21 |
| MPU6050 SCL | GPIO 22 |
| DHT11 | GPIO 4 |
| Push Button 1 | GPIO 18 |
| Push Button 2 | GPIO 19 |
| LDR | GPIO 34 |
| HC-SR04 TRIG | GPIO 13 |
| HC-SR04 ECHO | GPIO 14 |

The OLED and MPU6050 share the same I2C bus.

## How Pixel Works

Pixel continuously collects sensor information and processes it using separate FreeRTOS tasks.

The current mood and interaction state determine which expression is shown on the OLED.

### Normal Expression

Pixel normally displays its standard face with:

- Filled eyes
- Moving pupils
- Occasional blinking
- Small eye movements

### Happy Expression

When Pixel's happiness level is high, Pixel shows a happy expression with:

- Normal-style eyes
- Sparkle effects
- Blush marks

### Dizzy Expression

When Pixel is tilted strongly, the dizzy reaction is triggered.

The eyes remain similar to the normal expression, but the pupils move around inside the eyes to create a dizzy effect.

### Sleepy Expression

When the LDR detects darkness continuously for approximately 3 seconds, Pixel becomes sleepy.

The OLED displays `Zzz...`.

While Pixel is sleeping, interactions are ignored.

When sufficient light is detected continuously again for approximately 3 seconds, Pixel wakes up and returns to its appropriate mood.

### Petting

A short press of Button 1 pets Pixel.

Pixel reacts with a petting expression and happiness increases.

### Feeding

A long press of Button 1 for at least 1 second feeds Pixel.

Feeding:

- Reduces hunger
- Slightly increases happiness
- Displays the YUMMY reaction

### Temperature and Humidity

Pressing Button 2 displays the latest temperature and humidity measured by the DHT11.

## Person Detection

The HC-SR04 ultrasonic sensor is used to detect a nearby person.

Pixel uses distance thresholds with hysteresis to avoid repeated false triggers.

### Distance Logic

**Less than 40 cm**

Person is considered near and Pixel displays:

**OH! HI!**

**40–60 cm**

Transition range. The current presence state is maintained.

**More than 60 cm**

The person is considered away after remaining above 60 cm for approximately 2 seconds.

Invalid ultrasonic readings such as `-1` are ignored and do not change the presence state.

Pixel greets the person only when they approach after previously leaving.

After the "OH! HI!" reaction finishes, Pixel returns to the mood it had before the greeting.

## FreeRTOS Architecture

Pixel is divided into multiple tasks so different parts of the system can operate independently.

| Task | Priority | Responsibility |
|---|---:|---|
| `SensorTask` | 2 | Reads sensors |
| `InputTask` | 2 | Handles button input |
| `MoodTask` | 2 | Processes sensor and input data and controls Pixel's behavior |
| `DisplayTask` | 1 | Updates the OLED |

## Task Communication

Pixel uses three FreeRTOS queues.

### sensorQueue

Transfers the latest sensor information from `SensorTask` to `MoodTask`.

SensorTask → `sensorQueue` → MoodTask

This queue has a length of 1 and uses `xQueueOverwrite()` because only the latest sensor data is required.

### inputQueue

Transfers button events from `InputTask` to `MoodTask`.

InputTask → `inputQueue` → MoodTask

This queue can hold multiple input events.

### displayQueue

Transfers display commands from `MoodTask` to `DisplayTask`.

MoodTask → `displayQueue` → DisplayTask → OLED

This queue also stores only the latest display command.

## Overall System Flow

SensorTask → sensorQueue → MoodTask → displayQueue → DisplayTask → OLED

Button input follows:

Push Buttons → InputTask → inputQueue → MoodTask

## I2C Mutex

The OLED display and MPU6050 both use the ESP32's I2C bus.

Because they are accessed from different FreeRTOS tasks, Pixel uses an I2C mutex to prevent simultaneous access to the bus.

The basic sequence is:

1. A task requests access to I2C.
2. The task takes the mutex.
3. The OLED or MPU6050 is accessed.
4. The task releases the mutex.

This prevents two tasks from using the shared I2C bus at the same time.

## Mood System

Pixel has the following moods:

- `MOOD_NEUTRAL`
- `MOOD_HAPPY`
- `MOOD_SAD`
- `MOOD_HUNGRY`
- `MOOD_SLEEPY`
- `MOOD_DIZZY`

Pixel's mood is influenced by factors such as:

- Happiness
- Hunger
- Energy
- Surrounding light
- Physical tilt
- User interactions

The `MoodTask` determines the appropriate behavior based on the latest sensor and input information.

## Sensor Details

### MPU6050

The MPU6050 accelerometer is used to detect strong tilt.

When the measured acceleration indicates that Pixel has been tilted significantly, a dizzy reaction is triggered.

The dizzy reaction keeps the normal eye structure while moving the pupils around to create a dizzy effect.

### DHT11

The DHT11 measures:

- Temperature
- Humidity

The sensor is read periodically and the most recent valid readings are retained.

This prevents invalid DHT readings from unnecessarily replacing previously valid values.

### LDR

The LDR is connected using a 10kΩ resistor as a voltage divider.

Pixel continuously checks the light level.

When darkness remains below the configured threshold for approximately 3 seconds:

Darkness detected → Pixel becomes sleepy

When sufficient light remains present for approximately 3 seconds:

Light detected → Pixel wakes up

While Pixel is sleeping, interactions are ignored.

### HC-SR04

The ultrasonic sensor measures the distance between Pixel and a nearby person.

Pixel uses separate near and away thresholds so small fluctuations around a boundary do not repeatedly trigger greetings.

## Button Controls

### Button 1 — Short Press

A short press of Button 1 pets Pixel.

Pixel reacts to being petted and happiness increases.

### Button 1 — Long Press

Holding Button 1 for at least 1 second feeds Pixel.

Pixel reacts to being fed, hunger decreases, and happiness increases slightly.

### Button 2

Pressing Button 2 displays the current temperature and humidity.

## Display Expressions

Pixel's OLED is used as the face of the robot.

The final version contains expressions for:

- Normal
- Happy
- Sad
- Hungry
- Sleepy
- Dizzy
- Petting
- Feeding / YUMMY
- OH! HI!

## Project Structure

The repository contains the main Pixel firmware:

Pixel-FreeRTOS-Desk-Companion/
- `pixel.ino`
- `README.md`

Additional images and documentation can be added to the repository later.

## Software Requirements

The project is developed using the Arduino framework for ESP32.

### Required Libraries

Install the following libraries through the Arduino IDE Library Manager:

- Adafruit GFX Library
- Adafruit SSD1306
- DHT sensor library

The ESP32 board package must also be installed in the Arduino IDE.

## How to Run

1. Open `pixel.ino` in the Arduino IDE.
2. Select the correct ESP32 board.
3. Connect the ESP32 through USB.
4. Select the correct COM port.
5. Install the required libraries.
6. Compile the project.
7. Upload the program to the ESP32.
8. Connect the sensors and OLED according to the pin connections.
9. Power Pixel and observe the OLED interactions.

## Technologies Used

- ESP32
- Arduino
- C/C++
- FreeRTOS
- I2C
- OLED Display
- MPU6050
- DHT11
- LDR
- HC-SR04
- GPIO
- FreeRTOS Queues
- FreeRTOS Mutex

## Project Objective

The objective of Pixel is to demonstrate how an embedded system can combine:

- Sensor data
- User interaction
- State-based behavior
- OLED graphics
- Multitasking
- Inter-task communication
- Synchronization

into a single interactive embedded application.

Rather than using one large sequential program, Pixel uses FreeRTOS tasks to separate sensing, input handling, behavior processing, and display rendering.

## Final Version

**Pixel v26 — Final Completed Version**

The final version contains the completed hardware integration, FreeRTOS architecture, sensor processing, interaction logic, sleep and wake behavior, presence detection, mood system, and OLED expressions.
