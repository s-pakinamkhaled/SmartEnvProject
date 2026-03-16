#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ===============================
// Pins
// ===============================

#define DHTPIN 4
#define DHTTYPE DHT22

#define LDR_PIN 34
#define PIR_PIN 27
#define TRIG_PIN 5
#define ECHO_PIN 18

#define LED_PIN 13
#define BUZZER_PIN 12
#define SERVO_PIN 14
#define RELAY_PIN 26

// ===============================
// Thresholds
// ===============================

int TEMP_THRESHOLD = 30;
int LIGHT_THRESHOLD = 500;
int DIST_THRESHOLD = 20;

// ===============================
// WiFi + MQTT
// ===============================

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// ===============================

DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

unsigned long lastMsg = 0;
const long interval = 2000;

unsigned long lastHeartbeat = 0;

// manual override flags
bool manualLED = false;
bool manualServo = false;

unsigned long lastManualCommand = 0; // FIX

// ===============================
// MQTT Callback
// ===============================

void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message arrived: ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(message);

  lastManualCommand = millis(); // FIX

  // LED control
  if (String(topic) == "actuators/led") {

    manualLED = true;

    if (message == "1" || message == "true") {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
  }

  // Buzzer control
  if (String(topic) == "actuators/buzzer") {

    digitalWrite(BUZZER_PIN, HIGH);
    delay(500); // FIX shorter
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Servo control
  if (String(topic) == "actuators/servo") {

    manualServo = true;

    int angle = message.toInt();
    myServo.write(angle);
  }

  // Relay control
  if (String(topic) == "actuators/relay") {

    if (message == "1" || message == "true") {
      digitalWrite(RELAY_PIN, HIGH);
    } else {
      digitalWrite(RELAY_PIN, LOW);
    }
  }

  // Threshold updates
  if (String(topic) == "config/temp") {
    TEMP_THRESHOLD = message.toInt();
  }

  if (String(topic) == "config/light") {
    LIGHT_THRESHOLD = message.toInt();
  }

  if (String(topic) == "config/distance") {
    DIST_THRESHOLD = message.toInt();
  }
}

// ===============================
// WiFi Setup
// ===============================

void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
}

// ===============================
// MQTT reconnect
// ===============================

void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting to MQTT...");

    if (client.connect("ESP32Client123")) {

      Serial.println("connected");

      client.subscribe("actuators/led");
      client.subscribe("actuators/buzzer");
      client.subscribe("actuators/servo");
      client.subscribe("actuators/relay");

      client.subscribe("config/temp");
      client.subscribe("config/light");
      client.subscribe("config/distance");

    } else {

      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying...");

      delay(5000);
    }
  }
}

// ===============================
// Setup
// ===============================

void setup() {

  Serial.begin(115200);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  dht.begin();

  pinMode(PIR_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);

  myServo.attach(SERVO_PIN);
}

// ===============================
// Loop
// ===============================

void loop() {

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  unsigned long now = millis();

  // ===============================
  // reset manual override after 10 sec
  // ===============================

  if (millis() - lastManualCommand > 10000) { // FIX
    manualLED = false;
    manualServo = false;
  }

  // ===============================
  // Heartbeat
  // ===============================

  if (millis() - lastHeartbeat > 10000) {

    lastHeartbeat = millis();

    String status = "{\"uptime\":";
    status += millis() / 1000;
    status += ",\"rssi\":";
    status += WiFi.RSSI();
    status += "}";

    client.publish("system/status", status.c_str());
  }

  // ===============================
  // Sensor reading every 2 sec
  // ===============================

  if (now - lastMsg > interval) {

    lastMsg = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    int light = analogRead(LDR_PIN);
    int motion = digitalRead(PIR_PIN);

    // Ultrasonic sensor
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH);
    float distance = duration * 0.034 / 2;

    // ===============================
    // Automatic LED
    // ===============================

    if (!manualLED) {

      if (temperature > TEMP_THRESHOLD || light < LIGHT_THRESHOLD) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }

    // ===============================
    // Motion buzzer
    // ===============================

    if (motion == HIGH) { // FIX
      digitalWrite(BUZZER_PIN, HIGH);
      delay(500);
      digitalWrite(BUZZER_PIN, LOW);
    }

    // ===============================
    // Automatic Servo
    // ===============================

    if (!manualServo) {

      if (distance < DIST_THRESHOLD) {
        myServo.write(90);
      } else {
        myServo.write(0);
      }
    }

    // ===============================
    // MQTT Publish JSON
    // ===============================

    String tempJson = "{\"value\":" + String(temperature) + ",\"unit\":\"C\"}";
    String humJson = "{\"value\":" + String(humidity) + ",\"unit\":\"%\"}";
    String lightJson = "{\"value\":" + String(light) + "}";

    String motionJson = motion ? "{\"detected\":true}" : "{\"detected\":false}"; // FIX

    String distJson = "{\"value\":" + String(distance) + ",\"unit\":\"cm\"}";

    client.publish("sensors/temperature", tempJson.c_str());
    client.publish("sensors/humidity", humJson.c_str());
    client.publish("sensors/light", lightJson.c_str());
    client.publish("sensors/motion", motionJson.c_str());
    client.publish("sensors/distance", distJson.c_str());

    // ===============================
    // Serial Monitor
    // ===============================

    Serial.print("Temp: ");
    Serial.println(temperature);

    Serial.print("Humidity: ");
    Serial.println(humidity);

    Serial.print("Light: ");
    Serial.println(light);

    Serial.print("Motion: ");
    Serial.println(motion);

    Serial.print("Distance: ");
    Serial.println(distance);

    Serial.println("------------------");
  }
}