#ifndef FINGERPRINT_H
#define FINGERPRINT_H
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <cstddef>
#include <cstdint>
#include <mbedtls/sha256.h>

// create a fingerprint object
class FingerPrint {
  public:
    static const uint16_t HASH_SIZE = 32; // SHA-256 hash size in bytes
    static const uint16_t TEMPLATE_SIZE = 512;
    FingerPrint(Adafruit_Fingerprint* sensor);
    void begin(uint32_t baudrate = 57600);
    void setSerial(Stream* serial);  // ADD THIS LINE
    bool init();
    uint8_t readAndHashFingerprint(uint8_t hashOutput[HASH_SIZE]);
    bool compareHashes(const uint8_t hash1[HASH_SIZE], const uint8_t hash2[HASH_SIZE]);

	uint8_t enrollAndGetTemplate(uint8_t templateOutput[TEMPLATE_SIZE]);
    uint8_t uploadTemplateToBuffer(const uint8_t* templateData, uint8_t bufferID);
    uint8_t matchWithTemplate(const uint8_t* storedTemplate, uint16_t* score);
  private:
    Adafruit_Fingerprint* _sensor;
    Stream* _serial;  // ADD THIS LINE
    uint8_t _getTemplateBytes(uint8_t templateBuffer[TEMPLATE_SIZE]);
    uint8_t _readRawTemplate(uint8_t* buffer);
    int16_t _readByte(uint32_t timeout_ms);
    void _printHex(const uint8_t* buffer, size_t size);
};
#endif // FINGERPRINT_H
