# 💧 Water Adulteration Detection and Alert System

An IoT-based smart water quality monitoring system developed using **ESP32**, **TDS Sensor**, **Turbidity Sensor**, and **Logistic Regression** for intelligent water adulteration detection. The system continuously monitors water quality, displays live readings on an LCD, uploads sensor data to the cloud using **Blynk** and **Google Sheets**, and performs on-device machine learning inference to classify water samples as **Safe** or **Adulterated**.

---

## 📌 Features

* Real-time TDS measurement
* Real-time Turbidity measurement
* ESP32-based embedded firmware
* 16×2 I2C LCD live display
* Wi-Fi enabled cloud connectivity
* Google Sheets data logging
* Blynk IoT dashboard integration
* Automatic alert notifications
* On-device Logistic Regression inference
* Historical data collection for model retraining

---

## 🛠 Hardware Components

* ESP32 WROOM Development Board
* TDS Sensor
* Turbidity Sensor
* 16×2 I2C LCD Display
* Wi-Fi Network
* 5V Power Supply

---

## 💻 Software & Technologies

* Embedded C
* Arduino IDE
* ESP32 Framework
* Blynk IoT
* Google Apps Script
* Google Sheets
* Python
* Logistic Regression

---

## ⚙ System Architecture

```text
               Water Sample
                    │
      ┌─────────────┴─────────────┐
      │                           │
      ▼                           ▼
 TDS Sensor                Turbidity Sensor
      │                           │
      └─────────────┬─────────────┘
                    ▼
                 ESP32 MCU
                    │
      ┌─────────────┼─────────────┐
      │             │             │
      ▼             ▼             ▼
  I2C LCD      Blynk Cloud   Google Sheets
                    │
                    ▼
        Logistic Regression Model
                    │
                    ▼
         Safe / Adulterated Alert
```

---

## 🚀 Working Principle

1. The TDS and Turbidity sensors continuously monitor water quality.
2. ESP32 reads both analog sensors using its ADC channels.
3. Raw readings are converted into TDS (ppm) and Turbidity (NTU).
4. Sensor values are displayed on the I2C LCD.
5. Every 30 seconds, readings are uploaded to:

   * Blynk IoT Dashboard
   * Google Sheets
6. Historical sensor data collected in Google Sheets is used to train a Logistic Regression model.
7. The trained model predicts whether the water sample is **Safe** or **Adulterated**.
8. If adulteration is detected, an alert notification is sent through Blynk.

---

## 📊 Machine Learning Workflow

```text
Google Sheets Dataset
          │
          ▼
      CSV Export
          │
          ▼
Feature Scaling
          │
          ▼
Logistic Regression Training
          │
          ▼
Model Evaluation
          │
          ▼
Export Coefficients
          │
          ▼
ESP32 Firmware
          │
          ▼
Real-Time Prediction
```

---

## ☁ Cloud Integration

### Blynk Dashboard

* Live TDS Monitoring
* Live Turbidity Monitoring
* Water Quality Status
* Alert Notifications

### Google Sheets

* Automatic sensor logging
* Historical data storage
* Dataset generation for ML training

---

## 📂 Repository Structure

```text
smart-water-quality-monitoring-system/
│
├── main.ino
├── README.md
├── LICENSE
├── images/
│   ├── block_diagram.png
│   ├── circuit_diagram.png
│   ├── dashboard.png
│   └── prototype.jpg
│
├── dataset/
│   └── sample_data.csv
│
├── docs/
│   └── Water_Quality_Monitor_Report.pdf
│
└── ml/
    ├── logistic_regression.py
    └── model_training.ipynb
```

---

## 📈 Applications

* Drinking Water Monitoring
* Water Treatment Plants
* Smart Homes
* IoT Water Monitoring Systems
* Environmental Monitoring
* Rural Water Safety Projects

---

## 🔮 Future Improvements

* DS18B20 Temperature Compensation
* pH Sensor Integration
* Conductivity Sensor Support
* TinyML Deployment on ESP32
* Automatic Cloud-Based Model Retraining
* Firebase Integration
* Mobile Application Support

---

## 📷 Demonstration

The repository includes:

* Circuit diagram
* System architecture
* Hardware prototype
* Cloud dashboard screenshots
* Sample dataset
* Project report

---

## 📚 Skills Demonstrated

* Embedded Systems Programming
* ESP32 Firmware Development
* Sensor Interfacing
* IoT System Design
* Wi-Fi Communication
* Cloud Integration
* Google Apps Script


---



---

⭐ If you found this project useful, consider giving it a star!
