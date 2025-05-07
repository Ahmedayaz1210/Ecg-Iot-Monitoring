#include <Arduino.h>
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/aes.h"
#include <Preferences.h>
#include "secrets.h"  // Add this include for AWS credentials
#include <WiFiClientSecure.h>  // Add for WiFi and AWS connection
#include <PubSubClient.h>  // Add for MQTT
#include <ArduinoJson.h>  // Add for JSON processing

// Using pin 36 for ECG sensor
#define ECG_PIN 36  // ESP32 ADC pin

#define RO_MODE true
#define RW_MODE false
#define MQTT_MAX_PACKET_SIZE 512  // Add buffer size for MQTT

// MQTT topics
#define AWS_IOT_PUBLISH_TOPIC "ecg/data"
#define AWS_IOT_SUBSCRIBE_TOPIC "ecg/results"

// Creating secure WiFi and MQTT clients
WiFiClientSecure wifiClient = WiFiClientSecure();
PubSubClient mqttClient(wifiClient);

// Session ID to track messages
String session_id;

// Preferences library to store the encryption key in non-volatile storage
Preferences encryptionKeyPref;

// Arrays for ECG data storage
// Reason why it's 187 is because the dataset model is trained on has 187 samples per batch
float ecg_readings[187];
float ecg_normalized[187];
int reading_index = 0;

// State machine to manage different phases
enum State {COLLECTING, PROCESSING, SENDING, WAITING};
State current_state = COLLECTING;

// For timing samples
unsigned long last_reading_time = 0;
const int sampling_interval = 10; // 10ms = 100Hz sampling rate

// Variable definition for AES key generation
mbedtls_ctr_drbg_context ctr_drbg;
mbedtls_entropy_context entropy;
unsigned char key[32];

// The personalization string needs to unique for the project
const char *pers = "ecg_encryption_key_generation";
int ret;

// Variable definition for AES Encryption
// AES context structure that will hold all the internal state needed for AES encryption/decryption operations. 
mbedtls_aes_context aes;

unsigned char iv[16];
unsigned char input[800];  // Increased to handle 187 floats (748 bytes) + padding
unsigned char output[800]; // Same size as input
unsigned char encrypted_output[800]; // Buffer to hold the encrypted data
unsigned char encryption_IV_data[800]; // Buffer to hold IV (16 bytes) + encrypted data

size_t input_len = 0;
size_t output_len = 0;

// Function prototypes
bool generateAESkey();
bool generateRandomIV();
void float_to_bytes(float arr[], unsigned char bytes[]);
void normalizeECGData();
void encryptAndSendData();
size_t apply_pkcs7_padding(unsigned char *input, size_t input_len, unsigned char *output);
void encrypt_data(int padded_len);
void package_data(unsigned char *encrypted_output, unsigned char *encryption_IV_data, int padded_len);
bool loadKey();
void storeKey();
void connectToWiFi();
void connectToAWS();
void publishIV(unsigned char* iv);
void publishEncryptedDataChunks(unsigned char* encrypted_data, size_t data_length);
bool ensureConnected();
void messageCallback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ECG Monitoring System...");
  
  // Initialize sensor
  pinMode(ECG_PIN, INPUT);
  
  // Initialize arrays
  for (int i = 0; i < 187; i++) {
    ecg_readings[i] = 0;
    ecg_normalized[i] = 0;
  }
  
  // Initialize the random number generator
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  
  // Seed the random number generator
  if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (unsigned char*)pers, strlen(pers))) != 0) {
    Serial.printf("Failed to seed random generator: -0x%04x\n", -ret);
    return;
  }
  
  // Try to load the key from storage, generate a new one if none exists
  if (!loadKey()) {
    if (!generateAESkey()) {
      Serial.println("Key generation failed!");
      return;
    }
    storeKey(); // Store the newly generated key
  }

  // Generate a unique session ID
  session_id = String(random(0xffff), HEX) + String(millis(), HEX);
  
  // Connect to WiFi and AWS
  connectToWiFi();
  connectToAWS();
}

void normalizeECGData() {
  // Finding min and max in dataset
  float min_value = ecg_readings[0];
  float max_value = ecg_readings[0];
  
  for (int i = 0; i < 187; i++) {
    if (ecg_readings[i] < min_value) {
      min_value = ecg_readings[i];
    }
    if (ecg_readings[i] > max_value) {
      max_value = ecg_readings[i];
    }
  }
  
  // Normalizing data to 0-1 range
  for (int i = 0; i < 187; i++) {
    ecg_normalized[i] = (ecg_readings[i] - min_value) / (max_value - min_value);
  }
}

void encryptAndSendData() {
  // Generate a new random IV for this encryption operation
  if (!generateRandomIV()) {
    Serial.println("IV generation failed!");
    return;
  }
  
  // Convert normalized ECG data to bytes
  float_to_bytes(ecg_normalized, input);
  
  // Apply PKCS#7 padding
  output_len = apply_pkcs7_padding(input, input_len, output);
  Serial.print("After padding, data length: ");
  Serial.println(output_len);
  
  // Encrypt the data
  encrypt_data(output_len);
  
  // Package the IV with the encrypted data
  package_data(encrypted_output, encryption_IV_data, output_len);
  
  // Print normalized data for debugging
  Serial.println("ECG Data Batch:");
  Serial.print("[");
  for (int i = 0; i < 187; i++) {
    Serial.print(ecg_normalized[i], 4);
    if (i < 186) {
      Serial.print(", ");
    }
  }
  Serial.println("]");
  
  // Send data to AWS IoT Core
  // First, publish IV separately
  publishIV(encryption_IV_data);
  
  // Then, publish encrypted data in chunks
  publishEncryptedDataChunks(encryption_IV_data + 16, output_len);
}

bool generateAESkey() {
  // Generate the random key - RNG is already initialized in setup()
  if((ret = mbedtls_ctr_drbg_random(&ctr_drbg, key, 32)) != 0) {
    Serial.printf("Failed to generate random key: -0x%04x\n", -ret);
    return false;
  }
  
  Serial.println("AES-256 key generated successfully");
  Serial.print("Key: ");
  for (int i = 0; i < 32; i++) {
    Serial.printf("%02X ", key[i]);
  }
  Serial.println();
  return true;
}

bool generateRandomIV() {
  // Generate a random IV (initialization vector) for AES encryption
  if((ret = mbedtls_ctr_drbg_random(&ctr_drbg, iv, 16)) != 0) {
    Serial.printf("Failed to generate random IV: -0x%04x\n", -ret);
    return false;
  }

  // Immediately save the IV to the beginning of the package buffer
  memcpy(encryption_IV_data, iv, 16);

  Serial.println("IV generated successfully");
  Serial.print("IV: ");
  for (int i = 0; i < sizeof(iv); i++) {
    Serial.printf("%02X ", iv[i]);
  }
  Serial.println();
  return true;
}

void float_to_bytes(float arr[], unsigned char bytes[]) {
  input_len = 187 * sizeof(float); // Calculate the size of the input array in bytes

  // Copy the float array to the byte array
  memcpy(bytes, arr, input_len);

  Serial.println("Converted float array to bytes");
  Serial.print("Byte length: ");
  Serial.println(input_len);
}

size_t apply_pkcs7_padding(unsigned char *input, size_t input_len, unsigned char *output) {
  // Calculate the number of padding bytes needed
  size_t padding_len = 16 - (input_len % 16);
  if (padding_len == 0) {
    padding_len = 16;
  }

  // Copy the original input to the output buffer
  memcpy(output, input, input_len);

  // Add padding bytes to the output buffer
  for (size_t i = input_len; i < input_len + padding_len; i++) {
    output[i] = padding_len;
  }

  Serial.println("PKCS#7 padding applied");
  
  return input_len + padding_len;
}

void encrypt_data(int padded_len) {
  // Initialize the AES context
  mbedtls_aes_init(&aes);

  // Set the AES encryption key
  mbedtls_aes_setkey_enc(&aes, key, 256);

  // Encrypt the data using AES-CBC
  if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv, output, encrypted_output) != 0) {
    Serial.println("Encryption failed!");
    return;
  }

  Serial.println("Encryption successful!");
  
  // Free the AES context when done
  mbedtls_aes_free(&aes);
}

void package_data(unsigned char *encrypted_output, unsigned char *encryption_IV_data, int padded_len) {
  // Copy the encrypted data to the encryption_IV_data buffer after the IV
  memcpy(encryption_IV_data + 16, encrypted_output, padded_len);

  Serial.println("Data packaged with IV for transmission");
}

// Function to load AES Encryption key from non-volatile storage
bool loadKey() {
  encryptionKeyPref.begin("encryption_key", RO_MODE); // Open the preferences with read-only access
  
  if (!encryptionKeyPref.isKey("key")) {
    Serial.println("No encryption key found in storage. Will generate a new one.");
    encryptionKeyPref.end(); // Close the preferences
    return false;
  }
  
  size_t keySize = encryptionKeyPref.getBytesLength("key"); // Get the size of the stored key
  if (keySize != sizeof(key)) {
    Serial.println("Key size mismatch! Expected 32 bytes, found " + String(keySize) + " bytes.");
    encryptionKeyPref.end(); // Close the preferences
    return false;
  }
  
  encryptionKeyPref.getBytes("key", key, sizeof(key)); // Load the key from non-volatile storage
  encryptionKeyPref.end(); // Close the preferences
  
  Serial.println("Key loaded successfully.");
  Serial.print("Loaded Key: ");
  for (int i = 0; i < sizeof(key); i++) {
    Serial.printf("%02X ", key[i]);
  }
  Serial.println();
  
  return true;
}

// Function to store AES Encryption key in non-volatile storage
void storeKey() {
  encryptionKeyPref.begin("encryption_key", RW_MODE); // Open the preferences with read-write access
  encryptionKeyPref.clear(); // Clear any existing data in the preferences
  encryptionKeyPref.putBytes("key", key, sizeof(key)); // Store the key in non-volatile storage
  encryptionKeyPref.end(); // Close the preferences
  
  Serial.println("Key stored successfully.");
  Serial.print("Stored Key: ");
  for (int i = 0; i < sizeof(key); i++) {
    Serial.printf("%02X ", key[i]);
  }
  Serial.println();
}

// Connect to WiFi
void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Callback for received MQTT messages
void messageCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  
  // Parse the payload if needed
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.println(message);
  
  // You can add logic here to process commands or results
}

// Connect to AWS IoT
void connectToAWS() {
  // Configure certificates
  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(AWS_CERT_CRT);
  wifiClient.setPrivateKey(AWS_CERT_PRIVATE);
  
  // Configure MQTT client
  mqttClient.setServer(AWS_IOT_ENDPOINT, 8883);
  mqttClient.setCallback(messageCallback);
  
  Serial.print("Connecting to AWS IoT Core... ");
  
  // Create a unique client ID
  String clientId = "ESP32_ECG_Device-";
  clientId += String(random(0xffff), HEX);
  
  while (!mqttClient.connected()) {
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Connected!");
      
      // Subscribe to topics if needed
      mqttClient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void publishIV(unsigned char* iv) {
  // Create a JSON document
  JsonDocument doc;
  
  // Add metadata
  doc["device_id"] = "ESP32_ECG_Device";
  doc["timestamp"] = millis();
  doc["message_type"] = "iv";
  doc["sample_id"] = "sample_1"; // could make this dynamic later
  doc["session_id"] = session_id; // Add session ID to differentiate between IVs
  
  // Convert IV to hex string
  char encoded_iv[33]; // 16 bytes = 32 hex chars + null terminator
  for (int i = 0; i < 16; i++) {
    sprintf(&encoded_iv[i * 2], "%02x", iv[i]);
  }
  encoded_iv[32] = '\0';
  
  doc["iv"] = encoded_iv;
  
  // Serialize JSON to string
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  
  Serial.print("Publishing IV: ");
  Serial.println(jsonBuffer);
  
  // Publish to AWS IoT
  if (mqttClient.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer)) {
    Serial.println("IV published successfully");
  } else {
    Serial.print("Failed to publish IV, error code: ");
    Serial.println(mqttClient.state());
  }
}

bool ensureConnected() {
  if (!mqttClient.connected()) {
    Serial.println("MQTT connection lost, attempting to reconnect...");
    
    // Create a unique client ID
    String clientId = "ESP32_ECG_Device-";
    clientId += String(random(0xffff), HEX);
    
    // Try to reconnect with backoff
    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
      attempts++;
      Serial.print("Connection attempt ");
      Serial.print(attempts);
      Serial.print("... ");
      
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println("Connected!");
        mqttClient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
        return true;
      } else {
        Serial.print("Failed, rc=");
        Serial.println(mqttClient.state());
        delay(1000 * attempts); // Backoff with increasing delay
      }
    }
    
    if (!mqttClient.connected()) {
      Serial.println("Failed to reconnect after multiple attempts");
      return false;
    }
  }
  
  return true;
}

void publishEncryptedDataChunks(unsigned char* encrypted_data, size_t data_length) {
  // Define chunk size (smaller chunks for better reliability)
  const size_t CHUNK_SIZE = 32;
  
  // Calculate number of chunks needed
  int total_chunks = (data_length + CHUNK_SIZE - 1) / CHUNK_SIZE;
  
  Serial.print("Sending data in ");
  Serial.print(total_chunks);
  Serial.println(" chunks");
  
  // Send each chunk
  for (int chunk_index = 0; chunk_index < total_chunks; chunk_index++) {
    // Ensure MQTT is connected
    if (!ensureConnected()) {
      Serial.println("Cannot send chunk - connection failed");
      delay(1000);
      continue;
    }
    
    // Calculate start position and length for this chunk
    size_t chunk_start = chunk_index * CHUNK_SIZE;
    size_t chunk_length = min(CHUNK_SIZE, data_length - chunk_start);
    
    // Create a JSON document
    JsonDocument doc;
    
    // Add metadata
    doc["device_id"] = "ESP32_ECG_Device";
    doc["timestamp"] = millis();
    doc["message_type"] = "data_chunk";
    doc["sample_id"] = "sample_1";
    doc["session_id"] = session_id; 
    doc["chunk_index"] = chunk_index + 1;
    doc["total_chunks"] = total_chunks;
    
    // Convert this chunk of data to hex string
    char encoded_chunk[CHUNK_SIZE * 2 + 1];
    for (size_t i = 0; i < chunk_length; i++) {
      sprintf(&encoded_chunk[i * 2], "%02x", encrypted_data[chunk_start + i]);
    }
    encoded_chunk[chunk_length * 2] = '\0';
    
    doc["chunk_data"] = encoded_chunk;
    
    // Serialize JSON to string
    char jsonBuffer[300]; // Buffer size
    serializeJson(doc, jsonBuffer);
    
    Serial.print("Publishing chunk ");
    Serial.print(chunk_index + 1);
    Serial.print(" of ");
    Serial.print(total_chunks);
    Serial.println();
    
    // Publish to AWS IoT
    if (mqttClient.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer)) {
      Serial.println("Chunk published successfully");
    } else {
      Serial.print("Failed to publish chunk, error code: ");
      Serial.println(mqttClient.state());
    }
    
    // Process incoming messages and maintain the connection
    mqttClient.loop();
    
    // Increase delay between chunks
    delay(800);
  }
  
  Serial.println("All chunks sent");
}

void loop() {
  switch(current_state) {
    case COLLECTING:
      // Time to get a new reading?
      if (millis() - last_reading_time >= sampling_interval) {
        last_reading_time = millis();
        
        // Get ECG reading
        int raw_reading = analogRead(ECG_PIN);
        
        // Save to array
        ecg_readings[reading_index] = raw_reading;
        reading_index++;
        
        // Got all samples?
        if (reading_index >= 187) {
          current_state = PROCESSING;
          Serial.println("Got 187 samples. Processing now...");
        }
      }
      break;
    
    case PROCESSING:
      // Normalize ECG data
      normalizeECGData();
      Serial.println("Normalization done. Ready to send...");
      current_state = SENDING;
      break;
      
    case SENDING:
      // Send data with encryption to AWS IoT
      encryptAndSendData();
      Serial.println("Sent data. Taking a short break...");
      current_state = WAITING;
      break;
      
    case WAITING:
      // Quick break before next batch
      delay(1000);
      
      // Keep MQTT connection alive
      if (!mqttClient.loop()) {
        if (ensureConnected()) {
          Serial.println("MQTT connection maintained");
        }
      }
      
      // Reset for next batch
      reading_index = 0;
      current_state = COLLECTING;
      Serial.println("Starting next batch collection...");
      break;
  }
}