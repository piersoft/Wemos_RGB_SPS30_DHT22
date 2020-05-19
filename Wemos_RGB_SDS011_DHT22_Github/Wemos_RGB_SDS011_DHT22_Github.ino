#include <DHT.h>
#include <SDS011.h>
#include "base64.hpp"
#include "RunningAverage.h"
#include <ESP8266WiFi.h>


SDS011 sds;

float p10, p25;
int error;
RunningAverage pm25Stats(10);
RunningAverage pm10Stats(10);
RunningAverage temperatureStats(10);

#define DHTTYPE DHT22   // DHT 22
#define DEBUG

WiFiClient client;
// Wi-Fi Settings

const char* ssid = "retewifi"; // your wireless network name (SSID)
const char* pass = "passwordwifi"; // your Wi-Fi network password


const int httpPort = 80;
#define pinDHT D5 // per NodeMcu obbligatorio D1, WemosD1 --> D4 con dht integrata o D6 dht esterna

// Inizializzazione del sensore DHT
DHT dht(pinDHT, DHTTYPE);


// Variabili temporanee
static char tempCelsius[7];
static char tempUmidita[7];
unsigned long previousMillis = 0; //will store last time LED was updated
unsigned long interval = 60000 * 15; //interval at which to blink (milliseconds)
int conta;

// ThingSpeak Settings
const int channelID = 9981111; //channel ID ThingSpeak
String writeAPIKey = "SQCNCU8TXXXXXXX"; // write API key for your ThingSpeak Channel
const char* server = "184.106.153.149"; //


void(* Riavvia)(void) = 0;

void setup() {

  Serial.begin(9600);
  sds.begin(D2,D3); //NodeMcu (D8,D7) cosi si può caricare lo sketch senza staccare SDS011
  delay(10);

  dht.begin();
  delay(1000);
  // Connessione alla rete WiFi

  Serial.println();
  Serial.println();
  Serial.println("------------- Avvio connessione ------------");
  Serial.print("Tentativo di connessione alla rete: ");
  Serial.println(ssid);

  /*
      Viene impostata l'impostazione station (differente da AP o AP_STA)
     La modalità STA consente all'ESP8266 di connettersi a una rete Wi-Fi
     (ad esempio quella creata dal router wireless), mentre la modalità AP
     consente di creare una propria rete e di collegarsi
     ad altri dispositivi (ad esempio il telefono).
  */

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Sei connesso ora alla rete: ");
  Serial.println(ssid);
  Serial.println("WiFi connessa");


  float h = dht.readHumidity();

  // Lettura temperatura in gradi Celsius
  float t = dht.readTemperature();

  // Verifica se la lettura è riuscita altrimenti si ripete la rilevazione
  if (isnan(h) || isnan(t)) {
    Serial.println("Impossibile leggere i dati dal sensore DHT!");
    strcpy(tempCelsius, "Fallito");
    strcpy(tempUmidita, "Fallito");

    // loop();
  } else Serial.println("DHT OK");
  sds.sleep(); //Se SDS011 non spegne la ventola sono invertiti D7 e D8
  Serial.flush();

}

void loop() {
  Serial.flush();
  unsigned long currentMillis = millis();

   
  if (currentMillis - previousMillis > interval || conta==0) {
    delay(4000);
    previousMillis = currentMillis; //save the last time you blinked the LED
    conta++;
    pm25Stats.clear();
    pm10Stats.clear();
    temperatureStats.clear();
    sds.wakeup(); 

    Serial.println("Calibrating SDS011 (15 sec)");
    delay(15000);
    for (int i = 0; i < 10; i++)
    {
      error = sds.read(&p25, &p10);

      if (!error && p25 < 999 && p10 < 1999) {
        pm25Stats.addValue(p25);
        pm10Stats.addValue(p10);
      }

      Serial.println("Average PM10: " + String(pm10Stats.getAverage()) + ", PM2.5: " + String(pm25Stats.getAverage()));
      delay(1500);
    }


    dht.begin();
    delay(500);// inizializzazione del DHT al D0/GPIO16


    // La lettura da parte del sensore può richiedere anche più di 2 secondi

    // lettura dell'umidità
    float h = dht.readHumidity();

    // Lettura temperatura in gradi Celsius
    float t = dht.readTemperature();

    // Verifica se la lettura è riuscita altrimenti si ripete la rilevazione
    if (isnan(h) || isnan(t)) {
      Serial.println("Impossibile leggere i dati dal sensore DHT!");
      strcpy(tempCelsius, "Fallito");
      strcpy(tempUmidita, "Fallito");
      Riavvia();
      // loop();
    }
    else {
      // Calcola i valori di temperatura in Celsius e Umidità

      float hic = dht.computeHeatIndex(t, h, false);
      dtostrf(hic, 6, 2, tempCelsius);

      dtostrf(h, 6, 2, tempUmidita);


      Serial.print("Temperatura: ");
      Serial.print(t);
      Serial.print(" *C ");

      Serial.print("Umidita': ");
      Serial.print(h);
      Serial.println(" %\t");
      Serial.flush();

      float pm25n = normalizePM25(float(pm25Stats.getAverage()), h);
      float pm10n = normalizePM10(float(pm10Stats.getAverage()), h);

      if (isnan(pm25n) || isnan(pm10n)) {
        Serial.print("Errore sds011");
        Riavvia();
      } else {
 sds.sleep();
 delay(1000);
        String body = "field1=";
        body += String(pm10n);
        body += "&field2=";
        body += String(pm25n);
        body += "&field3=";
        body += String(t);
        body += "&field4=";
        body += String(h);
        WiFiClient client;
        if (!client.connect(server, httpPort)) {
          Serial.println("connection failed");
          return;
        }
        Serial.println("\nconnected to server");
        String url = "POST /update?api_key=" + writeAPIKey + "&field1=" + String(pm10n) + "&field2=" + String(pm25n) + "&field3=" + String(t) + "&field4=" + String(h) + " HTTP/1.1\r\nHost: " + server + "\r\nConnection: close\r\n\r\n";
       if (client.connect(server, 80)) {
        Serial.print("Requesting URL: ");
        Serial.println(url);
        client.print(url);
        // Read all the lines of the reply from server and print them to Serial
        while (client.available()) {
          String line = client.readStringUntil('\r');
          Serial.print(line);
        }
        }
        delay(8000);
      }

       
      

     
      // Serial.println("Sleep(" + String(sleepTime) + ")");
     
      //Serial.flush();
      // delay(2000);

      //delay(sleepTime * 1000);
      // setup();
       // Riavvia();
    }
  }
}

float normalizePM25(float pm25, float humidity) {
  return pm25 / (1.0 + 0.48756 * pow((humidity / 100.0), 8.60068));
}

float normalizePM10(float pm10, float humidity) {
  return pm10 / (1.0 + 0.81559 * pow((humidity / 100.0), 5.83411));
}