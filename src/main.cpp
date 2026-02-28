#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h> 
#include <DHT.h> 
#include <MQUnifiedsensor.h> 

// Pantalla OLED 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// BMP280 y DHT22 
Adafruit_BMP280 bmp; 
#define DHTPIN 4        
#define DHTTYPE DHT22   
DHT dht(DHTPIN, DHTTYPE);

// Pines 
#define PIN_PM 34
#define PIN_GAS 35
#define LED_ROJO 25
#define LED_VERDE 26
#define LED_AZUL 27

// Configuración MQUnifiedsensor 
#define PLACA "ESP-32"
#define VOLTAJE_RESOLUCION 3.3 
#define ADC_BIT_RESOLUCION 12  
#define TIPO_MQ "MQ-135"
#define RATIO_AIRE_LIMPIO 3.6  // Dato del datasheet del MQ135 para aire limpio

// Instanciamos el objeto del sensor
MQUnifiedsensor MQ135(PLACA, VOLTAJE_RESOLUCION, ADC_BIT_RESOLUCION, PIN_GAS, TIPO_MQ);

void setup() {

  delay(5000);
  Serial.begin(115200);
  
  pinMode(PIN_PM, INPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  
  Wire.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error: OLED no detectada"));
    for(;;); 
  }

  bmp.begin(0x76);
  dht.begin();

  // Inicialización y calibración del sensor MQ135
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(F("Calibrando MQ..."));
  display.println(F("NO ACERCAR GASES"));
  display.display();

  MQ135.setRegressionMethod(1); // Usar modelo matemático: PPM = a*ratio^b
  
  // Constantes A y B para medir el gas que se requiera, en este caso Alchol (El mejor indicador de aire viciado)
  MQ135.setA(77.255); 
  MQ135.setB(-3.18); 
  MQ135.init(); 

  // La librería toma 10 lecturas rápidas del aire actual para fijar su R0
  float calcR0 = 0;
  for(int i = 1; i <= 10; i ++) {
    MQ135.update(); 
    calcR0 += MQ135.calibrate(RATIO_AIRE_LIMPIO);
  }
  MQ135.setR0(calcR0/10); 
  
  Serial.print("Calibracion MQ Terminada. R0 = ");
  Serial.println(calcR0/10);


  delay(1000); 
}

void loop() {
  
  // LECTURA PARA EL SIMULADOR (HW-870)
  int concentracionPM = map(analogRead(PIN_PM), 0, 4095, 150, 0);
  
  // Restringir para que nunca dé valores negativos si el sensor salta
  concentracionPM = constrain(concentracionPM, 0, 150);
  
  // Actualiza y calcula la curva logarítmica sola
  MQ135.update(); 
  float ppmGas = MQ135.readSensor(); 

  // Leer Clima
  float temperatura = bmp.readTemperature() - 4.0; 
  float presion = bmp.readPressure() / 100.0F; 
  float humedad = dht.readHumidity();
  
  // 2. INTERFAZ GRÁFICA OLED
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setCursor(5, 0);
  display.println(F("Aire Sabana Centro"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE); 
  
  // Fila 1: PM y Temperatura
  display.setCursor(0, 14);
  display.print(F("PM:")); display.print(concentracionPM);
  
  display.setCursor(68, 14);
  display.print(F("T:")); display.print(temperatura, 1); display.print(F("C"));

  // Fila 2: Gas y Humedad
  display.setCursor(0, 26);
  display.print(F("Gas:")); display.print(ppmGas, 0); // Sin decimales
  
  display.setCursor(68, 26);
  display.print(F("H:")); display.print(humedad, 1); display.print(F("%"));

  // Fila 3: Presión
  display.setCursor(0, 38);
  display.print(F("Presion: ")); display.print(presion, 0); display.print(F(" hPa"));
  
  // 3. LÓGICA DE ALERTAS
  display.setCursor(0, 50);
  if (ppmGas > 1500 || concentracionPM >= 50) {
    display.println(F("Estado: PELIGROSO"));
    digitalWrite(LED_ROJO, HIGH); digitalWrite(LED_VERDE, LOW);
  } else if (ppmGas > 800 || concentracionPM >= 20) {
    display.println(F("Estado: REGULAR"));
    digitalWrite(LED_ROJO, HIGH); digitalWrite(LED_VERDE, HIGH); 
  } else {
    display.println(F("Estado: BUENO"));
    digitalWrite(LED_ROJO, LOW); digitalWrite(LED_VERDE, HIGH);
  }

  display.display();
  delay(2000); 
}