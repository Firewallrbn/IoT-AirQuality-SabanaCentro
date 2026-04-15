#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MQUnifiedsensor.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h> 
#include <DHT.h>

// --- LIBRERÍAS NUEVAS PARA EL SERVIDOR WEB ---
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// =========================================================================
// CREDENCIALES WI-FI LOCAL (Red de la zona/autoridades)
// =========================================================================
const char* ssid = "123";       
const char* password = "12345678"; 

// Instancia del Servidor Web en el puerto 80
AsyncWebServer server(80);

// --- CONFIGURACIÓN PANTALLA OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURACIÓN PMS5003 ---
#define PMS_RX_PIN 16
#define PMS_TX_PIN 17
#define PMS_SERIAL Serial2 

// --- CONFIGURACIÓN MQ-135 ---
#define PIN_GAS 35
#define PLACA "ESP-32"
#define VOLTAJE_RESOLUCION 3.3 
#define ADC_BIT_RESOLUCION 12  
#define TIPO_MQ "MQ-135"
#define RATIO_AIRE_LIMPIO 3.6 
MQUnifiedsensor MQ135(PLACA, VOLTAJE_RESOLUCION, ADC_BIT_RESOLUCION, PIN_GAS, TIPO_MQ);

// --- CONFIGURACIÓN CLIMA ---
Adafruit_BMP280 bmp; 
#define DHTPIN 4      
#define DHTTYPE DHT22 
DHT dht(DHTPIN, DHTTYPE);

// --- CONFIGURACIÓN ACTUADORES ---
#define PIN_ROJO 25
#define PIN_VERDE 26
#define PIN_AZUL 27
#define PIN_BUZZER 23

#define BUZZER_ON LOW
#define BUZZER_OFF HIGH

// =========================================================================
// CONSTANTES FÍSICAS Y TERMODINÁMICAS
// =========================================================================
const float KAPPA_PM = 0.25;       
const float M_FACTOR = 0.85;       
const float LIM_PM_MODERADO = 37.0;
const float LIM_VOC_CRITICO = 800.0; 

// Variables Históricas Inversión Térmica
float temp_historica = 0.0;
float pres_historica = 0.0;
unsigned long ultimo_guardado = 0;
bool alerta_inversion = false;

// =========================================================================
// VARIABLES GLOBALES (Hilos <-> Loop <-> Web)
// =========================================================================
volatile uint16_t pm25_value_crudo = 0; 
volatile float ppmGas = 0.0;
volatile float temp = 0.0;
volatile float presion = 0.0;
volatile float humedad = 0.0;

// Variables procesadas para enviar a la web
float pm25_corregido = 0.0;
String estadoOLED = "BUENA";
bool alarma_silenciada = false; // Control remoto desde la Web

// Búferes Históricos para Gráficos Web (Últimos 30 minutos)
#define MAX_HISTORIAL 30
float hist_pm[MAX_HISTORIAL];
float hist_gas[MAX_HISTORIAL];
int hist_index = 0;
unsigned long tiempo_ultimo_historial = 0;

// Mutex para proteger los cables I2C (OLED y BMP280)
SemaphoreHandle_t mutexI2C;

// =========================================================================
// FUNCIONES AUXILIARES Y LECTURA LÁSER
// =========================================================================
void setLEDColor(int rojo, int verde, int azul) {
  analogWrite(PIN_ROJO, rojo);
  analogWrite(PIN_VERDE, verde);
  analogWrite(PIN_AZUL, azul);
}

void sonarBuzzer(int tiempo) {
  if (!alarma_silenciada) { // Solo suena si las autoridades no lo han silenciado
    digitalWrite(PIN_BUZZER, BUZZER_ON); 
    delay(tiempo); 
    digitalWrite(PIN_BUZZER, BUZZER_OFF);
  } else {
    delay(tiempo); // Mantenemos el ritmo visual aunque no suene
  }
}

bool verifyChecksum(uint8_t* frame, uint16_t length) {
  uint16_t checksum = 0;
  for (int i = 0; i < length - 2; i++) {
    checksum += frame[i];
  }
  uint16_t received_checksum = (frame[length-2] << 8) | frame[length-1];
  return checksum == received_checksum;
}

void leerLaser() {
  static uint8_t buffer[128];
  static int index = 0;
  while (PMS_SERIAL.available()) {
    buffer[index] = PMS_SERIAL.read();
    if (index >= 31) {
      for (int start = 0; start <= index - 31; start++) {
        if (buffer[start] == 0x42 && buffer[start+1] == 0x4D) { 
          uint16_t frame_len = (buffer[start+2] << 8) | buffer[start+3];
          int total_len = frame_len + 4;
          if (index - start + 1 >= total_len) {
            if (verifyChecksum(&buffer[start], total_len)) {
              pm25_value_crudo = (buffer[start+12] << 8) | buffer[start+13];
            }
            int remaining = index - start - total_len + 1;
            for (int i = 0; i <= remaining; i++) {
              buffer[i] = buffer[start + total_len + i];
            }
            index = remaining;
            break;
          }
        }
      }
    }
    index = (index + 1) % 128;
  }
}

// =========================================================================
// HILO DE SENSORES (CORE 0) 
// =========================================================================
void TareaSensores(void *pvParameters) {
  for (;;) {
    leerLaser();
    MQ135.update();
    ppmGas = MQ135.readSensor();
    humedad = dht.readHumidity();

    if (xSemaphoreTake(mutexI2C, portMAX_DELAY) == pdTRUE) {
      temp = bmp.readTemperature();
      presion = bmp.readPressure() / 100.0F;
      xSemaphoreGive(mutexI2C); 
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

// =========================================================================
// SETUP (CORE 1)
// =========================================================================
void setup() {
  delay(3000); 
  Serial.begin(115200);
  Wire.begin(); 
  
  PMS_SERIAL.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AZUL, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_OFF); 
  setLEDColor(0, 0, 255); 
  
  mutexI2C = xSemaphoreCreateMutex();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error OLED"));
    for(;;);
  }
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 10); display.println(F("NODO QUETZAL"));
  display.println(F("Conectando Wi-Fi...")); display.display();

  // --- INICIAR SISTEMA DE ARCHIVOS (LittleFS) ---
  if(!LittleFS.begin(true)){
    Serial.println("Error montando LittleFS (Memoria interna)");
  }

// --- CONEXIÓN WI-FI LOCAL ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int intentos = 0;
  
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println(F("Conectando Wi-Fi..."));
  display.display();

  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500); Serial.print("."); 
    display.print("."); display.display();
    intentos++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Conectado!");
    Serial.print("IP del Tablero: "); Serial.println(WiFi.localIP());
    
    // --- PANTALLA GIGANTE DE IP (10 SEGUNDOS) ---
    display.clearDisplay(); 
    display.setTextSize(1);
    display.setCursor(20, 5);
    display.println("--- WIFI OK ---");
    
    display.setCursor(0, 25);
    display.println("Toma nota de la IP:");
    
    display.setTextSize(2); // Letra grande para que se vea de lejos
    display.setCursor(0, 40);
    display.println(WiFi.localIP()); 
    display.display(); 
    
    delay(10000); // 10,000 milisegundos = 10 Segundos exactos
    display.setTextSize(1); // Devolvemos la letra a tamaño normal
    
  } else {
    display.clearDisplay();
    display.setCursor(0,10);
    display.println("Fallo Wi-Fi."); 
    display.println("Modo Offline activado"); 
    display.display(); 
    delay(4000);
  }

  // --- RUTAS DEL SERVIDOR WEB ASÍNCRONO ---
  // 1. Cargar archivos de la interfaz gráfica
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "application/javascript");
  });

  // 2. API que entrega los datos en vivo y el historial a los gráficos (JSON)
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<1024> doc; // Creamos el documento JSON
    
    // Variables actuales
    doc["pm25"] = pm25_corregido;
    doc["gas"] = ppmGas;
    doc["temp"] = temp;
    doc["hum"] = humedad;
    doc["pres"] = presion;
    doc["estado"] = estadoOLED;
    doc["alarma_mute"] = alarma_silenciada;

    // Arreglos históricos
    JsonArray arr_pm = doc.createNestedArray("historial_pm");
    JsonArray arr_gas = doc.createNestedArray("historial_gas");
    for(int i=0; i<MAX_HISTORIAL; i++){
      arr_pm.add(hist_pm[i]);
      arr_gas.add(hist_gas[i]);
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 3. API para Silenciar/Activar la alarma desde la página web
  server.on("/mute", HTTP_GET, [](AsyncWebServerRequest *request){
    alarma_silenciada = !alarma_silenciada; // Invierte el estado
    String msg = alarma_silenciada ? "Alarma Silenciada" : "Alarma Activada";
    request->send(200, "text/plain", msg);
  });

  server.begin(); // Arrancar el servidor web
  Serial.println("Servidor Web Iniciado");

  // --- INICIAR SENSORES FÍSICOS ---
  dht.begin();
  bmp.begin(0x76);
  MQ135.setRegressionMethod(1); MQ135.setA(110.47); MQ135.setB(-2.862); MQ135.init();

  float calcR0 = 0;
  for(int i = 1; i <= 10; i ++) {
    MQ135.update(); calcR0 += MQ135.calibrate(RATIO_AIRE_LIMPIO);
    sonarBuzzer(30); delay(200);  
  }
  MQ135.setR0(calcR0/10); 
  
  sonarBuzzer(100); delay(100); sonarBuzzer(300);
  setLEDColor(0, 0, 0); 
  
  temp_historica = bmp.readTemperature();
  pres_historica = bmp.readPressure() / 100.0F;
  ultimo_guardado = millis();
  tiempo_ultimo_historial = millis();

  // Rellenar arreglo histórico con ceros inicialmente
  for(int i=0; i<MAX_HISTORIAL; i++) { hist_pm[i] = 0; hist_gas[i] = 0; }
  
  xTaskCreatePinnedToCore(TareaSensores, "Hilo_Sensores", 4096, NULL, 1, NULL, 0);
}

// =========================================================================
// LOOP PRINCIPAL (CORE 1)
// =========================================================================
void loop() {
  // --- 1. COMPENSACIÓN MATEMÁTICA ---
  if (humedad > 65.0) {
    float factor_crecimiento = 1.0 + (KAPPA_PM / ((100.0 / humedad) - 1.0));
    pm25_corregido = (pm25_value_crudo * M_FACTOR) / factor_crecimiento;
  } else {
    pm25_corregido = pm25_value_crudo * M_FACTOR;
  }

  // --- 2. GUARDAR HISTORIAL PARA LA PÁGINA WEB (Ej: Cada 60 Segundos) ---
  if (millis() - tiempo_ultimo_historial > 2000) {
    // Desplazar todos los datos hacia la izquierda (borrar el más viejo)
    for(int i = 0; i < MAX_HISTORIAL - 1; i++) {
      hist_pm[i] = hist_pm[i+1];
      hist_gas[i] = hist_gas[i+1];
    }
    // Guardar el dato nuevo en la última posición
    hist_pm[MAX_HISTORIAL - 1] = pm25_corregido;
    hist_gas[MAX_HISTORIAL - 1] = ppmGas;
    
    tiempo_ultimo_historial = millis();
  }

  // --- 3. DIAGNÓSTICO METEOROLÓGICO (INVERSIÓN TÉRMICA) ---
  if (millis() - ultimo_guardado > 10000) {
    float delta_T = temp - temp_historica;
    float delta_P = presion - pres_historica;
    
    if (delta_T < -1.5 && temp < 12.0 && abs(delta_P) < 1.0) {
      alerta_inversion = true;
    } else {
      alerta_inversion = false;
    }
    temp_historica = temp;
    pres_historica = presion;
    ultimo_guardado = millis();
  }

  // --- 4. MOTOR DE REGLAS LÓGICAS ---
  estadoOLED = "BUENA";
  String logSerial = "Normal.";
  setLEDColor(0, 255, 0); 

  if (pm25_corregido > LIM_PM_MODERADO || ppmGas > LIM_VOC_CRITICO) {
    if (alerta_inversion && humedad > 80.0) {
      estadoOLED = "INV. TERMICA!";
      logSerial = "EMERGENCIA ROJA";
      setLEDColor(255, 0, 0); 
      sonarBuzzer(150); delay(150); sonarBuzzer(150);
    } else {
      estadoOLED = "POLUCION MIXTA";
      logSerial = "ALERTA NARANJA";
      setLEDColor(255, 100, 0); 
      sonarBuzzer(50); delay(100);
    }
  } else if (ppmGas > 1200.0) {
    estadoOLED = "FOCO CERCANO"; logSerial = "PELIGRO LOCAL";
    setLEDColor(255, 0, 0); sonarBuzzer(300);
  } else if (pm25_corregido > LIM_PM_MODERADO) {
    estadoOLED = "POLVO REGIONAL"; logSerial = "ALERTA AMARILLA";
    setLEDColor(255, 255, 0); 
  } else if (pm25_value_crudo > 50 && pm25_corregido < 25) {
    estadoOLED = "NIEBLA PURA"; logSerial = "INFO: Neblina";
    setLEDColor(0, 255, 255); 
  }

  // --- 5. PANTALLA OLED ---
  if (xSemaphoreTake(mutexI2C, portMAX_DELAY) == pdTRUE) {
    display.clearDisplay(); display.setTextSize(1);
    display.setCursor(20, 0); display.println(F("NODO QUETZAL"));
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    display.setCursor(0, 15);
    display.print(F("PM: ")); display.print(pm25_corregido, 1);
    display.setCursor(68, 15);
    display.print(F("T: ")); display.print(temp, 1); display.print(F("C"));

    display.setCursor(0, 28);
    display.print(F("Gas: ")); display.print(ppmGas, 0);
    display.setCursor(68, 28);
    display.print(F("H: ")); display.print(humedad, 1); display.print(F("%"));

    display.setCursor(0, 41);
    display.print(F("Presion: ")); display.print(presion, 0); display.print(F(" hPa"));

    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    display.setCursor(0, 55);
    display.print(F("ST: ")); display.print(estadoOLED);
    
    // Indicador pequeñito si la alarma fue muteada desde la web
    if(alarma_silenciada) { display.setCursor(110, 55); display.print("[M]"); }

    display.display();
    xSemaphoreGive(mutexI2C); 
  }

  Serial.println(WiFi.localIP()); // Imprime la IP constantemente para que no la pierdas
  delay(1000); 
}