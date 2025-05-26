
# 🫀 Real-time ECG Analysis System with Deep Learning

A comprehensive IoT-based cardiac monitoring system that captures ECG signals from patients, processes them through encrypted cloud pipelines, and provides real-time anomaly detection using deep learning to alert medical professionals of potential cardiac abnormalities.

**🌐 Live Dashboard:** [https://ecg-iot-monitoring.vercel.app/](https://ecg-iot-monitoring.vercel.app/)

[![AWS](https://img.shields.io/badge/AWS-Cloud-orange)](https://aws.amazon.com/)
[![TensorFlow](https://img.shields.io/badge/TensorFlow-2.16-blue)](https://tensorflow.org/)
[![ESP32](https://img.shields.io/badge/ESP32-IoT-green)](https://www.espressif.com/)
[![SageMaker](https://img.shields.io/badge/SageMaker-ML-yellow)](https://aws.amazon.com/sagemaker/)

---

## 🏗️ System Architecture

```
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   ESP32 + ECG   │    │              │    │   Lambda 1      │    │   DynamoDB      │
│   AD8232 Sensor │───▶│  AWS IoT     │───▶│ (Data Storage   │───▶│ (Encrypted ECG  │
│ [AES Encryption]│    │   Core       │    │  + Session ID)  │    │    Chunks)      │
└─────────────────┘    │   (MQTT)     │    └─────────────────┘    └─────────────────┘
                       └──────────────┘                                       │
                                                                             │
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐             │
│   Dashboard     │    │              │    │   Lambda 2      │◀────────────┘
│ ECG Waveforms + │◀───│ API Gateway  │◀───│ Decrypt + Filter│
│ ML Diagnosis    │    │   (HTTPS)    │    │ + ML Inference  │
└─────────────────┘    └──────────────┘    └─────────┬───────┘
                                                     │
┌─────────────────┐                         ┌───────▼────────┐    ┌─────────────────┐
│   AWS SNS       │                         │   SageMaker    │    │ AWS Parameter   │
│ Doctor Alerts   │◀────────────────────────│  CNN Model     │    │ Store + KMS     │
│ (If Abnormal)   │                         │  Endpoint      │    │ (Decrypt Keys)  │
└─────────────────┘                         └────────────────┘    └─────────────────┘
```

---

## 📖 The Complete Journey

### 🔌 **Step 1: Signal Acquisition & Processing**
The journey begins with an **ESP32 microcontroller** connected to an **AD8232 ECG sensor module** that captures real-time cardiac electrical activity from the patient. The ESP32 firmware normalizes these raw biomedical signals to ensure consistent amplitude ranges, preparing them for secure transmission.

### 🔐 **Step 2: End-to-End Encryption**
Security is paramount in medical data. The system implements **AES-256 encryption** with:
- **Encryption keys** generated on first boot and stored in ESP32's **Non-Volatile Storage (NVS)**
- **Initialization Vectors (IV)** generated for each data transmission session
- **Session IDs** created by ESP32 to track individual patient monitoring sessions
- The same encryption key is securely stored in **AWS Key Management Service (KMS)** and **AWS Systems Manager Parameter Store** for cloud-side decryption

### 📡 **Step 3: IoT Data Transmission**
Encrypted ECG data is transmitted to **AWS IoT Core** using the **MQTT messaging protocol**. The system sends exactly **187 signal samples per transmission** - this specific number matches the input requirements of our trained machine learning model, where each row contains 187 ECG data points with the 188th column being the label (0=Normal, 1=Abnormal).

### 🗄️ **Step 4: Cloud Data Storage**
**AWS Lambda Function 1** receives IoT messages and processes them:
- Assigns unique session IDs to track patient data
- Stores encrypted ECG chunks in **Amazon DynamoDB** 
- Maintains data integrity and enables scalable retrieval
- Preserves encryption throughout the storage pipeline

### 🧠 **Step 5: Machine Learning Pipeline**

#### **Data Processing & Decryption**
**AWS Lambda Function 2** orchestrates the analysis pipeline:
- Retrieves encryption keys from **AWS KMS** and **Parameter Store**
- Decrypts stored ECG data using AES-256 
- Applies **bandpass filtering (0.5-40Hz)** to remove baseline wandering and high-frequency noise
- Reshapes data to the model's expected input format: `(1, 187, 1)`

#### **Deep Learning Model Architecture**
The system employs a **1D Convolutional Neural Network (CNN)** deployed on **Amazon SageMaker**:

**Model Specifications:**
- **Architecture**: 1D CNN with BatchNormalization and Dropout layers
- **Input Layer**: Accepts 187 ECG data points reshaped to (batch_size, 187, 1)
- **Convolutional Layers**: 
  - Conv1D(32 filters, kernel_size=3) → BatchNorm → ReLU → MaxPool → Dropout(0.2)
  - Conv1D(64 filters, kernel_size=3) → BatchNorm → ReLU → MaxPool → Dropout(0.2)
- **Dense Layers**: Flatten → Dense(32, ReLU) → Dropout(0.3) → Dense(1, Sigmoid)
- **Output**: Binary classification (0=Normal, 1=Abnormal)

**Training Dataset**: [MIT-BIH Arrhythmia Database via Kaggle](https://www.kaggle.com/datasets/shayanfazeli/heartbeat/data)
- **Dataset Size**: 109,446 ECG samples
- **Classes**: Normal vs Abnormal heartbeats
- **Preprocessing**: Bandpass filtering, normalization, and resampling to 187 points

**Model Storage**: Trained model artifacts are stored in **Amazon S3** and deployed as a **SageMaker real-time inference endpoint**

### 📊 **Step 6: Real-time Dashboard & Visualization**
**Lambda Function 2** is exposed through **AWS API Gateway**, enabling the web dashboard to:
- Retrieve decrypted and filtered ECG signals
- Display real-time **ECG waveforms** with proper cardiac rhythm visualization
- Perform **Fast Fourier Transform (FFT)** analysis for frequency domain insights
- Calculate **heart rate and R-peak detection** for vital sign monitoring
- Show **ML diagnosis results** with confidence scores
- Provide **signal quality metrics** for healthcare professionals

### 🚨 **Step 7: Medical Alert System**
When the CNN model detects abnormal cardiac patterns (prediction ≥ 0.5), the system automatically:
- Triggers **AWS Simple Notification Service (SNS)**
- Sends **real-time alerts to medical professionals**
- Includes patient session ID and abnormality confidence score
- Enables rapid medical intervention for potential cardiac events

---

## ✨ Key Features

### 🔒 **Enterprise-Grade Security**
- End-to-end AES-256 encryption from sensor to cloud
- AWS KMS for secure key management
- HTTPS/TLS for all web communications
- Session-based data isolation

### 🤖 **Advanced Machine Learning**
- Custom 1D CNN architecture optimized for ECG signals
- Real-time inference with sub-second latency
- Robust preprocessing pipeline with noise reduction
- High accuracy classification of cardiac abnormalities

### ⚡ **Real-time Processing**
- Live ECG signal streaming and visualization
- Instant ML-based diagnosis
- Automated medical alerting system
- Scalable serverless architecture

### 📱 **Professional Dashboard**
- Interactive ECG waveform plotting
- FFT frequency analysis charts
- Real-time vital sign monitoring
- Responsive design for medical professionals

---

## 🎯 Model Performance

| Metric | Score |
|--------|-------|
| **Accuracy** | 94.96% |
| **Precision** | 96.45% |
| **Recall** | 96.57% |
| **F1 Score** | 96.51% |
| **AUC Score** | 98.76% |

**Model Training Details:**
- **Framework**: TensorFlow 2.16
- **Training Time**: ~45 minutes on AWS SageMaker ml.m5.xlarge
- **Validation Strategy**: 70% train, 15% validation, 15% test split
- **Class Balancing**: Computed class weights to handle dataset imbalance
- **Early Stopping**: Implemented to prevent overfitting

---

## 🛠️ Technology Stack

### **Hardware & IoT**
- **ESP32** microcontroller with WiFi capability
- **AD8232** single-lead ECG sensor module
- **Non-Volatile Storage (NVS)** for secure key storage

### **AWS Cloud Services**
- **AWS IoT Core** - Device management and MQTT messaging
- **AWS Lambda** - Serverless compute for data processing
- **Amazon DynamoDB** - NoSQL database for encrypted ECG storage
- **AWS SageMaker** - Machine learning model deployment and inference
- **Amazon S3** - Model artifact storage
- **AWS KMS** - Encryption key management
- **AWS Systems Manager Parameter Store** - Secure parameter storage
- **AWS API Gateway** - RESTful API for dashboard communication
- **AWS SNS** - Medical alert notification service

### **Machine Learning & Analytics**
- **TensorFlow 2.16** - Deep learning framework
- **1D Convolutional Neural Networks** - ECG pattern recognition
- **NumPy & SciPy** - Signal processing and numerical computation
- **Custom DSP Pipeline** - Bandpass filtering and noise reduction

### **Frontend & Visualization**
- **HTML5/CSS3/JavaScript** - Responsive web dashboard
- **Chart.js** - Real-time ECG visualization
- **FFT Analysis** - Frequency domain signal analysis
- **Vercel** - Dashboard hosting and deployment

---

## 🏥 Medical Applications

This system addresses critical needs in **cardiac monitoring**:
- **Continuous patient monitoring** in clinical settings
- **Remote cardiac health assessment** for telemedicine
- **Early detection of arrhythmias** and cardiac abnormalities
- **Real-time alerts** for medical emergencies
- **Long-term cardiac health tracking** for chronic patients

---

## 🙏 Acknowledgments

- **MIT-BIH Arrhythmia Database** - Providing the gold standard ECG dataset for model training
- **Kaggle Community** - For preprocessing and sharing the heartbeat classification dataset
- **AWS** - For comprehensive cloud infrastructure and machine learning services
- **TensorFlow Team** - For the powerful deep learning framework
- **Espressif Systems** - For the versatile ESP32 IoT platform
- **Analog Devices** - For the AD8232 ECG sensor technology

---

**⭐ This project demonstrates the power of combining IoT, cloud computing, and machine learning to create life-saving medical technology.**
