#include <Arduino.h>

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Wire.h>
#include <SensirionI2cSps30.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>
#include <Preferences.h>

// ------------ CONFIGURAÇÕES ------------

#define API_KEY "AIzaSyDiy0ppbDGrp0H7ZVDTbojbUHZpEE7ZuCU"
#define PROJECT_ID "meteo-d18e3" // ex: "weather-station-1234"

#define DHTPIN 4
#define DHTTYPE DHT22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define DEBUGDHT 1
#define DEBUGSPS30 1

// ---- BOTÃO (define o pino) ----
#define BTN_PIN 0 // pino do botão (ajusta ao teu)

const char *htmlForm = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Configurar WiFi</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #1a1a2e, #16213e);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .card {
      background: white;
      padding: 40px;
      border-radius: 16px;
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
      width: 100%;
      max-width: 380px;
    }

    .icon {
      text-align: center;
      font-size: 48px;
      margin-bottom: 10px;
    }

    h2 {
      text-align: center;
      color: #1a1a2e;
      margin-bottom: 24px;
      font-size: 22px;
    }

    label {
      display: block;
      margin-bottom: 6px;
      color: #555;
      font-size: 13px;
      font-weight: bold;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    input[type=text], input[type=password] {
      width: 100%;
      padding: 12px 16px;
      margin-bottom: 18px;
      border: 2px solid #e0e0e0;
      border-radius: 8px;
      font-size: 15px;
      transition: border 0.3s;
      outline: none;
    }

    input[type=text]:focus, input[type=password]:focus {
      border-color: #4a90e2;
    }

    input[type=submit] {
      width: 100%;
      padding: 14px;
      background: linear-gradient(135deg, #4a90e2, #357abd);
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      letter-spacing: 1px;
      transition: opacity 0.3s;
    }

    input[type=submit]:hover {
      opacity: 0.9;
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="icon">📡</div>
    <h2>Configurar WiFi</h2>

    <form action="/save">
      <label>SSID</label>
      <input type="text" name="ssid" placeholder="Nome da rede WiFi">

      <label>Password</label>
      <input type="password" name="pass" placeholder="Password da rede">

      <label>Nome da Estação</label>
      <input type="text" name="station" placeholder="Ex: STATION_01">

      <input type="submit" value="GUARDAR">
    </form>
  </div>
</body>
</html>
)rawliteral";

enum BootStatus
{
  BOOT_PENDING,
  BOOT_OK,
  BOOT_FAIL
};
// ---- ECRÃS ----
enum ScreenMode
{
  SCREEN_BOOT,
  SCREEN_COMPLETE,
  SCREEN_STATUS
};
ScreenMode currentScreen = SCREEN_STATUS; // ecrã inicial após boot

struct BootItem
{
  const char *label;
  BootStatus status;
};
BootItem bootItems[] = {
    {"WiFi", BOOT_PENDING},
    {"DHT22", BOOT_PENDING},
    {"SPS30", BOOT_PENDING},
    {"Firebase", BOOT_PENDING},
};
const int NUM_BOOT_ITEMS = 4;
// ------------ OBJETOS ------------
DHT dht(DHTPIN, DHTTYPE);

WebServer server(80);
Preferences preferences;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

SensirionI2cSps30 sps30;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

float mc_2p5, mc_10p0;
float mc_1p0, mc_4p0;
float nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0;
float typical_particle_size;
float temp;
float hum;

String ssid;
String password;
String station;
bool wifiConnected = false;
bool firebaseConnected = false;
unsigned long previousFirebaseMillis = 0;
unsigned long previousOledMillis = 0;
const unsigned long firebaseInterval = 60000; // 60 segundos
const unsigned long oledInterval = 700;

// ---- VARIÁVEIS DE ESTADO (globais) ----
bool lastDHTok = false;
bool lastSPS30ok = false;
bool lastFirebaseok = false;
unsigned long lastFirebaseTime = 0; // millis() da última escrita ok
unsigned long lastDHTTime = 0;
unsigned long lastSPS30Time = 0;

void handleButton()
{
  static bool lastState = HIGH;
  static unsigned long lastDebounce = 0;

  bool reading = digitalRead(BTN_PIN);

  // Se o estado mudou, reinicia o timer de debounce
  if (reading != lastState)
  {
    lastDebounce = millis();
  }

  // Só actua se passou tempo suficiente (debounce de 50ms)
  if ((millis() - lastDebounce) > 50)
  {
    // Detecta flanco descendente (botão premido)
    static bool confirmed = HIGH;
    if (reading == LOW && confirmed == HIGH)
    {
      // Avança para o próximo ecrã
      currentScreen = (ScreenMode)((currentScreen + 1) % 3);
      // Forçar redesenho imediato
      previousOledMillis = 0;
    }
    confirmed = reading;
  }

  lastState = reading;
}

void handleSave()
{
  ssid = server.arg("ssid");
  password = server.arg("pass");
  station = server.arg("station");
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.putString("station", station);
  preferences.end();

  server.send(200, "text/html", "Guardado. A reiniciar...");
  delay(2000);
  ESP.restart();
}
//
void startAP()
{

  WiFi.softAP("ESP32_Config");

  server.on("/", []()
            { server.send(200, "text/html", htmlForm); });

  server.on("/save", handleSave);

  server.begin();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Modo Config WiFi");
  display.println("Ligue a:");
  display.println("ESP32_Config");
  display.display();
}
/// @brief
/// @return
int getDHT22Data()
{
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  lastDHTok = !isnan(temp) && !isnan(hum);
  lastDHTTime = millis();

#if DEBUGDHT > 0
  Serial.print("temperatura:");
  Serial.println(temp);
  Serial.print("humidade:");
  Serial.println(hum);
#endif
  return 0;
}

int getSPS30Data()
{
  uint16_t dataReady = false;
  uint16_t error = sps30.readDataReadyFlag(dataReady);

  if (!error && dataReady)
  {
    // fazer leitura

    uint16_t error = sps30.readMeasurementValuesFloat(
        mc_1p0, mc_2p5, mc_4p0, mc_10p0,
        nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0,
        typical_particle_size);

#if DEBUGSPS30 > 0
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
#endif
  }
  lastSPS30ok = !error;
  lastSPS30Time = millis();
  return error;
}
/// @brief
/// @param _errodht
/// @param _erroSPS30
/// @return
int writeFirebase(int _errodht, int _erroSPS30)
{
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

  // Caminho no Firestore: coleção "leituras"
  // String documentPath = station ; //  String("STATION_03/"); //+ timestamp;
  // documentPath+="/";
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
  if (!_erroSPS30)
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
    Serial.println("Erro leitura sensor sps30");
  }
  content.set("fields/timestamp/integerValue", (unsigned long)now);

  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", documentPath.c_str(), content.raw()))
  {
    Serial.printf("Dados enviados -> Temp: %.2f °C, Hum: %.2f %%\n", temp, hum);
    lastFirebaseok = true;
    lastFirebaseTime = millis();
  }
  else
  {
    lastFirebaseok = false;

    Serial.print("Falha no envio: ");
    Serial.println(fbdo.errorReason());
    return 1;
  }
  return 0;
}

void drawBootScreen()
{
  display.clearDisplay();

  // --- Título ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 0);
  display.print("METEO STATION");

  // Linha separadora
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // --- Lista de dispositivos ---
  // Cada linha ocupa ~12px de altura, começa em y=14
  for (int i = 0; i < NUM_BOOT_ITEMS; i++)
  {
    int y = 14 + i * 12;

    // Ícone de status (3 chars à direita)
    const char *icon;
    switch (bootItems[i].status)
    {
    case BOOT_OK:
      icon = "[OK]";
      break;
    case BOOT_FAIL:
      icon = "[!!]";
      break;
    default:
      icon = "[..]";
      break;
    }

    // Label à esquerda
    display.setCursor(4, y);
    display.print(bootItems[i].label);

    // Status à direita (posição fixa x=90)
    display.setCursor(90, y);
    display.print(icon);
  }

  // --- Linha inferior com versão ---
  display.drawLine(0, 53, 127, 53, SSD1306_WHITE);
  display.setCursor(28, 56);
  display.print("v1.0  booting...");

  display.display();
}
void setBootStatus(int index, BootStatus status)
{
  bootItems[index].status = status;
  drawBootScreen();
  delay(300); // pequena pausa para o utilizador ver a mudança
}

void drawBootComplete()
{
  display.clearDisplay();

  // Título
  display.setTextSize(1);
  display.setCursor(20, 0);
  display.print("METEO STATION");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (wifiConnected)
  {
    // Mostrar IP
    display.setCursor(4, 14);
    display.print("WiFi OK");
    display.setCursor(4, 26);
    display.print(WiFi.localIP());

    // Mostrar nome da estação
    display.setCursor(4, 38);
    display.print("Station:");
    display.setCursor(4, 48);
    display.print(station);
  }
  else
  {
    display.setCursor(4, 14);
    display.print("Modo Config AP");
    display.setCursor(4, 26);
    display.print("SSID:");
    display.setCursor(4, 36);
    display.print("ESP32_Config");
    display.setCursor(4, 48);
    display.print(WiFi.softAPIP());
  }

  display.drawLine(0, 53, 127, 53, SSD1306_WHITE);
  display.setCursor(28, 56);
  display.print("Sistema OK");

  display.display();
  delay(2000); // mostrar 2 segundos antes de passar ao loop
}

// ---- FUNÇÃO DE STATUS ----
void drawStatusScreen()
{
  display.clearDisplay();

  // --- Título com nome da estação ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  // Centrar o nome da estação
  int16_t x, y;
  uint16_t w, h;
  display.getTextBounds(station.c_str(), 0, 0, &x, &y, &w, &h);
  display.setCursor((128 - w) / 2, 0);
  display.print(station);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // --- WiFi ---
  display.setCursor(0, 12);
  display.print("WiFi");
  display.setCursor(40, 12);
  if (wifiConnected && WiFi.status() == WL_CONNECTED)
  {
    display.print("OK ");
    // Mostrar os últimos 13 chars do IP para caber
    String ip = WiFi.localIP().toString();
    display.print(ip.substring(ip.lastIndexOf('.') - 3));
  }
  else
  {
    display.print("FALHA");
  }

  // --- Firebase ---
  display.setCursor(0, 22);
  display.print("Firebase");
  display.setCursor(55, 22);
  if (!firebaseConnected)
  {
    display.print("DESLIG.");
  }
  else if (lastFirebaseok)
  {
    // Mostrar há quantos minutos foi a última escrita
    unsigned long mins = (millis() - lastFirebaseTime) / 60000;
    display.print("OK ");
    display.print(mins);
    display.print("m");
  }
  else
  {
    display.print("ERRO");
  }

  // --- DHT22 ---
  display.setCursor(0, 32);
  display.print("DHT22");
  display.setCursor(40, 32);
  if (lastDHTok)
  {
    display.print(temp, 1);
    display.print("C ");
    display.print(hum, 0);
    display.print("%");
  }
  else
  {
    display.print("ERRO");
  }

  // --- SPS30 ---
  display.setCursor(0, 42);
  display.print("SPS30");
  display.setCursor(40, 42);
  if (lastSPS30ok)
  {
    display.print("PM2.5:");
    display.print(mc_2p5, 1);
  }
  else
  {
    display.print("ERRO");
  }

  // --- Linha inferior com hora da última leitura ---
  display.drawLine(0, 53, 127, 53, SSD1306_WHITE);
  display.setCursor(0, 56);
  // Mostrar countdown para próxima leitura
  unsigned long secsToNext = (firebaseInterval - (millis() - previousFirebaseMillis)) / 1000;
  display.print("Prox: ");
  display.print(secsToNext);
  display.print("s");

  // Mostrar hora atual se disponível
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
    display.setCursor(90, 56);
    display.print(buf);
  }

  display.display();
}

int setupFirebase()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Firebase Config
  config.api_key = API_KEY;
  // config.project_id = PROJECT_ID;

  // Autenticação anónima
  auth.user.email = "lobinho.gomes.alva@sapo.pt";
  auth.user.password = "lobo00";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseConnected = true;
  Serial.println("Firebase connected...");
  setBootStatus(3, BOOT_OK);
  return 0;
}
// ------------ SETUP ------------
void setup()
{

  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin(21, 22);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  drawBootScreen();

  dht.begin();
  float t = dht.readTemperature();
  setBootStatus(1, isnan(t) ? BOOT_FAIL : BOOT_OK);

  // sensor particulas...

  // Serial.println("Scanning...");

  // for (byte address = 1; address < 127; address++)
  // {
  //   Wire.beginTransmission(address);
  //   if (Wire.endTransmission() == 0)
  //   {
  //     Serial.print("Dispositivo encontrado em 0x");
  //     Serial.println(address, HEX);
  //   }
  // }

  sps30.begin(Wire, 0x69);
  uint16_t error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  setBootStatus(2, error ? BOOT_FAIL : BOOT_OK);
  if (error)
  {
    Serial.println("Erro ao iniciar SPS30");
  }
  else
  {
    Serial.println("SPS30 iniciado");
  }

  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  station = preferences.getString("station", "");
  preferences.end();

  if (ssid != "")
  {
    WiFi.begin(ssid.c_str(), password.c_str());

    if (WiFi.waitForConnectResult() == WL_CONNECTED)
    {
      wifiConnected = true;
      Serial.println("WiFi ligado");
      setBootStatus(0, BOOT_OK); // WiFi OK
    }
    else
      setBootStatus(0, BOOT_FAIL); // WiFi falhou
  }

  if (!wifiConnected)
  {
    startAP();
  }
  else
  {
    // Firebase
    setupFirebase();

    // Quando tudo terminar, mostrar resumo final
    delay(1000);
    drawBootComplete(); // <-- nova função abaixo

    // display.setCursor(0, 0);
    // display.println("WiFi Ligado");
    // display.println(WiFi.localIP());
    // display.display();

    // Aqui inicializa Firebase
    // initFirebase();
  }
  // display.println("Sistema OK");
  // display.display();
  Serial.println("System iniciate OK");
}

void updateOLED()
{
  switch (currentScreen)
  {
  case SCREEN_BOOT:
    drawBootScreen();
    break;
  case SCREEN_COMPLETE:
    drawBootComplete();
    break;
  case SCREEN_STATUS:
    drawStatusScreen();
    break;
  }
}
// ------------ LOOP ------------
void loop()
{
  //  Serial.println("inicio loop...");

  if (!wifiConnected)
  {
    server.handleClient();
    return;
  }

  handleButton(); // <-- acrescentar aqui

  unsigned long now_loop = millis();

  if (now_loop - previousOledMillis >= oledInterval)
  {
    previousOledMillis = now_loop;
    updateOLED();
  }

  if (now_loop - previousFirebaseMillis >= firebaseInterval)
  {
    previousFirebaseMillis = now_loop;

    int errodht = getDHT22Data();
    int erroSPS30 = getSPS30Data();
    writeFirebase(errodht, erroSPS30);
  }
}
