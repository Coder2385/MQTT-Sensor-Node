#include <stdio.h>
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <PubSubClient.h>
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
#include <cfloat>
#include "never.h"

// Hardware settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Declare global OLED and BME280 objects (hardware interfaces)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

// Status flags and timing
bool sensorReady = false;
bool oledReady = false;
unsigned long lastUpdate = 0;

const long UPDATE_INTERVAL = 2000;
const int BUTTON_PIN = 4;
bool buttonPressed = false;
const int LED_BUTTON = 5;

const float TEMP_DREMPEL = 28.00;

const int MODE_INFO = 1;
const int MODE_SENSOR = 0;
const int MODE_STATS = 2;
int currentMode = MODE_SENSOR;

bool buttonState = false; 
bool lastButtonState = false; 
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;

unsigned long startTime = 0;
unsigned long pressDuration = 0;
unsigned long infoModeStartTime = 0;
const unsigned long infoModeDuration = 15000;

bool longPressDetected = false; 
int lastReading = HIGH;

float minTemp = FLT_MAX;
float maxTemp = -FLT_MAX;
int threshold = 0;

// Create a web server object that listens on port 80 (standard HTTP port)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Generates the HTML webpage with sensor values (temperature & humidity)
String createWebPage(float temperature, float humidity) {

  String tempRegel;
  if (temperature >= 28.0) {
    tempRegel = "Temperature: <span id='temperature' style='color:red'>" + String(temperature, 2) + "</span> &deg;C";
  } else {
       tempRegel = "Temperature: <span id='temperature'>" + String(temperature, 2) + "</span> &deg;C";
     }
     
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>ESP32 Sensor</title>";
  html += "<style>body { font-family: monospace; }</style>";
  html += "</head><body>";

  // Display sensor values in a clean layout
  html += "<h2>ESP32 Sensor Data</h2>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += tempRegel;
  html += "</div>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += "Humidity:    <span id='humidity'>" + String(humidity, 2) + "</span> %";
  html += "</div>";
  html += "<h3>Statistics</h3>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += "minTemp:     <span id='minTemp'>" + String(minTemp, 2) + "</span> &deg;C";
  html += "</div>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += "maxTemp:     <span id='maxTemp'>" + String(maxTemp, 2) + "</span> &deg;C";
  html += "</div>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += "threshold:   <span id='threshold'>" + String(threshold) + "</span> times";
  html += "</div>";
  html += "<script>";
  html += "var ws = new WebSocket('ws://' + window.location.hostname + '/ws');";
  html += "ws.onmessage = function(event) {";
  html += "    var data = event.data.split(',');";
  html += "    document.getElementById('temperature').innerHTML = data[0];";
  html += "    document.getElementById('humidity').innerHTML = data[1];";
  html += "    document.getElementById('minTemp').innerHTML = data[2];";
  html += "    document.getElementById('maxTemp').innerHTML = data[3];";
  html += "    document.getElementById('threshold').innerHTML = data[4];";
  html += "};";
  html += "</script>";
  html += "</body></html>";

  return html; // Return the full HTML string to the browser
}

void handleRoot(AsyncWebServerRequest *request) { 
    // Reads the temperature and humidity values
    float temp = bme.readTemperature();
    float hum = bme.readHumidity();

     // Check if sensor returned invalid data (NaN = Not a Number)
     if (isnan(temp) || isnan(hum)) {
       // Show error page and stop
       request->send(200, "text/html", "<html><body>Sensor not available</body></html>");
       return;
     }
     
     String html = createWebPage(temp, hum);
     // Send the HTML webpage to the browser (HTTP 200 = OK)
     request->send(200, "text/html", html);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient * client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

                if (type == WS_EVT_CONNECT) {
                  Serial.println("log: client connected");
                } else if (type == WS_EVT_DISCONNECT) {
                  Serial.println("log: client disconnected");
                } 
             }

void connectMQTT() {
    while (!mqttClient.connected()) {
        Serial.println("Connecting to MQTT...");
        if (mqttClient.connect("ESP32Client")) {
            Serial.println("Connected to MQTT broker");
        } else {
            Serial.println("Failed, retrying in 2 seconds");
            delay(2000);
        }
    }
}

void showSensorData(float temp, float hum) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Show alert on OLED if temperature exceeds threshold
    // (LED is controlled in loop(), not here, to avoid duplicate logic)
    if (temp > TEMP_DREMPEL) {
        display.println("Warm!");
    }
    
    // Display content based on current operating mode
    if (currentMode == MODE_INFO) {
        // Info Mode: show network and system info
        display.print("IP: ");
        display.println(WiFi.softAPIP());    // Print ESP32's softAP IP address

        // Calculate uptime in hours, minutes, and seconds
        unsigned long totalSeconds = millis() / 1000;
        unsigned long hours = totalSeconds / 3600;
        unsigned long minutes = (totalSeconds % 3600) / 60;
        unsigned long seconds = totalSeconds % 60;
        
        // Display uptime in compact format (e.g., "1h 2m 17s" or "2m 17s")
        display.print("Uptime: ");
        if (hours > 0) {
            display.print(hours);
            display.print("h ");
        }
        display.print(minutes);
        display.print("m ");
        display.print(seconds);
        display.print("s");

    } else if (currentMode == MODE_STATS) {
        display.print("minTemp: ");
        display.println(minTemp);

        display.print("maxTemp: ");
        display.println(maxTemp);

        display.print("threshold: ");
        display.println(threshold);
    }
       else {
        // Sensor Mode: show environmental data and button feedback
        if (buttonPressed) {
          display.println("Button pressed!");  // Acknowledgment of button pressed
          buttonPressed = false;               // Reset flag to show message only once
        }
        display.println("Measure Environment");
        // Display sensor readings if valid, otherwise show error
        if (!isnan(temp) && !isnan(hum)) {
            display.println("Temperature: " + String(temp, 2) + " C");
            display.println("Humidity:    " + String(hum, 2) + " %");
        } else {
            display.println("Sensor error");
        }
    }
    // Refresh the OLED screen with updated content
    display.display();
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_BUTTON, OUTPUT);
    

    // 1. Eerst WiFi verbinden
    WiFi.begin(WiFi_SSID, WiFi_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());
    
     // 2. Dan broker instellen
    mqttClient.setServer("broker.hivemq.com", 1883);

    // 3. Dan MQTT verbinden
    connectMQTT();

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, handleRoot); // link the URL (main page) to the handleRoot function
    server.begin(); // Start webServer

    // Initialize BME280 sensor
    if(bme.begin(0x76)) {
        sensorReady = true;
    } else {
        Serial.print("Sensor not found!");
    } 
    
    // Initialize OLED screen
    if(display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
        oledReady = true;
    } else {
        Serial.print("Oled not found!");
    }
}

void loop() {
    
    if (!mqttClient.connected()) {
    connectMQTT();
    }
    mqttClient.loop();
 
    // Checks if it's time for an update
     if(millis() - lastUpdate >= UPDATE_INTERVAL) { 
        if (sensorReady && oledReady) {
         float temp = bme.readTemperature();
         float hum = bme.readHumidity();

         String tempString = String(temp, 2);
         char tempChar[10];
         tempString.toCharArray(tempChar, 10);

         mqttClient.publish("esp32/sensor/temperature",tempChar);

         String humString = String(hum, 2);
         char humChar[10];
         humString.toCharArray(humChar, 10);

         mqttClient.publish("esp32/sensor/humidity", humChar);

         String minTempString = String(minTemp, 2);
         char minTempChar[10];
         minTempString.toCharArray(minTempChar, 10);

         mqttClient.publish("esp32/sensor/minTemp", minTempChar);

         String maxTempString = String(maxTemp, 2);
         char maxTempChar[10];
         maxTempString.toCharArray(maxTempChar, 10);

         mqttClient.publish("esp32/sensor/maxTemp", maxTempChar);

         String thresholdString = String(threshold);
         char thresholdChar[10];
         thresholdString.toCharArray(thresholdChar, 10);

         mqttClient.publish("esp32/sensor/threshold", thresholdChar);
         Serial.println("MQTT published");

         // Build CSV string with sensor data and statistics, then broadcast to all connected WebSocket clients
         String data = String(temp, 2) + "," + String(hum, 2) + "," + String(minTemp, 2) + "," + String(maxTemp, 2) + "," + String(threshold);
         ws.textAll(data);
         
         // Update statistics with latest temperature reading
         if (temp < minTemp) {
           minTemp = temp;   // New lowest temperature recorded
         }

         if (temp > maxTemp) {
           maxTemp = temp;  // New highest temperature recorded
         }

         if (temp > TEMP_DREMPEL) {
           threshold += 1;  // Increment threshold exceeded counter
         }
        
        // Control warning LED based on temperature threshold
        if (temp > TEMP_DREMPEL) {
           digitalWrite(LED_BUTTON, HIGH); // Turn on LED if over the threshold
         } else {
           digitalWrite(LED_BUTTON, LOW);  // Turn off LED if under the threshold
        }
        // Read the current raw state of the button (HIGH = released, LOW = pressed)
        int reading = digitalRead(BUTTON_PIN);
        // Reset debounce timer whenever the raw button state changes
        if (reading != lastReading) {
            lastDebounceTime = millis();
        }
        // Update previous reading for next comparison
        lastReading = reading;  
        // After stable signal (debounced), update the clean button state
        if (millis() - lastDebounceTime > debounceDelay) {
            buttonState = reading;
        }
        
        // Detect stable transitions (press or release)
        if (buttonState != lastButtonState) {
            if (buttonState == LOW) {
               // Button just pressed → start long-press timer
              startTime = millis();
              longPressDetected = false;  // Allow detection for this press
              buttonPressed = true;       // Flag for "Button pressed!" message on OLED
            }
            if (buttonState == HIGH) {
                longPressDetected = false;
            }
            // Remember current state for next cycle
            lastButtonState = buttonState;
        }
        // Continuously check for long press while button is held
        if (buttonState == LOW) {
          unsigned long pressDuration = millis() - startTime;
          // If held for 2+ seconds and not already processed
          if (pressDuration >= 2000 && !longPressDetected) {
            // Cycle through modes on long press: SENSOR → INFO → STATS → SENSOR
            if (currentMode == MODE_SENSOR) {
              currentMode = MODE_INFO;
            } else if (currentMode == MODE_INFO) {
              currentMode = MODE_STATS;
            } else if (currentMode == MODE_STATS) {
              currentMode = MODE_SENSOR;
            }

            infoModeStartTime = millis(); // Start auto-return timer
            longPressDetected = true;      // Prevent repeated activation
          }   
      }
        
        // Auto-return from Info Mode after 15 seconds
        if (currentMode == MODE_INFO) {
            if (millis() - infoModeStartTime >= infoModeDuration) {
              currentMode = MODE_SENSOR;  // Return to sensor readings
            }
        }

        if (currentMode == MODE_STATS) {
           if (millis() - infoModeStartTime >= infoModeDuration) {
             currentMode = MODE_SENSOR;
           }
        }

          // Checks for incorrect values
          if (!isnan(temp) && !isnan(hum)) {

            // Voeg tijdelijk toe in loop():
            Serial.println(digitalRead(BUTTON_PIN));
            // Print sensor readings to Serial Monitor for debugging
            Serial.println("Temperature: " + String(temp, 2) + " °C");
            Serial.println("Humidity:    " + String(hum, 2) + " %");
          } else {
            Serial.println("Sensor error");
            sensorReady = false; // sensor fails > turn off
          }

          showSensorData(temp, hum);
        } 

    // remember when we did the last update
    lastUpdate = millis();
  }
}

