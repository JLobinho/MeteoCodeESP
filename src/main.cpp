#include <Arduino.h>

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Wire.h>
#include <SensirionI2cSps30.h>

// ------------ CONFIGURAÇÕES ------------
#define WIFI_SSID "FALCAO"
#define WIFI_PASSWORD "{L2o0B1o6}"

#define API_KEY "AIzaSyDiy0ppbDGrp0H7ZVDTbojbUHZpEE7ZuCU"
#define PROJECT_ID "meteo-d18e3" // ex: "weather-station-1234"

#define DHTPIN 4
#define DHTTYPE DHT22

// ------------ OBJETOS ------------
DHT dht(DHTPIN, DHTTYPE);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

SensirionI2cSps30 sps30;

float mc_2p5, mc_10p0;
float mc_1p0, mc_4p0;
float nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0;
float typical_particle_size;
float temp;
float hum;
// ------------ SETUP ------------
void setup()
{
  Serial.begin(115200);
  dht.begin();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("A ligar ao WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" Conectado!");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Firebase Config
  config.api_key = API_KEY;
  // config.project_id = PROJECT_ID;

  // Autenticação anónima
  auth.user.email = "lobinho.gomes.alva@sapo.pt";
  auth.user.password = "lobo00";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // sensor particulas...

  Serial.println("Scanning...");
  Wire.begin(21, 22);

  for (byte address = 1; address < 127; address++)
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0)
    {
      Serial.print("Dispositivo encontrado em 0x");
      Serial.println(address, HEX);
    }
  }

  sps30.begin(Wire, 0x69);
  uint16_t error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (error)
  {
    Serial.println("Erro ao iniciar SPS30");
  }
  else
  {
    Serial.println("SPS30 iniciado");
  }
}

// ------------ LOOP ------------
void loop()
{
  Serial.println("inicio loop...");
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  Serial.print("temperatura:");
  Serial.println(temp);
  Serial.print("humidade:");
  Serial.println(hum);

  // Timestamp atual
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Falha ao obter hora!");
  }
  time(&now);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  Serial.print("Data formatada: ");
  Serial.println(buffer);
  String normalFormatTime = String(buffer);
  String timestamp = String((unsigned long)now);
  Serial.println("creat  documente leitura...");

  uint16_t dataReady = false;
  uint16_t error = sps30.readDataReadyFlag(dataReady);

    if (!error && dataReady)
  {
    // fazer leitura

    uint16_t error = sps30.readMeasurementValuesFloat(
        mc_1p0, mc_2p5, mc_4p0, mc_10p0,
        nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0,
        typical_particle_size);

    if (!error)
    {
      Serial.print("PM1.0: ");
      Serial.println(mc_1p0);
      Serial.print(" PM2.5: ");
      Serial.println(mc_2p5);
      Serial.print(" PM4.0: ");
      Serial.println(mc_4p0);
      Serial.print(" PM10.0: ");
      Serial.println(mc_10p0);
      Serial.print(" NC0.5: ");
      Serial.println(nc_0p5);
      Serial.print(" NC1.0: ");
      Serial.println(nc_1p0);
      Serial.print(" NC2.5: ");
      Serial.println(nc_2p5);
      Serial.print(" NC4.0: ");
      Serial.println(nc_4p0);
      Serial.print(" NC10.0: ");
      Serial.println(nc_10p0);
      Serial.print("Particulas size (typical): ");
      Serial.println(typical_particle_size);
    }
    else
    {
      Serial.println("Erro leitura");
    }

    // Caminho no Firestore: coleção "leituras"
    String documentPath = String("STATION_03/"); //+ timestamp;
    documentPath += timestamp;

    FirebaseJson content;
    if (!isnan(temp) && !isnan(hum))
    {
      content.set("fields/temperatura/doubleValue", temp);
      content.set("fields/humidade/doubleValue", hum);
    }
    else
    {
      Serial.println("Erro a ler sensor!");
    }
    if (!error)
    {
      content.set("fields/PM01.0/doubleValue", mc_1p0);
      content.set("fields/PM02.5/doubleValue", mc_2p5);
      content.set("fields/PM04.0/doubleValue", mc_4p0);
      content.set("fields/PM10.0/doubleValue", mc_10p0);
      content.set("fields/NC00.5/doubleValue", nc_0p5);
      content.set("fields/NC01.0/doubleValue", nc_1p0);
      content.set("fields/NC02.5/doubleValue", nc_2p5);
      content.set("fields/NC04.0/doubleValue", nc_4p0);
      content.set("fields/NC10.0/doubleValue", nc_10p0);
      content.set("fields/TypicalSize/doubleValue", typical_particle_size);
      content.set("fields/Data/stringValue", normalFormatTime);
    }
    else
    {
      Serial.println("Erro leitura");
    }
    content.set("fields/timestamp/integerValue", (unsigned long)now);

    if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", documentPath.c_str(), content.raw()))
    {
      Serial.printf("Dados enviados -> Temp: %.2f °C, Hum: %.2f %%\n", temp, hum);
    }
    else
    {
      Serial.print("Falha no envio: ");
      Serial.println(fbdo.errorReason());
    }
  }

  // sensor particulas

  delay(60000); // envia a cada 1 minuto
}
