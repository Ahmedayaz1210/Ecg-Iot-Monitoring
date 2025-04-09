#include <Arduino.h>

// Using pin 36 for ECG sensor
#define ECG_PIN 36  // ESP32 ADC pin

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
  // TODO: Implement AES encryption for Phase 1
  // Will need to encrypt this data before sending to AWS
  
  // For now, just printing  normalized data
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