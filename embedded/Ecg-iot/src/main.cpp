#include <Arduino.h>
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/aes.h"
#include <Preferences.h>

// Using pin 36 for ECG sensor
#define ECG_PIN 36  // ESP32 ADC pin

#define RO_MODE true
#define RW_MODE false

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
  
  // For now, just printing normalized data
  Serial.println("ECG Data Batch:");
  Serial.print("[");
  for (int i = 0; i < 187; i++) {
    Serial.print(ecg_normalized[i], 4);
    if (i < 186) {
      Serial.print(", ");
    }
  }
  Serial.println("]");
  
  // TODO: Connect to AWS IoT Core and send this data
  // Need to set up AWS credentials and MQTT client
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

  // This copies the float array to the byte array. The size of the byte array is equal to the size of the float array in bytes.
  // The memcpy function is used to copy the data from the float array to the byte array.
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
  Serial.println("Encrypted data:");
  for (size_t i = 0; i < padded_len; i++) {
    Serial.print(encrypted_output[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

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

// Function to store AES Encryption key in non-volatile storage so it can be used later and remain persistent across reboots
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
      // Send data (with encryption eventually)
      encryptAndSendData();
      Serial.println("Sent data. Taking a short break...");
      current_state = WAITING;
      break;
      
    case WAITING:
      // Quick break before next batch
      delay(1000);
      
      // Reset for next batch
      reading_index = 0;
      current_state = COLLECTING;
      Serial.println("Starting next batch collection...");
      break;
  }
}


/*

void decrypt_data(unsigned char *encryption_IV_data, size_t data_length, unsigned char *decrypted_output) {
  // Starting with retrieving the IV from the first 16 bytes of the data
  unsigned char extracted_iv[16];
  memcpy(extracted_iv, encryption_IV_data, 16); // Copy the first 16 bytes to the extracted IV

  // Initialize the AES context
  mbedtls_aes_init(&aes);

  // Set the AES decryption key
  mbedtls_aes_setkey_dec(&aes, key, 256);

  // Decrypt the data using AES-CBC
  if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, data_length - 16, extracted_iv, encryption_IV_data + 16, decrypted_output) != 0) {
    Serial.println("Decryption failed!");
    return;
  }

  
  Serial.println("Decrypted data:");
  for (size_t i = 0; i < data_length - 16; i++) {
    Serial.print(decrypted_output[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Remove PKCS7 padding
  size_t encrypted_data_length = data_length - 16; // 768 - 16 = 752
  // The last byte of the decrypted output contains the padding value
  size_t padding_value = decrypted_output[encrypted_data_length - 1]; // decrypted_output[752 - 1] = decrypted_output[751] 

  // Verify all padded values are correct
  bool valid_padding = true;
  for (int i = 0; i < padding_value; i++) {
    if (decrypted_output[encrypted_data_length - 1 - i] != padding_value) {
      valid_padding = false;
      break;
    }
  }

  if (valid_padding) {
    // Calculate original data length by removing padding
    size_t original_length = encrypted_data_length - padding_value;
    Serial.print("Original data length: ");
    Serial.println(original_length);
    
    // Bytes to float conversion
    float decrypted_float[187]; 
    memcpy(decrypted_float, decrypted_output, original_length); 
    
    Serial.println("Decrypted float data:");
    for (size_t i = 0; i < 187; i++) { 
      Serial.print(decrypted_float[i], 4);
      Serial.print(", ");
    }
    Serial.println();
  }
  else {
    Serial.println("Invalid padding detected!");
  }
*/