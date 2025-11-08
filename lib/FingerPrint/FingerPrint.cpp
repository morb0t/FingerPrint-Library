#include "FingerPrint.h"
#include <cstdint>

FingerPrint::FingerPrint(Adafruit_Fingerprint* sensor) {
  _sensor = sensor;
  _serial = nullptr;
}

void FingerPrint::setSerial(Stream* serial) {
  _serial = serial;
}

void FingerPrint::begin(uint32_t baudrate) {
  _sensor->begin(baudrate);
}

bool FingerPrint::init(){
  Serial.println("\nFingerprint sensor checking...");
  if(_sensor->verifyPassword()) {
    Serial.println("Fingerprint sensor detected!");

    _sensor->getParameters();
    Serial.print(F("Sys ID: 0x")); Serial.println(_sensor->system_id, HEX);
    Serial.print(F("Capacity: ")); Serial.println(_sensor->capacity);

    _sensor->getTemplateCount();
    Serial.print(F("Template count: ")); Serial.println(_sensor->templateCount);
    return true;
  } else {
    Serial.println("Fingerprint sensor not detected :(");
    return false;
  }
}

// Helper to read one byte with timeout
int16_t FingerPrint::_readByte(uint32_t timeout_ms) {
  if (!_serial) {
    Serial.println("Error: Serial not set!");
    return -1;
  }
  
  uint32_t start = millis();
  while (!_serial->available()) {
    if (millis() - start > timeout_ms) {
      return -1; // Timeout
    }
    yield();
  }
  return _serial->read();
}

uint8_t FingerPrint::_readRawTemplate(uint8_t* buffer) {
  Serial.println("Reading template using manual packet parsing...");
  
  // Send UpChar command (0x08, buffer 1)
  uint8_t packet[] = {FINGERPRINT_UPLOAD, 0x01};
  
  Serial.println("Sending UpChar command...");
  Adafruit_Fingerprint_Packet uploadCmd(FINGERPRINT_COMMANDPACKET, sizeof(packet), packet);
  _sensor->writeStructuredPacket(uploadCmd);
  
  // Read acknowledgment using library function
  uint8_t ackData[64];
  Adafruit_Fingerprint_Packet ackPacket(FINGERPRINT_ACKPACKET, 0, ackData);
  
  uint8_t result = _sensor->getStructuredPacket(&ackPacket);
  if (result != FINGERPRINT_OK) {
    Serial.printf("Failed to receive ACK: 0x%02X\n", result);
    return result;
  }
  
  if (ackPacket.data[0] != FINGERPRINT_OK) {
    Serial.printf("UpChar command failed: 0x%02X\n", ackPacket.data[0]);
    return ackPacket.data[0];
  }
  
  Serial.println("UpChar acknowledged, reading data packets manually...");
  
  uint16_t bytesRead = 0;
  bool endReceived = false;
  int packetCount = 0;
  
  // Now read packets manually byte by byte
  while (!endReceived && bytesRead < TEMPLATE_SIZE) {
    packetCount++;
    
    // Read packet header: 0xEF01 (2 bytes)
    int16_t b1 = _readByte(2000);
    int16_t b2 = _readByte(100);
    
    if (b1 < 0 || b2 < 0) {
      Serial.printf("Timeout reading packet header #%d\n", packetCount);
      if (bytesRead > 0) {
        Serial.printf("Using partial data: %d bytes\n", bytesRead);
        memset(buffer + bytesRead, 0, TEMPLATE_SIZE - bytesRead);
        return FINGERPRINT_OK;
      }
      return FINGERPRINT_TIMEOUT;
    }
    
    if (b1 != 0xEF || b2 != 0x01) {
      Serial.printf("Invalid packet header: %02X %02X\n", b1, b2);
      return FINGERPRINT_PACKETRECIEVEERR;
    }
    
    // Read address (4 bytes) - usually 0xFFFFFFFF
    for (int i = 0; i < 4; i++) {
      if (_readByte(100) < 0) {
        Serial.println("Timeout reading address");
        return FINGERPRINT_TIMEOUT;
      }
    }
    
    // Read packet identifier (1 byte)
    int16_t packetType = _readByte(100);
    if (packetType < 0) {
      Serial.println("Timeout reading packet type");
      return FINGERPRINT_TIMEOUT;
    }
    
    // Read length (2 bytes, big endian)
    int16_t len_high = _readByte(100);
    int16_t len_low = _readByte(100);
    if (len_high < 0 || len_low < 0) {
      Serial.println("Timeout reading length");
      return FINGERPRINT_TIMEOUT;
    }
    
    uint16_t packetLen = (len_high << 8) | len_low;
    
    Serial.printf("Packet #%d - Type: 0x%02X, Length: %d\n", 
                  packetCount, packetType, packetLen);
    
    if (packetType == FINGERPRINT_DATAPACKET || 
        packetType == FINGERPRINT_ENDDATAPACKET) {
      
      // Length includes checksum (2 bytes)
      uint16_t dataLen = packetLen - 2;
      
      // Read data bytes
      for (uint16_t i = 0; i < dataLen && bytesRead < TEMPLATE_SIZE; i++) {
        int16_t dataByte = _readByte(100);
        if (dataByte < 0) {
          Serial.printf("Timeout reading data byte %d\n", i);
          return FINGERPRINT_TIMEOUT;
        }
        buffer[bytesRead++] = (uint8_t)dataByte;
      }
      
      // Read checksum (2 bytes) and discard
      _readByte(100);
      _readByte(100);
      
      Serial.printf("Read %d bytes, total: %d/%d\n", dataLen, bytesRead, TEMPLATE_SIZE);
      
      if (packetType == FINGERPRINT_ENDDATAPACKET) {
        Serial.println("End packet received");
        endReceived = true;
      }
    } else if (packetType == FINGERPRINT_ACKPACKET) {
      Serial.println("Received ACK packet instead of data");
      // Read and discard ACK data
      for (uint16_t i = 0; i < packetLen; i++) {
        _readByte(100);
      }
      return FINGERPRINT_PACKETRECIEVEERR;
    } else {
      Serial.printf("Unexpected packet type: 0x%02X\n", packetType);
      return FINGERPRINT_PACKETRECIEVEERR;
    }
  }
  
  if (bytesRead < TEMPLATE_SIZE) {
    Serial.printf("Padding %d bytes with zeros\n", TEMPLATE_SIZE - bytesRead);
    memset(buffer + bytesRead, 0, TEMPLATE_SIZE - bytesRead);
  }
  
  Serial.printf("Download complete: %d bytes\n", bytesRead);
  
  // Print first 32 bytes
  Serial.print("First 32 bytes: ");
  for(int i = 0; i < 32 && i < bytesRead; i++) {
    if(buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
    if(i % 16 == 15) Serial.print("\n                ");
    else Serial.print(" ");
  }
  Serial.println();
  
  return FINGERPRINT_OK;
}

uint8_t FingerPrint::_getTemplateBytes(uint8_t templateBuffer[TEMPLATE_SIZE]) {
  uint8_t p = 0;

  Serial.println("Place finger on sensor...");
  while (_sensor->getImage() != FINGERPRINT_OK) {
    if(p++ > 200) {
      Serial.println("Timeout waiting for finger.");
      return 1;
    }
    delay(50);
  }

  Serial.println("Finger detected, generating template...");
  p = _sensor->image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Error generating template: 0x%02X\n", p);
    while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
      delay(100);
    }
    return 2;
  }

  Serial.println("Template in CharBuffer1, waiting before download...");
  delay(200); // Give sensor time to prepare

  Serial.println("Downloading template...");
  uint8_t result = _readRawTemplate(templateBuffer);
  if (result != FINGERPRINT_OK) {
    Serial.printf("Error downloading template: 0x%02X\n", result);
    while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
      delay(100);
    }
    return 3;
  }

  Serial.println("Template downloaded successfully.");
  while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
    delay(100);
  }
  Serial.println("Finger removed.");
  return 0; // Success
}

uint8_t FingerPrint::readAndHashFingerprint(uint8_t hashOutput[HASH_SIZE]) {
  uint8_t templateBuffer[TEMPLATE_SIZE];

  // Get the template bytes using the private helper
  uint8_t result = _getTemplateBytes(templateBuffer);
  if (result != 0) {
    return result; // Return error code from helper
  }

  // Hash the 512-byte template data using SHA-256
  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts_ret(&sha_ctx, 0); // 0 for SHA256
  mbedtls_sha256_update_ret(&sha_ctx, templateBuffer, TEMPLATE_SIZE);
  mbedtls_sha256_finish_ret(&sha_ctx, hashOutput);
  mbedtls_sha256_free(&sha_ctx);

  Serial.print("Template Hash (SHA-256): ");
  _printHex(hashOutput, HASH_SIZE);

  return 0; // Success
}

bool FingerPrint::compareHashes(const uint8_t hash1[HASH_SIZE], const uint8_t hash2[HASH_SIZE]) {
  return (memcmp(hash1, hash2, HASH_SIZE) == 0);
}

void FingerPrint::_printHex(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

/* uint8_t FingerPrint::uploadTemplateToBuffer(const uint8_t* templateData, uint8_t bufferID) { */
/*   Serial.printf("Uploading template to CharBuffer%d...\n", bufferID); */
  
/*   if (!_serial) { */
/*     Serial.println("Error: Serial not initialized"); */
/*     return FINGERPRINT_PACKETRECIEVEERR; */
/*   } */
  
/*   // Manual packet construction for DownChar command */
/*   // Packet format: Header(2) + Address(4) + PacketID(1) + Length(2) + Data + Checksum(2) */
  
/*   uint8_t header[] = {0xEF, 0x01}; // Header */
/*   uint8_t address[] = {0xFF, 0xFF, 0xFF, 0xFF}; // Default address */
  
/*   // Send command packet first */
/*   uint8_t cmdData[] = {0x09, bufferID}; // DownChar command */
/*   uint16_t cmdLen = sizeof(cmdData) + 2; // +2 for checksum */
  
/*   // Calculate checksum for command */
/*   uint16_t sum = 0x01 + cmdLen; // PacketID + Length */
/*   for (int i = 0; i < sizeof(cmdData); i++) { */
/*     sum += cmdData[i]; */
/*   } */
  
/*   // Send command packet */
/*   _serial->write(header, 2); */
/*   _serial->write(address, 4); */
/*   _serial->write(0x01); // Command packet */
/*   _serial->write((cmdLen >> 8) & 0xFF); */
/*   _serial->write(cmdLen & 0xFF); */
/*   _serial->write(cmdData, sizeof(cmdData)); */
/*   _serial->write((sum >> 8) & 0xFF); */
/*   _serial->write(sum & 0xFF); */
/*   _serial->flush(); */
  
/*   Serial.println("Command sent, waiting for ACK..."); */
/*   delay(100); */
  
/*   // Read ACK */
/*   uint8_t ackData[64]; */
/*   Adafruit_Fingerprint_Packet ackPacket(FINGERPRINT_ACKPACKET, 0, ackData); */
/*   uint8_t result = _sensor->getStructuredPacket(&ackPacket); */
  
/*   if (result != FINGERPRINT_OK || ackPacket.data[0] != FINGERPRINT_OK) { */
/*     Serial.printf("DownChar ACK failed: 0x%02X\n", ackPacket.data[0]); */
/*     return ackPacket.data[0]; */
/*   } */
  
/*   Serial.println("ACK received, sending data packets..."); */
  
/*   // Send template data in packets */
/*   const uint16_t PACKET_SIZE = 128; */
/*   uint16_t bytesSent = 0; */
  
/*   while (bytesSent < TEMPLATE_SIZE) { */
/*     uint16_t chunkSize = min((uint16_t)(TEMPLATE_SIZE - bytesSent), PACKET_SIZE); */
/*     bool isLastPacket = (bytesSent + chunkSize >= TEMPLATE_SIZE); */
    
/*     uint8_t packetType = isLastPacket ? FINGERPRINT_ENDDATAPACKET : FINGERPRINT_DATAPACKET; */
/*     uint16_t dataLen = chunkSize + 2; // +2 for checksum */
    
/*     // Calculate checksum */
/*     sum = packetType + dataLen; */
/*     for (uint16_t i = 0; i < chunkSize; i++) { */
/*       sum += templateData[bytesSent + i]; */
/*     } */
    
/*     // Send data packet */
/*     _serial->write(header, 2); */
/*     _serial->write(address, 4); */
/*     _serial->write(packetType); */
/*     _serial->write((dataLen >> 8) & 0xFF); */
/*     _serial->write(dataLen & 0xFF); */
/*     _serial->write(templateData + bytesSent, chunkSize); */
/*     _serial->write((sum >> 8) & 0xFF); */
/*     _serial->write(sum & 0xFF); */
/*     _serial->flush(); */
    
/*     bytesSent += chunkSize; */
/*     Serial.printf("Sent %d/%d bytes\n", bytesSent, TEMPLATE_SIZE); */
    
/*     delay(20); // Small delay between packets */
/*   } */
  
/*   Serial.println("All data packets sent"); */
/*   return FINGERPRINT_OK; */
/* } */

/* // Match current fingerprint against a stored template */
/* uint8_t FingerPrint::matchWithTemplate(const uint8_t* storedTemplate, uint16_t* score) { */
/*   Serial.println("\n---- Matching Fingerprint ----"); */
  
/*   // Step 1: Capture current fingerprint */
/*   Serial.println("Place finger on sensor..."); */
/*   uint8_t p = 0; */
/*   uint8_t timeout = 0; */
/*   while (_sensor->getImage() != FINGERPRINT_OK) { */
/*     if (timeout++ > 200) { */
/*       Serial.println("Timeout waiting for finger"); */
/*       return 1; */
/*     } */
/*     delay(50); */
/*   } */
  
/*   Serial.println("Finger detected, converting to template..."); */
/*   p = _sensor->image2Tz(1); // Store in CharBuffer1 */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.printf("Error converting image: 0x%02X\n", p); */
/*     return 2; */
/*   } */
  
/*   // Step 2: Upload stored template to CharBuffer2 */
/*   Serial.println("Uploading stored template to sensor..."); */
/*   p = uploadTemplateToBuffer(storedTemplate, 2); */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.println("Failed to upload template"); */
/*     return 3; */
/*   } */
  
/*   // Step 3: Match the two templates using RegModel (0x05 command) */
/*   Serial.println("Comparing templates..."); */
  
/*   // Send match command manually */
/*   uint8_t matchCmd[] = {0x03}; // Match command (compare CharBuffer1 and CharBuffer2) */
/*   Adafruit_Fingerprint_Packet matchPacket(FINGERPRINT_COMMANDPACKET, sizeof(matchCmd), matchCmd); */
/*   _sensor->writeStructuredPacket(matchPacket); */
  
/*   // Read match response */
/*   uint8_t matchAckData[64]; */
/*   Adafruit_Fingerprint_Packet matchAck(FINGERPRINT_ACKPACKET, 0, matchAckData); */
  
/*   p = _sensor->getStructuredPacket(&matchAck); */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.printf("Failed to get match response: 0x%02X\n", p); */
/*     return 5; */
/*   } */
  
/*   uint8_t matchResult = matchAck.data[0]; */
  
/*   if (matchResult == FINGERPRINT_OK) { */
/*     // Extract confidence score (bytes 1-2, big endian) */
/*     *score = ((uint16_t)matchAck.data[1] << 8) | matchAck.data[2]; */
/*     Serial.printf("✓ Match found! Confidence: %d\n", *score); */
    
/*     // Wait for finger removal */
/*     while (_sensor->getImage() != FINGERPRINT_NOFINGER) { */
/*       delay(100); */
/*     } */
/*     Serial.println("Finger removed"); */
/*     return 0; // Success */
/*   } else { */
/*     Serial.printf("✗ No match (result: 0x%02X)\n", matchResult); */
    
/*     // Wait for finger removal */
/*     while (_sensor->getImage() != FINGERPRINT_NOFINGER) { */
/*       delay(100); */
/*     } */
/*     Serial.println("Finger removed"); */
/*     return 4; // No match */
/*   } */
/* } */

/* // Enhanced enrollment that returns the template */
/* uint8_t FingerPrint::enrollAndGetTemplate(uint8_t templateOutput[TEMPLATE_SIZE]) { */
/*   Serial.println("\n---- Enrolling New Fingerprint ----"); */
  
/*   // Get first scan */
/*   Serial.println("Place finger on sensor (scan 1/2)..."); */
/*   uint8_t p = 0; */
/*   uint8_t timeout = 0; */
/*   while (_sensor->getImage() != FINGERPRINT_OK) { */
/*     if (timeout++ > 200) { */
/*       Serial.println("Timeout waiting for finger"); */
/*       return 1; */
/*     } */
/*     delay(50); */
/*   } */
  
/*   Serial.println("Converting image 1..."); */
/*   p = _sensor->image2Tz(1); */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.printf("Error converting image: 0x%02X\n", p); */
/*     return 2; */
/*   } */
  
/*   Serial.println("Remove finger"); */
/*   delay(2000); */
/*   while (_sensor->getImage() != FINGERPRINT_NOFINGER) { */
/*     delay(100); */
/*   } */
  
/*   // Get second scan */
/*   Serial.println("Place same finger again (scan 2/2)..."); */
/*   timeout = 0; */
/*   while (_sensor->getImage() != FINGERPRINT_OK) { */
/*     if (timeout++ > 200) { */
/*       Serial.println("Timeout waiting for finger"); */
/*       return 1; */
/*     } */
/*     delay(50); */
/*   } */
  
/*   Serial.println("Converting image 2..."); */
/*   p = _sensor->image2Tz(2); */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.printf("Error converting image: 0x%02X\n", p); */
/*     return 2; */
/*   } */
  
/*   // Create model from both scans */
/*   Serial.println("Creating fingerprint model..."); */
/*   p = _sensor->createModel(); */
/*   if (p == FINGERPRINT_OK) { */
/*     Serial.println("✓ Fingerprints matched!"); */
/*   } else if (p == FINGERPRINT_ENROLLMISMATCH) { */
/*     Serial.println("✗ Fingerprints did not match. Try again."); */
/*     return 3; */
/*   } else { */
/*     Serial.printf("Error creating model: 0x%02X\n", p); */
/*     return 3; */
/*   } */
  
/*   // Download the created model */
/*   Serial.println("Downloading template..."); */
/*   p = _readRawTemplate(templateOutput); */
/*   if (p != FINGERPRINT_OK) { */
/*     Serial.println("Failed to download template"); */
/*     return 4; */
/*   } */
  
/*   Serial.println("✓ Enrollment successful! Template ready for storage."); */
  
/*   // Wait for finger removal */
/*   while (_sensor->getImage() != FINGERPRINT_NOFINGER) { */
/*     delay(100); */
/*   } */
/*   Serial.println("Finger removed"); */
  
/*   return 0; // Success */
/* } */

uint8_t FingerPrint::uploadTemplateToBuffer(const uint8_t* templateData, uint8_t bufferID) {
  Serial.printf("Uploading template to CharBuffer%d...\n", bufferID);
  
  if (!_serial) {
    Serial.println("Error: Serial not initialized");
    return FINGERPRINT_PACKETRECIEVEERR;
  }
  
  // Manual packet construction for DownChar command
  // Packet format: Header(2) + Address(4) + PacketID(1) + Length(2) + Data + Checksum(2)
  
  uint8_t header[] = {0xEF, 0x01}; // Header
  uint8_t address[] = {0xFF, 0xFF, 0xFF, 0xFF}; // Default address
  
  // Send command packet first
  uint8_t cmdData[] = {0x09, bufferID}; // DownChar command
  uint16_t cmdLen = sizeof(cmdData) + 2; // +2 for checksum
  
  // Calculate checksum for command
  uint16_t sum = 0x01 + cmdLen; // PacketID + Length
  for (int i = 0; i < sizeof(cmdData); i++) {
    sum += cmdData[i];
  }
  
  // Send command packet
  _serial->write(header, 2);
  _serial->write(address, 4);
  _serial->write(0x01); // Command packet
  _serial->write((cmdLen >> 8) & 0xFF);
  _serial->write(cmdLen & 0xFF);
  _serial->write(cmdData, sizeof(cmdData));
  _serial->write((sum >> 8) & 0xFF);
  _serial->write(sum & 0xFF);
  _serial->flush();
  
  Serial.println("Command sent, waiting for ACK...");
  delay(100);
  
  // Read ACK
  uint8_t ackData[64];
  Adafruit_Fingerprint_Packet ackPacket(FINGERPRINT_ACKPACKET, 0, ackData);
  uint8_t result = _sensor->getStructuredPacket(&ackPacket);
  
  if (result != FINGERPRINT_OK || ackPacket.data[0] != FINGERPRINT_OK) {
    Serial.printf("DownChar ACK failed: 0x%02X\n", ackPacket.data[0]);
    return ackPacket.data[0];
  }
  
  Serial.println("ACK received, sending data packets...");
  
  // Send template data in packets
  const uint16_t PACKET_SIZE = 128;
  uint16_t bytesSent = 0;
  
  while (bytesSent < TEMPLATE_SIZE) {
    uint16_t chunkSize = min((uint16_t)(TEMPLATE_SIZE - bytesSent), PACKET_SIZE);
    bool isLastPacket = (bytesSent + chunkSize >= TEMPLATE_SIZE);
    
    uint8_t packetType = isLastPacket ? FINGERPRINT_ENDDATAPACKET : FINGERPRINT_DATAPACKET;
    uint16_t dataLen = chunkSize + 2; // +2 for checksum
    
    // Calculate checksum
    sum = packetType + dataLen;
    for (uint16_t i = 0; i < chunkSize; i++) {
      sum += templateData[bytesSent + i];
    }
    
    // Send data packet
    _serial->write(header, 2);
    _serial->write(address, 4);
    _serial->write(packetType);
    _serial->write((dataLen >> 8) & 0xFF);
    _serial->write(dataLen & 0xFF);
    _serial->write(templateData + bytesSent, chunkSize);
    _serial->write((sum >> 8) & 0xFF);
    _serial->write(sum & 0xFF);
    _serial->flush();
    
    bytesSent += chunkSize;
    Serial.printf("Sent %d/%d bytes\n", bytesSent, TEMPLATE_SIZE);
    
    delay(20); // Small delay between packets
  }
  
  Serial.println("All data packets sent");
  return FINGERPRINT_OK;
}

// Match current fingerprint against a stored template
uint8_t FingerPrint::matchWithTemplate(const uint8_t* storedTemplate, uint16_t* score) {
  Serial.println("\n---- Matching Fingerprint ----");
  
  // Step 1: Capture current fingerprint with better guidance
  Serial.println("Place finger firmly on sensor...");
  Serial.println("(Press down evenly, avoid sliding)");
  
  uint8_t p = 0;
  uint8_t timeout = 0;
  uint8_t imageQuality = 0;
  
  // Try to get a good quality image
  while (true) {
    p = _sensor->getImage();
    if (p == FINGERPRINT_OK) {
      // Check image quality by attempting conversion
      uint8_t tempResult = _sensor->image2Tz(1);
      if (tempResult == FINGERPRINT_OK) {
        Serial.println("✓ Good quality image captured");
        break;
      } else if (tempResult == FINGERPRINT_IMAGEMESS) {
        Serial.println("Image too messy, try again...");
        delay(500);
        timeout++;
      } else if (tempResult == FINGERPRINT_FEATUREFAIL) {
        Serial.println("Could not find features, reposition finger...");
        delay(500);
        timeout++;
      } else {
        // Image converted successfully
        break;
      }
    }
    
    if (timeout++ > 200) {
      Serial.println("Timeout waiting for good fingerprint");
      return 1;
    }
    delay(50);
  }
  
  Serial.println("Finger detected, converting to template...");
  // Image already converted above, so CharBuffer1 is ready
  
  // Step 2: Upload stored template to CharBuffer2
  Serial.println("Uploading stored template to sensor...");
  p = uploadTemplateToBuffer(storedTemplate, 2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Failed to upload template");
    return 3;
  }
  
  // Step 3: Match the two templates using RegModel (0x05 command)
  Serial.println("Comparing templates...");
  
  // Send match command manually
  uint8_t matchCmd[] = {0x03}; // Match command (compare CharBuffer1 and CharBuffer2)
  Adafruit_Fingerprint_Packet matchPacket(FINGERPRINT_COMMANDPACKET, sizeof(matchCmd), matchCmd);
  _sensor->writeStructuredPacket(matchPacket);
  
  // Read match response
  uint8_t matchAckData[64];
  Adafruit_Fingerprint_Packet matchAck(FINGERPRINT_ACKPACKET, 0, matchAckData);
  
  p = _sensor->getStructuredPacket(&matchAck);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Failed to get match response: 0x%02X\n", p);
    return 5;
  }
  
  uint8_t matchResult = matchAck.data[0];
  
  if (matchResult == FINGERPRINT_OK) {
    // Extract confidence score (bytes 1-2, big endian)
    *score = ((uint16_t)matchAck.data[1] << 8) | matchAck.data[2];
    Serial.printf("✓ Match found! Confidence: %d\n", *score);
    
    // Wait for finger removal
    while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
      delay(100);
    }
    Serial.println("Finger removed");
    return 0; // Success
  } else if (matchResult == FINGERPRINT_ENROLLMISMATCH) {
    // Try one more time with a fresh scan
    Serial.println("First attempt failed, trying once more...");
    
    // Wait a moment
    delay(500);
    
    // Get another image
    Serial.println("Keep finger on sensor...");
    timeout = 0;
    while (_sensor->getImage() != FINGERPRINT_OK) {
      if (timeout++ > 100) {
        Serial.println("✗ Verification failed - no match");
        return 4;
      }
      delay(50);
    }
    
    p = _sensor->image2Tz(1);
    if (p != FINGERPRINT_OK) {
      Serial.println("✗ Could not process second scan");
      return 4;
    }
    
    // Try matching again
    _sensor->writeStructuredPacket(matchPacket);
    p = _sensor->getStructuredPacket(&matchAck);
    
    if (p == FINGERPRINT_OK && matchAck.data[0] == FINGERPRINT_OK) {
      *score = ((uint16_t)matchAck.data[1] << 8) | matchAck.data[2];
      Serial.printf("✓ Match found on retry! Confidence: %d\n", *score);
      
      while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
      }
      Serial.println("Finger removed");
      return 0;
    }
    
    Serial.println("✗ No match after retry");
    while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
      delay(100);
    }
    Serial.println("Finger removed");
    return 4;
  } else {
    Serial.printf("✗ No match (result: 0x%02X)\n", matchResult);
    
    // Wait for finger removal
    while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
      delay(100);
    }
    Serial.println("Finger removed");
    return 4; // No match
  }
}

// Enhanced enrollment that returns the template
uint8_t FingerPrint::enrollAndGetTemplate(uint8_t templateOutput[TEMPLATE_SIZE]) {
  Serial.println("\n---- Enrolling New Fingerprint ----");
  
  // Get first scan
  Serial.println("Place finger on sensor (scan 1/2)...");
  uint8_t p = 0;
  uint8_t timeout = 0;
  while (_sensor->getImage() != FINGERPRINT_OK) {
    if (timeout++ > 200) {
      Serial.println("Timeout waiting for finger");
      return 1;
    }
    delay(50);
  }
  
  Serial.println("Converting image 1...");
  p = _sensor->image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Error converting image: 0x%02X\n", p);
    return 2;
  }
  
  Serial.println("Remove finger");
  delay(2000);
  while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
    delay(100);
  }
  
  // Get second scan
  Serial.println("Place same finger again (scan 2/2)...");
  timeout = 0;
  while (_sensor->getImage() != FINGERPRINT_OK) {
    if (timeout++ > 200) {
      Serial.println("Timeout waiting for finger");
      return 1;
    }
    delay(50);
  }
  
  Serial.println("Converting image 2...");
  p = _sensor->image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Error converting image: 0x%02X\n", p);
    return 2;
  }
  
  // Create model from both scans
  Serial.println("Creating fingerprint model...");
  p = _sensor->createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("✓ Fingerprints matched!");
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("✗ Fingerprints did not match. Try again.");
    return 3;
  } else {
    Serial.printf("Error creating model: 0x%02X\n", p);
    return 3;
  }
  
  // Download the created model
  Serial.println("Downloading template...");
  p = _readRawTemplate(templateOutput);
  if (p != FINGERPRINT_OK) {
    Serial.println("Failed to download template");
    return 4;
  }
  
  Serial.println("✓ Enrollment successful! Template ready for storage.");
  
  // Wait for finger removal
  while (_sensor->getImage() != FINGERPRINT_NOFINGER) {
    delay(100);
  }
  Serial.println("Finger removed");
  
  return 0; // Success
}
