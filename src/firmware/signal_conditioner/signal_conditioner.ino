/****************************************************************
    Inclusão das bibliotecas, definição de constantes e
                      variáveis globais
****************************************************************/
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include "WiFiManager.h"
#include <ArduinoJson.h>
#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "UbidotsEsp32Mqtt.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// constantes do LCD
#define ADDRESS  0x27
#define COLUMNS 16
#define ROWS 2

//Arquivo onde as configurações serão salvas
#define JSON_CONFIG_FILE "/config.json" 

//Arquivo onde as medições serão salvas
#define MEASURES_FILE "/measure.csv" 

// Definição dos pinos utilizados no projeto.
#define RED_PIN 14       // Pino do LED vermelho.
#define GREEN_PIN 17     // Pino do LED verde.
#define BLUE_PIN 16      // Pino do LED azul.
#define BUTTON_LCD 12 // Pino do botão de ligar o lcd.
#define BUTTON_RESET 2   // Pino do botão de reiniciar.
#define SPEAKER_PIN 15   // Pino do Buzzer.
#define WIFI_INPUT 25    //pino do wifi
#define WIFI_LED_PIN 4    //pino do LED do wifi

String WIFI_SSID = "test";      // Armazena o SSID do Wi-Fi
String WIFI_PASS = "";      // Armazena a senha do Wi-Fi

//Configuraçãoes feitas no site via LAN
char ubidotsToken[50] = "";
char espName[30] = "sensor_celula2";
char measureUnit[5] = "N";
float maxValue = 1;
float tareValue = 0;
float setScale = 204241.493;
int publishFrequency = 1000;

bool sdWorking = true;      // Estado do sd, verdadeiro se estiver funcionando.
bool isScanning = true;    // Estado da varredura, verdadeiro se estiver medindo.
int lastButtonPressed = -1; // Estado do botão, verdadeiro se estiver pressionado.
unsigned long timer;        //timer para calcular a frequencia de publicação

LiquidCrystal_I2C lcd(ADDRESS, COLUMNS, ROWS); // Inicialização do LCD
HX711 scale; // Objeto da balança.
Adafruit_BME280 bme; //Inicializando o BME


/************************************************************
                  Funções do Cartão SD
************************************************************/
//Função para enviar os dados pelo ubidots
bool sendData(float value, float h, float t, char* ssid, char* pass) {
  Ubidots ubidots(ubidotsToken); //Cria o objeto do Ubidots
  const bool connected = ubidots.connectToWifi(ssid, pass, handleScan, 5); //Conecta o Ubidots ao Wifi
  //Se está conectado
  if (connected) {
    ubidots.setup(); //Inicia o funcionamento do ubidots
    bool reconnected = true;

    //Se o ubidots não está conectado
    if (!ubidots.connected()) {
      Serial.println("Reconectando");
      cleanLCDRow(0, "Reconectando");
      reconnected = ubidots.reconnect(handleScan, 2);
    }

    //Se foi reconectado
    if (reconnected) {
      //Adiciona as variáveis
      ubidots.add("temperatura", t);
      ubidots.add("humidade", t);
      ubidots.add(measureUnit, value); //Define o dado do HX711 para ser enviado 

      //Envia os dados adicionados
      ubidots.publish(espName); //Envia no nome do dispositivo

      //Indica isso 
      Serial.println("Dados enviados");
      cleanLCDRow(0, "Dados enviados");
      return true;
    }
  }
  return false;
}


/************************************************************
                  Funções do Cartão SD
************************************************************/
//Função chamada para salvar as configurações
void saveConfigFile() {
  //Se o SD está funcionando
  if (sdWorking) {
    //Põem as configurações em um JSON
    Serial.println(F("Saving config"));
    StaticJsonDocument<512> json;
    json["ubidotsToken"] = ubidotsToken;
    json["espName"] = espName;
    json["measureUnit"] = measureUnit;
    json["publishFrequency"] = publishFrequency;
    json["maxValue"] = maxValue;
    json["tareValue"] = tareValue;
    json["setScale"] = setScale;
    json["WIFI_SSID"] = WIFI_SSID;
    json["WIFI_PASS"] = WIFI_PASS;

    //Abre o arquivo e salva
    File configFile = SD.open(JSON_CONFIG_FILE, FILE_WRITE);
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJsonPretty(json, Serial);
    if (serializeJson(json, configFile) == 0) {
      Serial.println(F("Failed to write to file"));
    }
    configFile.close(); //Fecha o arquivo
  }
}

//Adiciona mais uma linha a um arquivo já existente
void appendFile(const char * message, const char * header){
  Serial.printf("Appending to file: %s\n", MEASURES_FILE);
  
  //Se não existir no sd
  if (!SD.exists(MEASURES_FILE)) {
    //Abre o arquivo e cria um cabeçalho
    File measureFile1 = SD.open(MEASURES_FILE, FILE_WRITE);
    if(measureFile1.println(header)){
      Serial.println("Measure File written");

    } else {
      Serial.println("Measure File Write failed");
    }

    measureFile1.close(); //Fecha o arquivo
  }

  //Abre o arquivo e salva em uma nova linha
  File measureFile = SD.open(MEASURES_FILE, FILE_APPEND);
  if(!measureFile){
    Serial.println("Failed to open file for appending");
    return;
  }
  if(measureFile.print(message)){
      Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  measureFile.close();  //Fecha o arquivo
}


//Carrega as variáveis de configuração
bool loadConfigFile() {
  //Se o sd está funcionando
  if (sdWorking) {
    //Se o arquivo existe
    if (SD.exists(JSON_CONFIG_FILE)) {
      File configFile = SD.open(JSON_CONFIG_FILE);

      //Se configFile existe
      if (configFile) {
        StaticJsonDocument<512> json;
        DeserializationError error = deserializeJson(json, configFile);
        JsonObject obj = json.as<JsonObject>();
        serializeJsonPretty(json, Serial); 
        
        
        //Se não deu erro
        if (!error) {
          Serial.println("\nparsed json");
          
          //Carrega os arquivos nas variáveis globais
          strcpy(ubidotsToken, json["ubidotsToken"]);
          strcpy(espName, json["espName"]);
          strcpy(measureUnit, json["measureUnit"]);
          maxValue = json["maxValue"].as<float>();
          tareValue = json["tareValue"].as<float>();
          setScale = json["setScale"].as<float>();
          publishFrequency = json["publishFrequency"].as<float>();
          WIFI_SSID = obj["WIFI_SSID"].as<String>();
          WIFI_PASS = obj["WIFI_PASS"].as<String>();
          Serial.println(obj["WIFI_PASS"].as<String>());

          return true;

        } else {
          Serial.println("failed to load json config");
        }
      }
    }  
    return false;
  }
  return false;
}


//Escreve as informações das medições no cartão sd
void writeMeasures(float h, float t, float value) {
  //Monta o texto que será escrito no SD
  String headerStr = "milissegundos ligado; umidade %; temperatura (*C); " + String(measureUnit); 
  String messageStr = String(millis()) + "; " + String(h) + "; " + String(t) + "; " + String(value, 6) + "\n";
  char header[80];
  char message[80];
  //Transforma em array de char
  headerStr.toCharArray(header, 80);
  messageStr.toCharArray(message, 80);

  //Adiciona no arquivo
  appendFile(message, header);
}

/***************************************************
     FUNÇÕES REFERENTES A CONFIGURAÇÃO VIA LAN
       
Código baseado no repositório feito por
https://github.com/sponsors/witnessmenow/
****************************************************/
// Irá ser chamada quando o usuário apertar em Salvar na configuração LAN
void saveConfigCallback() {
  Serial.println("Should save config");
  saveConfigFile();
}


//Callback que irá ser chamada quando a página de configuração for aberta.
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered Conf Mode");

  Serial.print("Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());

  Serial.print("Config IP Address: ");
  Serial.println(WiFi.softAPIP());
}


//Função utilizada para pegar os dados enviados na configuração lan
String getCustomParamValue(WiFiManager *myWiFiManager, String name) {
  String value;

  int numArgs = myWiFiManager->server->args();
  for (int i = 0; i < numArgs; i++) {
    Serial.println(myWiFiManager->server->arg(i));
  }
  if (myWiFiManager->server->hasArg(name))
  {
    value = myWiFiManager->server->arg(name);
  }
  return value;
}

//Função para criar e instanciar a configuração via WiFi
void startWifiManager(bool (*callback)()) {
  //Liga o led do wifi
  digitalWrite(WIFI_LED_PIN, HIGH);
 //Instancia a controlador da página web 
  WiFiManager wm;
  //Define os callbacks
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback);

  /************************************************************
       Criação dos parâmetros personalizados do WiFi Manager
  *************************************************************/
  WiFiManagerParameter ubidots_token_input("ubidots_token", "Token do ubidots (max: 50 caracteres)", ubidotsToken, 50); 
  WiFiManagerParameter esp_name_input("esp_name", "Nome do dispositivo (max: 30 caracteres)", espName, 30); 
  WiFiManagerParameter measure_unit_input("measure_unit", "Unidade de medida (max: 5 caracteres)", measureUnit, 5); 
  
  char convertedMaxValue[11];
  dtostrf(maxValue, 3, 3, convertedMaxValue); 
  WiFiManagerParameter max_value_input("max_value", "Valor mínimo para o alerta", convertedMaxValue, 12);
  
  char convertedSetScale[11];
  dtostrf(setScale, 3, 3, convertedSetScale); 
  WiFiManagerParameter set_units_input("units_value", "Valor da calibragem", convertedSetScale, 12);
  
  char convertedPublishTime[11];
  dtostrf(publishFrequency, 3, 3, convertedPublishTime); 
  WiFiManagerParameter publish_frequecy_input("publish_time", "Frequência de envio", convertedPublishTime, 12);

  char *customHtml = "type=\"checkbox\"";
  WiFiManagerParameter do_scan_input("key_bool", "Fazer a tara?", "T", 2, customHtml); 

  wm.addParameter(&ubidots_token_input);
  wm.addParameter(&esp_name_input);
  wm.addParameter(&measure_unit_input);
  wm.addParameter(&max_value_input);
  wm.addParameter(&set_units_input);
  wm.addParameter(&publish_frequecy_input);
  wm.addParameter(&do_scan_input);

  //Inicia a página de configuração web 
  char wifiName[23];
  String macAddress = String("LedZ+" + WiFi.macAddress());
  macAddress.toCharArray(wifiName, 23);
  cleanLCDRow(0, "Abrindo conexao wifi");
  if (wm.startConfigPortal(wifiName, "iptamainteli", callback)) {
    cleanLCDRow(0, "Salvo com sucesso");
    //Atribui os inputs as variáveis globais
    strncpy(ubidotsToken, ubidots_token_input.getValue(), sizeof(ubidotsToken));
    Serial.print("ubidotsToken: ");
    Serial.println(ubidotsToken);

    strncpy(espName, esp_name_input.getValue(), sizeof(espName));
    Serial.print("espName: ");
    Serial.println(espName);

    strncpy(measureUnit, measure_unit_input.getValue(), sizeof(measureUnit));
    Serial.print("measureUnit: ");
    Serial.println(measureUnit);

    maxValue = atoi(max_value_input.getValue());
    Serial.print("maxValue: ");
    Serial.println(maxValue);

    setScale = atoi(set_units_input.getValue());
    Serial.print("setScale: ");
    Serial.println(setScale);

    publishFrequency = atoi(publish_frequecy_input.getValue());
    Serial.print("publishFrequency: ");
    Serial.println(publishFrequency);
    
    int tareScan = (strncmp(do_scan_input.getValue(), "T", 1) == 0);
    WIFI_PASS = wm.getWiFiPass();
    WIFI_SSID = wm.getWiFiSSID();

    //Se foi apertado para fazer tara
    if (tareScan) {
      doTare();
    }

    //Salva as configurações em memória não volátil
    saveConfigFile();
    cleanLCDRow(0, "Config salva");
    digitalWrite(WIFI_LED_PIN, LOW); //Desliga o led do wifi
    return;
  }
  cleanLCDRow(0, "Não foi salvo");
  digitalWrite(WIFI_LED_PIN, LOW); //Desliga o led do wifi
}


/************************************************************
              Funções padrões do arduino IDE
************************************************************/
void setup() {
  //Inicia o cartão sd
  if (!SD.begin(5)) {
    sdWorking = false;
    Serial.println("SD Failed to init");
  }
  delay(50);


  //Carrega os arquivos
  loadConfigFile();

  //Define os pinos
  pinMode(BUTTON_LCD, INPUT);
  pinMode(BUTTON_RESET, INPUT);
  pinMode(WIFI_INPUT, INPUT);

  pinMode(SPEAKER_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(WIFI_LED_PIN, OUTPUT);

  //Define o modo padrão que o WiFi deverá ficar
  WiFi.mode(WIFI_STA);
  
  lcd.init(); // Inicia a comunicação com o display
  lcd.backlight(); // Liga a iluminação do display
  lcd.clear(); // Limpa o display
  lcd.setCursor(0, 0); // Define onde irá começar a escrever
  lcd.print("Iniciou"); // Imprime a mensagem no LCD

  bme.begin(0x76); //Define a porta I2C do bme

  // Inicializa o objeto HX711 chamado scale.
  scale.begin(26, 27); // Pino DT = 26, Pino SCK = 27.
  scale.set_scale(setScale); // Define a escala do HX711.
  scale.tare(tareValue); //Realiza a tara
  
  // Inicializa a comunicação Serial.
  Serial.begin(115200);
  Serial.println("Esp32 iniciou.");

  timer = millis(); //Inicializa o timer com o millis atual
}


void loop() {
  handleWifiAndScan(); //Escuta os inputs, incluindo o do Wifi
  
  const int time = (millis() - timer); // Atribui a distância do tempo atual até o tempo desejado
  //Se o tempo for maior que a frequência de publicação e está escaneando
  const bool timeHasPassed = (abs(time) >(publishFrequency));

  //Se o tempo passou e tem nome de wifi salvo
  if (timeHasPassed && !WIFI_SSID.equals("")) {
    //Converte os valores salvos do wifi de String para Char Array
    char ssid[50];
    char pass[50];
    WIFI_SSID.toCharArray(ssid, 50);
    WIFI_PASS.toCharArray(pass, 50);

    //Limpa o serial
    Serial.flush();

    const float value = measure(); //Lê o HX711
    const float h = changeNaNTo0(bme.readHumidity()); // Lê a humidade
    const float t = changeNaNTo0(bme.readTemperature()); // Lê a temperatura em °C 

    //Envia os valores com o Ubidots
    bool valuesSent = sendData(value, h, t, ssid, pass);
    if (!valuesSent) {
      cleanLCDRow(0, "Falha ao enviar dados");
      writeMeasures(h, t, value);
    }
    timer = millis(); //Redefine o tempo utilizado para a contagem do timer  
  }

  delay(20);
}


/************************************************************
            Funções para lidar com os inputs
************************************************************/
//Escuta os inputs dos botões e do botão de config. via lan
void handleWifiAndScan() {
  handleWifiInput();
  handleScan();
}


//Escuta o botão de config. via lan
void handleWifiInput() {
  if (checkButton(WIFI_INPUT)) {
    Serial.println("Abrindo conexao wifi");
    startWifiManager(checkWifiInput); //Inicia a configuração via lan
  }
}


//Escuta os botões e verifica se o botão de config. via lan foi apertado
bool checkWifiInput() {
  handleScan();
  return checkButton(WIFI_INPUT);
}


//Agrupa todos os inputs de botões para poderem ser incluidos em um while
void checkInputs() {
  if (checkButton(BUTTON_LCD)) {
    isScanning = !isScanning; // Alterna o estado da varredura.
    
    
    if (isScanning) {
      lcd.backlight();
      delay(40);
      cleanLCDRow(0, "Escaneando...");
    } else {
      lcd.noBacklight();
      delay(40);
      cleanLCDRow(0, "");
    }
    
  }
  if (checkButton(BUTTON_RESET)) {
    digitalWrite(SPEAKER_PIN, LOW); // Desativa o buzzer.
    ESP.restart(); // Reseta o esp
  }
}


//Serve para evitar toque fantasma e erro ao pressionar algum botão
bool checkButton(int pin) {
  int pressed = digitalRead(pin);
  // Se o botão for pressionado e o estado anterior for diferente do atual.
  if (pressed == HIGH && lastButtonPressed != pin) {
    lastButtonPressed = pin;
    return true;
  } else if (pressed == LOW && lastButtonPressed == pin) {
    lastButtonPressed = -1;
  }

  return false;
}


/************************************************************
          Funções para lidar com a medição
************************************************************/
//Escuta os botões que não são o de wifi e faz o funcionamento do SCAN
void handleScan(){
  // Se o botão for pressionado e o estado anterior for diferente do atual.
  checkInputs();
  
  // Se o botão foi pressionado, começa a medir.
  if (isScanning) {    
    scale.power_up();
    measureAndAlarm();

    // Senão, desliga o LED.
  } else {
    digitalWrite(SPEAKER_PIN, LOW); // Desativa o buzzer.
    colorRGB(0, 0, 0);
    scale.power_down();
  }
}


// Mede o peso e ativa o alarme alterando a cor do LED.
void measureAndAlarm() {
  float weight = measure(); // Mede o peso.

  // Se o peso for igual ou maior que 0.5.
  if (weight >= maxValue) {
    digitalWrite(SPEAKER_PIN, HIGH); // Ativa o buzzer.
    colorRGB(255, 0, 0); // Acende o LED vermelho.

  } else if (weight >= 0) { // Se o peso estiver entre 0.1 e 0.5
    digitalWrite(SPEAKER_PIN, LOW); // Desativa o buzzer.
    colorRGB(0, 255, 0); // Acende o LED verde.

  } else {
    colorRGB(0, 0, 255); // Acende o LED azul.
    digitalWrite(SPEAKER_PIN, LOW); // Desativa o buzzer.
  }
  delay(100);
}


// Usa o HX711 para medir o peso.
float measure() {
  if(scale.is_ready()){  // Se o HX711 está pronto para medir
    float weight = scale.get_units(); // Obtém a medida da balança.
    cleanLCDRow(1, String(weight, 5) + " kg");
    return weight; // Retorna o valor medido.
  }
  return 0.0;
}


//Realiza a tara no HX711
void doTare() {
  //Se o HX711 não está pronto para medir
  if (!scale.is_ready()) {
    scale.power_up(); //Liga o HX711
    delay(50);
  }

  float units = scale.get_units();  // Pega a medição da balança
  tareValue = units;                // Salva a medição
  scale.tare(units);                // Realiza a tara com base na medição
  
  //Se o HX711 não está pronto para medir
  if (!scale.is_ready()) {
    scale.power_down(); //Liga o HX711
    delay(50);
  }
  cleanLCDRow(0, "Tara realizada");
}


//Altera os valores NAN para 0 
//Isso é feito para poder enviar os valores para o Ubidots
float changeNaNTo0(float number) {
  //Se não é um número
  if (isnan(number)) {
    return 0; //Retorna 0
  }
  return number; //Retorna o número
}

/************************************************************
      Funções para lidar com dispositivos de saída
************************************************************/
// Função para mudar a cor do LED RGB.
void colorRGB(int red, int green, int blue) {
  analogWrite(RED_PIN, red);
  analogWrite(GREEN_PIN, green);
  analogWrite(BLUE_PIN, blue);
}


//Função para limpar apenas uma linha do LCD e escrever um texto
void cleanLCDRow(int row, String text) {
  lcd.setCursor(0, row);         // Define o início da escrita
  lcd.print("                "); // Apaga o conteúdo da linha
  lcd.setCursor(0, row);         // Define o início da escrita
  lcd.print(text);               // Escreve o texto enviado
}