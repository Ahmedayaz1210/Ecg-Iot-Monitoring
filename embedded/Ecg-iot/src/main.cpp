#include <Arduino.h>
#define LED 2

void setup() {
  Serial.begin(115200);  // Initialize serial communication
  pinMode(LED, OUTPUT);
  Serial.println("ESP32 is running! Setup complete.");
}

void loop() {
  digitalWrite(LED, HIGH);
  Serial.println("LED should be ON");  // Send message to computer
  delay(500);
  
  digitalWrite(LED, LOW);
  Serial.println("LED should be OFF"); // Send message to computer
  delay(500);
}