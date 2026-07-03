/*
 * ============================================================
 *  Water Quality Adulteration Detection System
 *  ESP32 + TDS Sensor + Turbidity Sensor + I2C LCD
 *  Wi-Fi → Google Sheets (data logging) + Blynk (dashboard)
 *  Logistic Regression inference (on-device TinyML)
 * ============================================================
 *
 *  Hardware Connections (from circuit diagram):
 *    TDS  Sensor  AOUT  → GPIO34  (ADC1_CH6)
 *    Turbidity AOUT     → GPIO35  (ADC1_CH7)
 *    LCD  SDA           → GPIO21
 *    LCD  SCL           → GPIO22
 *    All VCC            → 5 V
 *    All GND            → GND
 *
 *  Dependencies (ESP-IDF components / Arduino libraries):
 *    - Wire.h          (I2C)
 *    - LiquidCrystal_I2C.h
 *    - WiFi.h
 *    - HTTPClient.h
 *    - math.h          (expf for sigmoid)
 *
 *  Compile with Arduino IDE 2.x or PlatformIO targeting ESP32.
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

/* ──────────────────────────────────────────────────────────
   1. USER CONFIGURATION
   ────────────────────────────────────────────────────────── */

/* Wi-Fi credentials */
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

/* Blynk */
#define BLYNK_TEMPLATE_ID   "TMPL_XXXXXXXX"
#define BLYNK_TEMPLATE_NAME "WaterQuality"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"

/* Google Sheets Web-App URL (from Apps Script deployment) */
#define GSHEET_URL  "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec"

/* ADC pins */
#define TDS_PIN        34
#define TURBIDITY_PIN  35

/* LCD I2C address (commonly 0x27 or 0x3F) */
#define LCD_ADDRESS  0x27
#define LCD_COLS     16
#define LCD_ROWS      2

/* Sampling & upload intervals (milliseconds) */
#define SAMPLE_INTERVAL_MS   5000UL    /* read sensors every 5 s  */
#define UPLOAD_INTERVAL_MS  30000UL    /* push to cloud every 30 s */

/* ADC reference voltage & resolution */
#define ADC_VREF      3.3f
#define ADC_MAX      4095.0f

/* TDS calibration — adjust K factor for your probe */
#define TDS_K_FACTOR  0.5f
#define TEMP_DEFAULT  25.0f           /* °C, used when no temp sensor */

/* Turbidity calibration (linear map: voltage → NTU) */
#define TURB_V_CLEAR  2.5f            /* voltage at 0 NTU  */
#define TURB_SLOPE   -1120.4f         /* NTU per volt (negative slope) */

/* Safety thresholds (threshold-based fallback) */
#define TDS_SAFE_MAX      500.0f      /* ppm  */
#define TURBIDITY_SAFE_MAX  4.0f      /* NTU  */

/* ──────────────────────────────────────────────────────────
   2. LOGISTIC REGRESSION MODEL COEFFICIENTS
      (Replace with values from your Python training script)

      z = w0*TDS + w1*Turbidity + w2*Temperature + bias
      P = sigmoid(z)
      label = (P > 0.5) ? 1 : 0   (1 = Adulterated)
   ────────────────────────────────────────────────────────── */
static const float LR_W0   =  0.0123f;   /* TDS weight         */
static const float LR_W1   =  0.4512f;   /* Turbidity weight   */
static const float LR_W2   = -0.0214f;   /* Temperature weight */
static const float LR_BIAS = -4.8730f;   /* Intercept          */

/* Feature scaling parameters (from StandardScaler in Python)  */
/* Replace with actual mean/std from your training dataset      */
static const float TDS_MEAN   = 220.0f;  static const float TDS_STD   = 95.0f;
static const float TURB_MEAN  =   3.0f;  static const float TURB_STD  =  2.5f;
static const float TEMP_MEAN  =  25.5f;  static const float TEMP_STD  =  1.2f;

/* ──────────────────────────────────────────────────────────
   3. GLOBAL OBJECTS & STATE
   ────────────────────────────────────────────────────────── */
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

struct SensorData {
    float tds_ppm;
    float turbidity_ntu;
    float temperature_c;
    int   label;          /* 0 = Safe, 1 = Adulterated */
    float probability;    /* ML output */
};

static SensorData g_current;
static unsigned long g_last_sample = 0;
static unsigned long g_last_upload  = 0;

/* ──────────────────────────────────────────────────────────
   4. MATH HELPERS
   ────────────────────────────────────────────────────────── */

/**
 * sigmoid() – logistic activation function
 * @param z  linear combination of features
 * @return   probability in (0, 1)
 */
static float sigmoid(float z)
{
    return 1.0f / (1.0f + expf(-z));
}

/**
 * standardize() – apply Z-score normalisation
 */
static float standardize(float value, float mean, float std_dev)
{
    if (std_dev < 1e-9f) return 0.0f;
    return (value - mean) / std_dev;
}

/* ──────────────────────────────────────────────────────────
   5. SENSOR READING
   ────────────────────────────────────────────────────────── */

/**
 * read_tds_ppm()
 * Reads TDS sensor, compensates for temperature, returns ppm.
 * Formula: TDS = (133.42 * V^3 - 255.86 * V^2 + 857.39 * V) * K
 */
static float read_tds_ppm(float temperature_c)
{
    /* Average 10 samples to reduce ADC noise */
    long raw_sum = 0;
    for (int i = 0; i < 10; i++) {
        raw_sum += analogRead(TDS_PIN);
        delay(10);
    }
    float raw_avg = (float)raw_sum / 10.0f;

    float voltage = raw_avg * ADC_VREF / ADC_MAX;

    /* Temperature compensation coefficient */
    float comp = 1.0f + 0.02f * (temperature_c - 25.0f);
    float comp_voltage = voltage / comp;

    float tds = (133.42f * comp_voltage * comp_voltage * comp_voltage
               - 255.86f * comp_voltage * comp_voltage
               + 857.39f * comp_voltage)
               * TDS_K_FACTOR;

    return (tds < 0.0f) ? 0.0f : tds;
}

/**
 * read_turbidity_ntu()
 * Converts analog turbidity voltage to NTU via linear calibration.
 */
static float read_turbidity_ntu(void)
{
    long raw_sum = 0;
    for (int i = 0; i < 10; i++) {
        raw_sum += analogRead(TURBIDITY_PIN);
        delay(10);
    }
    float raw_avg = (float)raw_sum / 10.0f;
    float voltage = raw_avg * ADC_VREF / ADC_MAX;

    /* Linear model: NTU = slope * (V - V_clear) */
    float ntu = TURB_SLOPE * (voltage - TURB_V_CLEAR);
    if (ntu < 0.0f) ntu = 0.0f;

    return ntu;
}

/* ──────────────────────────────────────────────────────────
   6. LOGISTIC REGRESSION INFERENCE
   ────────────────────────────────────────────────────────── */

/**
 * lr_predict()
 * Applies trained logistic regression model to sensor readings.
 * Updates the label and probability fields of the data struct.
 *
 * @param d  pointer to SensorData (tds, turbidity, temperature must be set)
 */
static void lr_predict(SensorData *d)
{
    /* Standardise features */
    float x0 = standardize(d->tds_ppm,       TDS_MEAN,  TDS_STD);
    float x1 = standardize(d->turbidity_ntu, TURB_MEAN, TURB_STD);
    float x2 = standardize(d->temperature_c, TEMP_MEAN, TEMP_STD);

    /* Linear combination */
    float z = LR_W0 * x0 + LR_W1 * x1 + LR_W2 * x2 + LR_BIAS;

    /* Sigmoid → probability */
    d->probability = sigmoid(z);
    d->label       = (d->probability > 0.5f) ? 1 : 0;
}

/* ──────────────────────────────────────────────────────────
   7. LCD DISPLAY
   ────────────────────────────────────────────────────────── */

/**
 * lcd_show_readings()
 * Displays TDS, turbidity, and classification result on I2C LCD.
 */
static void lcd_show_readings(const SensorData *d)
{
    lcd.clear();

    /* Line 0: TDS and Turbidity */
    lcd.setCursor(0, 0);
    lcd.print("TDS:");
    lcd.print((int)d->tds_ppm);
    lcd.print("ppm T:");
    lcd.print(d->turbidity_ntu, 1);

    /* Line 1: Classification result */
    lcd.setCursor(0, 1);
    if (d->label == 0) {
        lcd.print("Status: SAFE    ");
    } else {
        lcd.print("!! ADULTERATED !!");
    }
}

/* ──────────────────────────────────────────────────────────
   8. BLYNK DASHBOARD PUSH
   ────────────────────────────────────────────────────────── */

/**
 * blynk_push()
 * Sends sensor values and ML result to Blynk virtual pins.
 *   V0 – TDS (ppm)
 *   V1 – Turbidity (NTU)
 *   V2 – Temperature (°C)
 *   V3 – Label (0/1)
 *   V4 – Probability (0.0–1.0)
 */
static void blynk_push(const SensorData *d)
{
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    char url[512];

    /* Blynk HTTP API: update multiple pins */
    snprintf(url, sizeof(url),
        "https://blynk.cloud/external/api/batch/update"
        "?token=%s"
        "&V0=%.1f&V1=%.2f&V2=%.1f&V3=%d&V4=%.3f",
        BLYNK_AUTH_TOKEN,
        d->tds_ppm,
        d->turbidity_ntu,
        d->temperature_c,
        d->label,
        d->probability);

    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Blynk] HTTP error: %d\n", code);
    }

    /* Send Blynk notification if water is adulterated */
    if (d->label == 1) {
        char notif_url[512];
        snprintf(notif_url, sizeof(notif_url),
            "https://blynk.cloud/external/api/notify"
            "?token=%s"
            "&body=ALERT: Water adulteration detected! TDS=%.0fppm, Turbidity=%.1fNTU",
            BLYNK_AUTH_TOKEN,
            d->tds_ppm,
            d->turbidity_ntu);
        HTTPClient http_notif;
        http_notif.begin(notif_url);
        http_notif.GET();
        http_notif.end();
    }

    http.end();
}

/* ──────────────────────────────────────────────────────────
   9. GOOGLE SHEETS LOGGING
   ────────────────────────────────────────────────────────── */

/**
 * gsheet_log()
 * POSTs a JSON payload to the Google Apps Script Web App,
 * which appends a row to the linked Google Sheet.
 *
 * Apps Script endpoint expects:
 *   { tds, turbidity, temperature, label, probability }
 */
static void gsheet_log(const SensorData *d)
{
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(GSHEET_URL);
    http.addHeader("Content-Type", "application/json");

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"tds\":%.1f,\"turbidity\":%.2f,"
        "\"temperature\":%.1f,\"label\":%d,\"probability\":%.4f}",
        d->tds_ppm,
        d->turbidity_ntu,
        d->temperature_c,
        d->label,
        d->probability);

    int code = http.POST(payload);
    if (code != 200) {
        Serial.printf("[GSheet] HTTP error: %d\n", code);
    }
    http.end();
}

/* ──────────────────────────────────────────────────────────
   10. WI-FI CONNECTION
   ────────────────────────────────────────────────────────── */

static void wifi_connect(void)
{
    Serial.printf("\n[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected. IP: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed – continuing offline.");
    }
}

/* ──────────────────────────────────────────────────────────
   11. ARDUINO ENTRY POINTS
   ────────────────────────────────────────────────────────── */

void setup(void)
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Water Quality Monitor – Starting ===");

    /* Initialise I2C LCD */
    Wire.begin(21, 22);   /* SDA=21, SCL=22 */
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Water Quality");
    lcd.setCursor(0, 1);
    lcd.print("Monitor v1.0");
    delay(2000);
    lcd.clear();

    /* Configure ADC */
    analogReadResolution(12);           /* 12-bit: 0–4095  */
    analogSetAttenuation(ADC_11db);     /* 0–3.3 V range   */
    pinMode(TDS_PIN, INPUT);
    pinMode(TURBIDITY_PIN, INPUT);

    /* Connect to Wi-Fi */
    wifi_connect();

    Serial.println("[Setup] Ready.");
}

void loop(void)
{
    unsigned long now = millis();

    /* ── Sensor sampling ── */
    if (now - g_last_sample >= SAMPLE_INTERVAL_MS) {
        g_last_sample = now;

        /* Read sensors */
        g_current.temperature_c = TEMP_DEFAULT;   /* replace with DS18B20 if fitted */
        g_current.tds_ppm       = read_tds_ppm(g_current.temperature_c);
        g_current.turbidity_ntu = read_turbidity_ntu();

        /* Run ML inference */
        lr_predict(&g_current);

        /* Update LCD */
        lcd_show_readings(&g_current);

        /* Serial debug output */
        Serial.printf(
            "[Data] TDS=%.1f ppm | Turb=%.2f NTU | Temp=%.1f C "
            "| P=%.3f | Label=%s\n",
            g_current.tds_ppm,
            g_current.turbidity_ntu,
            g_current.temperature_c,
            g_current.probability,
            (g_current.label == 0) ? "SAFE" : "ADULTERATED");
    }

    /* ── Cloud upload ── */
    if (now - g_last_upload >= UPLOAD_INTERVAL_MS) {
        g_last_upload = now;

        /* Reconnect Wi-Fi if dropped */
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Reconnecting...");
            WiFi.disconnect();
            wifi_connect();
        }

        blynk_push(&g_current);
        gsheet_log(&g_current);

        Serial.println("[Upload] Cloud push complete.");
    }
}

/* ──────────────────────────────────────────────────────────
   APPENDIX: Google Apps Script (paste into Script Editor)
   ──────────────────────────────────────────────────────────

function doPost(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  var data  = JSON.parse(e.postData.contents);
  var ts    = new Date().toLocaleString();
  sheet.appendRow([
    ts,
    data.tds,
    data.turbidity,
    data.temperature,
    data.label,
    data.probability
  ]);
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok" }))
    .setMimeType(ContentService.MimeType.JSON);
}

   ────────────────────────────────────────────────────────── */
