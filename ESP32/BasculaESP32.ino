#include <Arduino.h>
//Librería del conversor AC/DC
#include "HX711.h"
//Librería para manejar velocidad del procesador
#include "soc/rtc.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>
//Librería para el formato JSON
#include <ArduinoJson.h>

//Librerías para el modo WebServer
#include <WiFiClient.h>
#include <WebServer.h>
//Librerías necesarias para el SH110X
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

//Librerías para el SD
#include "SD.h"
#include "SPI.h"

//Librerías para el Asyc Web
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

#define SCREEN_WIDTH 128  // OLED display ancho, pixeles
#define SCREEN_HEIGHT 64  // OLED display altura, pixeles
#define OLED_RESET -1     //   necesario para funcionar
#define i2c_Address 0x3c
//Conexión Display
//SCL - 22
//SDA - 21
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int buttonSend = 15;
const int buttonTare = 2;
const int ledOK = 27;
const int ledFail = 25;
int reading;
int lastReading;

//Variables para guardar infoESP32
//Variables para request HTTP POST
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "password";
const char* PARAM_INPUT_3 = "idSensor";
const char* PARAM_INPUT_4 = "enrutamiento";

//Guardar valores del HTML
String ssid;
String password;
String idSensor;
String enrutamiento;

//Rutas del path
const char* ssidPath = "/ssid.txt";
const char* passwordPath = "/password.txt";
const char* idSensorPath = "/idSensor.txt";
const char* enrutamientoPath = "/enrutamiento.txt";

//Timers ESP32
unsigned long previousMillis = 0;
const long interval = 10000;  // Intervalo para conexion ESP32

const int port = 80;
//Inicializamos el puerto
WiFiServer server(port);

AsyncWebServer asyncServer(80);

// Inicializa la librería Express
WebServer app;

//Configuraciones para los pines del esp32 - 38 pines
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;
//Vcc - 3v3
//Gnd - gnd
//E+ - Rojo
//E- - Negro
//A- - Blanco
//A+ - Verde

//Configuracion de los pines para el SD
//MOSI - 23
//CLK  - 18
//MISO - 19
//CS   - 5

HX711 scale;
void setup() {
  Serial.begin(115200);              //Inicializamos la comunicacion Serial
  display.begin(i2c_Address, true);  // Inicializamos el display
  iniciarBascula();                  //Iniciamos la bascula atraves del metodo
  initSD();

  // Cargamos los valores del SD
  ssid = readFile(SD, ssidPath);
  password = readFile(SD, passwordPath);
  idSensor = readFile(SD, idSensorPath);
  enrutamiento = readFile(SD, enrutamientoPath);
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(idSensor);
  Serial.println(enrutamiento);

  if (initWiFi()) {
    //Codigo para cuando tenga conexion
    Serial.println("Conectado a la red WiFi");
    //Mostramos la direccion IP del ESP32
    Serial.println(WiFi.localIP());

  } else {
    // Se imprime que el ESP entrara en modo configuracion
    Serial.println("Modo configuracion AP(Access Point)");
    // Inicializamos el AP
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("Direccion del AP: ");
    Serial.println(IP);
    //Le pasamamos la direccion del AP para imprimirlo en el Display
    displayConf(IP.toString());
    // Web Server cargamos el html de configuracion
    asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(SD, "/wifimanager.html", "text/html");
    });

    asyncServer.serveStatic("/", SD, "/");

    //En este apartado obtnemos los datos del modo configuracion
    asyncServer.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid valor
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Escribimos los datos en la SD
            writeFile(SD, ssidPath, ssid.c_str());
          }
          // HTTP POST pass valor
          if (p->name() == PARAM_INPUT_2) {
            password = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(password);
            // Escribimos los datos en la SD
            writeFile(SD, passwordPath, password.c_str());
          }
          // HTTP POST idSensor valor
          if (p->name() == PARAM_INPUT_3) {
            idSensor = p->value().c_str();
            Serial.print("Id del sensor: ");
            Serial.println(idSensor);
            // Escribimos los datos en la SD
            writeFile(SD, idSensorPath, idSensor.c_str());
          }
          // HTTP POST enrutamiento valor
          if (p->name() == PARAM_INPUT_4) {
            enrutamiento = p->value().c_str();
            Serial.print("Enrutamiento: ");
            Serial.println(enrutamiento);
            // Escribimos los datos en la SD
            writeFile(SD, enrutamientoPath, enrutamiento.c_str());
          }
        }
      }
      request->send(200, "text/plain", "ESP configurada. Reiniciando dispositivo.........");
      delay(3000);
      ESP.restart();
    });
    asyncServer.begin();
  }


  // Inicializa la librería Express
  app.begin();
  // Crea la ruta para obtener la lectura del sensor
  app.on("/peso", HTTP_GET, []() {
    // Verifica si se hizo una petición a la ruta /peso
    if (app.uri() == "/peso") {
      int peso = pesoEnKg();
      // Crea la respuesta JSON
      StaticJsonDocument<200> json;
      json["peso"] = peso;
      // Envía la respuesta JSON
      String jsonResponse;
      serializeJson(json, jsonResponse);
      app.send(200, "application/json", jsonResponse);
    }
  });
}

void loop() {
  while (WiFi.status() == WL_CONNECTED) {
    app.handleClient();
    lectura();
    if (digitalRead(buttonTare) == HIGH) {
      Serial.println("Tarando.......");
      scale.tare();
      delay(500);
      Serial.println("Se coloco la bascula a 0");
    }
  }
}
//Metodo para obtener los valores del peso
int pesoEnKg() {
  delay(300);
  int pesoKg = scale.get_units();
  Serial.println(pesoKg, 1);
  delay(1000);
  scale.power_up();
  return pesoKg;
}

//Metodo para inicar la bascula
void iniciarBascula() {
  //Ajustamos la frecuencia del CPU para poder manipular la bascula
  //rtc_clk_cpu_freq_set(RTC_CPU_FREQ_80M);
  //Inicializamos la bascula
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(374.684);
  //scale.set_scale(380.36);
  scale.tare();
}

//Metodo para imprimir en el Display SH110X
void displayWeight(int weight) {
  //Aqui este apartado deja fijo lo que se va imprimir
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 10);
  // Display static text
  display.println("Peso:");
  display.print(" ");
  display.display();
  display.setCursor(0, 30);
  display.setTextSize(2);
  display.print(weight);
  display.print(" ");
  display.print("g");
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print(WiFi.localIP());
  display.display();
}
//Metodo de lectura para poder imprimir en caso de que exista un cambio
void lectura() {
  if (scale.wait_ready_timeout(200)) {
    reading = round(scale.get_units());
    Serial.print("Peso: ");
    Serial.println(reading);
    if (reading != lastReading) {
      //Llamamos el metodo para imprimir el valor actual
      displayWeight(reading);
    }
    lastReading = reading;
  } else {
    Serial.println("HX711 not found.");
  }
}

// Inicializamos la SD
void initSD() {
  if (!SD.begin()) {
    Serial.println("SD Card no instalada");
    return;
  }
}

// Lectura de la SD
String readFile(fs::FS& fs, const char* path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Escritura de la SD
void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Inicializamos el Wifi
bool initWiFi() {
  listDir(SD, "/", 0);

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Conectando al Wifi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Error al conectar.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}
//Listamos los archivos en la SD
void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}
//Metodo para imprimir los daros del AP
void displayConf(String IP) {
  //Aqui este apartado deja fijo lo que se va imprimir
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 10);
  // Display static text
  display.println("Modo configuracion:");
  display.print(" ");
  display.display();
  display.setCursor(0, 30);
  display.setTextSize(1);
  display.print("AP:");
  display.print(" ");
  display.print("ESP-WIFI-MANAGER");
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("Contraseña:");
  display.setCursor(0, 50);
  display.print(IP);
  display.display();
}