# FingerPrint Library

A powerful Arduino library for fingerprint sensors that provides advanced template management, matching capabilities, and SHA-256 hashing support. Built on top of the Adafruit Fingerprint Sensor Library with enhanced features for secure biometric authentication.

## 🔐 Features

- **Template Extraction**: Download raw fingerprint templates (512 bytes) from the sensor
- **External Template Matching**: Match fingerprints against templates stored outside the sensor
- **SHA-256 Hashing**: Generate cryptographic hashes of fingerprint templates
- **Enhanced Enrollment**: Two-scan enrollment with automatic template retrieval
- **Robust Matching**: Multi-attempt verification with quality checks
- **Template Upload**: Upload stored templates back to sensor memory for matching
- **Detailed Logging**: Comprehensive serial output for debugging and monitoring

## 📋 Table of Contents

- [Hardware Requirements](#hardware-requirements)
- [Dependencies](#dependencies)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
- [Usage Examples](#usage-examples)
- [How It Works](#how-it-works)
- [Error Codes](#error-codes)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## 🔧 Hardware Requirements

- **Microcontroller**: ESP32 (or any Arduino-compatible board with hardware serial)
- **Fingerprint Sensor**: Adafruit-compatible fingerprint sensor (e.g., R307, AS608, ZFM-70)
- **Connections**:
  - Sensor RX → ESP32 TX (GPIO 17)
  - Sensor TX → ESP32 RX (GPIO 16)
  - Sensor VCC → 3.3V or 5V (check sensor specs)
  - Sensor GND → GND

## 📦 Dependencies

This library requires:

- [Adafruit Fingerprint Sensor Library](https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library)
- **mbedTLS** (for SHA-256 hashing) - included with ESP32 Arduino core

## 📥 Installation

### PlatformIO

1. Copy the `FingerPrint` folder to your project's `lib/` directory
2. Add to your `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = 
    adafruit/Adafruit Fingerprint Sensor Library
```

### Arduino IDE

1. Copy the `FingerPrint` folder to your Arduino `libraries` folder
2. Install "Adafruit Fingerprint Sensor Library" via Library Manager
3. Restart Arduino IDE

## 🚀 Quick Start

```cpp
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <FingerPrint.h>

// Configure hardware serial for ESP32
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
FingerPrint fingerPrintSensor(&finger);

// Storage for fingerprint template
uint8_t userTemplate[FingerPrint::TEMPLATE_SIZE];

void setup() {
  Serial.begin(115200);
  
  // Initialize sensor serial (RX=16, TX=17)
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  
  // Initialize library
  fingerPrintSensor.begin(57600);
  fingerPrintSensor.setSerial(&mySerial);
  
  if (!fingerPrintSensor.init()) {
    Serial.println("Sensor initialization failed!");
    while (1);
  }
  
  Serial.println("Fingerprint sensor ready!");
}

void loop() {
  // Enroll a new fingerprint
  if (fingerPrintSensor.enrollAndGetTemplate(userTemplate) == 0) {
    Serial.println("Enrollment successful!");
    // Save userTemplate to database/EEPROM here
  }
  
  delay(3000);
  
  // Verify against stored template
  uint16_t matchScore = 0;
  if (fingerPrintSensor.matchWithTemplate(userTemplate, &matchScore) == 0) {
    Serial.printf("Match found! Confidence: %d/255\n", matchScore);
  }
  
  delay(3000);
}
```

## 📖 API Reference

### Constructor

```cpp
FingerPrint(Adafruit_Fingerprint* sensor)
```

Creates a new FingerPrint object.

**Parameters:**
- `sensor`: Pointer to an initialized Adafruit_Fingerprint object

---

### Core Methods

#### `void begin(uint32_t baudrate = 57600)`

Initializes the fingerprint sensor communication.

**Parameters:**
- `baudrate`: Communication speed (default: 57600)

---

#### `void setSerial(Stream* serial)`

Sets the hardware serial interface for low-level communication.

**Parameters:**
- `serial`: Pointer to the hardware serial stream (e.g., `&Serial2`)

**Note:** Must be called before `init()`

---

#### `bool init()`

Verifies sensor connection and retrieves sensor parameters.

**Returns:**
- `true`: Sensor detected and ready
- `false`: Sensor not found or password incorrect

**Output:** Prints sensor information (ID, capacity, template count)

---

### Enrollment & Matching

#### `uint8_t enrollAndGetTemplate(uint8_t templateOutput[TEMPLATE_SIZE])`

Enrolls a new fingerprint with two-scan verification and retrieves the template.

**Parameters:**
- `templateOutput`: Buffer to store the 512-byte template

**Returns:**
- `0`: Success - template stored in buffer
- `1`: Timeout waiting for finger
- `2`: Image conversion failed
- `3`: Fingerprints didn't match or model creation failed
- `4`: Template download failed

**Workflow:**
1. Prompts for first finger scan
2. Waits for finger removal
3. Prompts for second scan
4. Creates template model
5. Downloads template to buffer

---

#### `uint8_t matchWithTemplate(const uint8_t* storedTemplate, uint16_t* score)`

Matches a live fingerprint against a stored template.

**Parameters:**
- `storedTemplate`: Previously enrolled 512-byte template
- `score`: Pointer to store match confidence (0-255)

**Returns:**
- `0`: Match successful
- `1`: Timeout or image quality issues
- `3`: Template upload failed
- `4`: No match found
- `5`: Communication error

**Confidence Scores:**
- `>= 50`: Strong match (recommended threshold)
- `20-49`: Weak match
- `< 20`: Very low confidence

**Features:**
- Automatic retry on first failure
- Image quality validation
- Detailed error reporting

---

### Template Hashing

#### `uint8_t readAndHashFingerprint(uint8_t hashOutput[HASH_SIZE])`

Captures a fingerprint and generates its SHA-256 hash.

**Parameters:**
- `hashOutput`: Buffer to store 32-byte SHA-256 hash

**Returns:**
- `0`: Success
- Non-zero: Error (same codes as template reading)

**Use Case:** Store only hashes instead of full templates for privacy

---

#### `bool compareHashes(const uint8_t hash1[HASH_SIZE], const uint8_t hash2[HASH_SIZE])`

Compares two SHA-256 hashes.

**Parameters:**
- `hash1`: First 32-byte hash
- `hash2`: Second 32-byte hash

**Returns:**
- `true`: Hashes match
- `false`: Hashes differ

---

### Low-Level Methods

#### `uint8_t uploadTemplateToBuffer(const uint8_t* templateData, uint8_t bufferID)`

Uploads a template to sensor's character buffer.

**Parameters:**
- `templateData`: 512-byte template
- `bufferID`: Buffer ID (1 or 2)

**Returns:**
- `FINGERPRINT_OK`: Success
- Other: Error code from sensor

**Note:** Primarily used internally by `matchWithTemplate()`

---

## 💡 Usage Examples

### Example 1: Simple Enrollment & Verification

```cpp
#include <FingerPrint.h>

HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
FingerPrint fpSensor(&finger);

uint8_t template1[FingerPrint::TEMPLATE_SIZE];
bool enrolled = false;

void setup() {
  Serial.begin(115200);
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  
  fpSensor.begin(57600);
  fpSensor.setSerial(&mySerial);
  fpSensor.init();
}

void loop() {
  if (!enrolled) {
    Serial.println("=== ENROLLMENT ===");
    if (fpSensor.enrollAndGetTemplate(template1) == 0) {
      enrolled = true;
      Serial.println("User enrolled!");
    }
  } else {
    Serial.println("=== VERIFICATION ===");
    uint16_t score = 0;
    if (fpSensor.matchWithTemplate(template1, &score) == 0) {
      if (score >= 50) {
        Serial.println("✓ ACCESS GRANTED");
      } else {
        Serial.println("⚠ Low confidence match");
      }
    } else {
      Serial.println("✗ ACCESS DENIED");
    }
  }
  delay(3000);
}
```

### Example 2: Hash-Based Matching

```cpp
uint8_t hash1[FingerPrint::HASH_SIZE];
uint8_t hash2[FingerPrint::HASH_SIZE];

// Enroll
Serial.println("Enroll your finger...");
if (fpSensor.readAndHashFingerprint(hash1) == 0) {
  Serial.println("Hash stored!");
}

delay(5000);

// Verify
Serial.println("Verify your finger...");
if (fpSensor.readAndHashFingerprint(hash2) == 0) {
  if (fpSensor.compareHashes(hash1, hash2)) {
    Serial.println("✓ MATCH!");
  } else {
    Serial.println("✗ NO MATCH");
  }
}
```

### Example 3: Multi-User System

```cpp
#define MAX_USERS 10

uint8_t userTemplates[MAX_USERS][FingerPrint::TEMPLATE_SIZE];
int userCount = 0;

void enrollNewUser() {
  if (userCount >= MAX_USERS) {
    Serial.println("Database full!");
    return;
  }
  
  if (fpSensor.enrollAndGetTemplate(userTemplates[userCount]) == 0) {
    Serial.printf("User %d enrolled!\n", userCount);
    userCount++;
    // Save to EEPROM/SD card here
  }
}

bool verifyUser(int* userId) {
  uint16_t bestScore = 0;
  int bestMatch = -1;
  
  for (int i = 0; i < userCount; i++) {
    uint16_t score = 0;
    if (fpSensor.matchWithTemplate(userTemplates[i], &score) == 0) {
      if (score > bestScore) {
        bestScore = score;
        bestMatch = i;
      }
    }
  }
  
  if (bestMatch >= 0 && bestScore >= 50) {
    *userId = bestMatch;
    return true;
  }
  return false;
}
```

## 🔍 How It Works

### Template Structure

The library extracts **512-byte raw templates** from the sensor, which contain:
- Fingerprint feature points
- Minutiae data
- Sensor-specific encoding

### Matching Process

1. **Capture**: Live fingerprint → CharBuffer1
2. **Upload**: Stored template → CharBuffer2  
3. **Compare**: Sensor matches buffers using proprietary algorithm
4. **Result**: Confidence score (0-255)

### Why External Templates?

- **Scalability**: Store unlimited users in external database
- **Flexibility**: Use your own storage (SD card, cloud, EEPROM)
- **Portability**: Transfer templates between sensors
- **Security**: Keep templates separate from sensor

## ❌ Error Codes

| Code | Method | Meaning |
|------|--------|---------|
| `0` | All | Success |
| `1` | Enrollment/Match | Timeout waiting for finger |
| `2` | Enrollment | Image conversion failed |
| `3` | Enrollment | Fingerprints don't match |
| `3` | Match | Template upload failed |
| `4` | Enrollment | Template download failed |
| `4` | Match | No match found |
| `5` | Match | Communication error |

## 🐛 Troubleshooting

### Sensor Not Detected

- Check wiring (RX/TX may be swapped)
- Verify baud rate (usually 57600)
- Ensure sufficient power supply (fingerprint sensors draw significant current)
- Try sensor's default password: `0x00000000`

### Low Match Scores

- Clean the sensor surface
- Ensure dry fingers
- Press firmly without sliding
- Cover entire sensor area
- Re-enroll with better scans

### Enrollment Fails

- Wait for finger to be completely removed between scans
- Use same finger position for both scans
- Avoid partial fingerprints
- Check serial monitor for specific error codes

### Template Download Timeout

- Increase timeout in `_readByte()` (currently 2000ms)
- Check serial connection stability
- Ensure `setSerial()` was called before `init()`

## 📄 License

MIT License - Copyright (c) 2025 ELHARDA Anouar

See [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Built on [Adafruit Fingerprint Sensor Library](https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library)
- Uses mbedTLS for cryptographic functions
- Inspired by the need for flexible fingerprint authentication systems

## 📧 Contact & Support

- **Repository**: [github.com/morb0t/FingerPrint-Library](https://github.com/morb0t/FingerPrint-Library)
- **Issues**: Report bugs via GitHub Issues
- **Author**: ELHARDA Anouar (@morb0t)

---

**⭐ Star this repo if you find it useful!**