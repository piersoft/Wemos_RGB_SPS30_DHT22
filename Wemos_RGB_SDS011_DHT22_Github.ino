
#include "sps30.h"
#include "base64.hpp"
#include "RunningAverage.h"
#include <ESP8266WiFi.h>

#include <WiFiClient.h>


#define SP30_COMMS I2C_COMMS

/////////////////////////////////////////////////////////////
/* define RX and TX pin for softserial and Serial1 on ESP32
   can be set to zero if not applicable / needed           */
/////////////////////////////////////////////////////////////
#define TX_PIN 0
#define RX_PIN 0

/////////////////////////////////////////////////////////////
/* define driver debug
   0 : no messages
   1 : request sending and receiving
   2 : request sending and receiving + show protocol errors */
//////////////////////////////////////////////////////////////
#define DEBUG 0

///////////////////////////////////////////////////////////////
/////////// NO CHANGES BEYOND THIS POINT NEEDED ///////////////
///////////////////////////////////////////////////////////////

// function prototypes (sometimes the pre-processor does not create prototypes themself on ESPxx)
void serialTrigger(char * mess);
void ErrtoMess(char *mess, uint8_t r);
void Errorloop(char *mess, uint8_t r);
void GetDeviceInfo();
bool read_all();


// create constructor
SPS30 sps30;
const char* ssid = "Piersoft"; // your wireless network name (SSID)
const char* pass = "12345678"; // your Wi-Fi network password

const int httpPort = 80;
const int channelID = 11111111111; //channel ID ThingSpeak
String writeAPIKey = "YYYYYYYYYYYYY"; // write API key for your ThingSpeak Channel
const char* server = "184.106.153.149";
void(* Riavvia)(void) = 0;
//const int sleepTime = 60 * 60 * 24;
RunningAverage pm25Stats(10);
RunningAverage pm10Stats(10);
RunningAverage pm25NStats(10);
RunningAverage pm10NStats(10);
RunningAverage partSizeStats(10);
unsigned long previousMillis = 0; //will store last time LED was updated
unsigned long interval = 60000 * 1; //interval at which to blink (milliseconds)
const int buttonPin = D5;
const int redPin = D7;
const int greenPin = D8;
const int bluePin = D6;

void setup() {
  pinMode(buttonPin, INPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  
  Serial.begin(115200);

  Serial.println(F("Trying to connect"));

  // set driver debug level
  sps30.EnableDebugging(DEBUG);

  // set pins to use for softserial and Serial1 on ESP32
  if (TX_PIN != 0 && RX_PIN != 0) sps30.SetSerialPin(RX_PIN, TX_PIN);

  // Begin communication channel;
  if (sps30.begin(SP30_COMMS) == false) {
    Errorloop("could not initialize communication channel.", 0);
  }

  // check for SPS30 connection
  if (sps30.probe() == false) {
    Errorloop("could not probe / connect with SPS30.", 0);
  }
  else
    Serial.println(F("Detected SPS30."));

  // reset SPS30 connection
  if (sps30.reset() == false) {
    Errorloop("could not reset.", 0);
  }

  // read device info
  GetDeviceInfo();

  // start measurement
  if (sps30.start() == true)
    Serial.println(F("Measurement started"));
  else
    Errorloop("Could NOT start measurement", 0);

  if (SP30_COMMS == I2C_COMMS) {
    if (sps30.I2C_expect() == 4)
      Serial.println(F(" !!! Due to I2C buffersize only the SPS30 MASS concentration is available !!! \n"));
  }
   
  unsigned long autoclean = 60 * 60 *24; //interval at which to autoclean (seconds)
  sps30.SetAutoCleanInt(autoclean);
  
  Serial.println("------------- Avvio connessione ------------");
  Serial.print("Tentativo di connessione alla rete: ");
  Serial.println(ssid);
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
  Serial.println("Tutto pronto. Ogni 15 minuti leggo e invio e ogni 24 ore faccio il clean");
  Serial.flush();
}

void loop() {
   int buttonState;
  buttonState = digitalRead(buttonPin);
  int valore=pm10Stats.getAverage();
  //valore=36; //DEBUG
  if (buttonState == LOW) {
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, LOW);
  }else {
    //Serial.println("Pulsante premuto");
    if (valore>=50){
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, LOW);
    }else if (valore>=35){
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, 100);
    digitalWrite(bluePin, LOW);
    }else {
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, HIGH);
    digitalWrite(bluePin, LOW);
    }
  
  }
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis; //save the last time you blinked the LED

    Serial.println("------------- Riavvio connessione ------------");
    Serial.print("Tentativo di connessione alla rete: ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);


    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
      delay(250);
      Serial.print(".");
    }

    Serial.println("");
    Serial.print("Sei riconnesso ora alla rete: ");
    Serial.flush();
    read_all();

  }

}

/**
   @brief : read and display device info
*/
void GetDeviceInfo()
{
  char buf[32];
  uint8_t ret;

  //try to read serial number
  ret = sps30.GetSerialNumber(buf, 32);
  if (ret == ERR_OK) {
    Serial.print(F("Serial number : "));
    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get serial number", ret);

  // try to get product name
  ret = sps30.GetProductName(buf, 32);
  if (ret == ERR_OK)  {
    Serial.print(F("Product name  : "));

    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get product name.", ret);

  // try to get article code
  ret = sps30.GetArticleCode(buf, 32);
  if (ret == ERR_OK)  {
    Serial.print(F("Article code  : "));

    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get Article code .", ret);
}

/**
   @brief : read and display all values
*/
bool read_all()
{
  
  static bool header = true;
  uint8_t ret, error_cnt = 0;
  struct sps_values val;
  for (int i = 0; i < 10; i++)
  {
    // loop to get data
    do {

      ret = sps30.GetValues(&val);

      // data might not have been ready
      if (ret == ERR_DATALENGTH) {

        if (error_cnt++ > 3) {
          ErrtoMess("Error during reading values: ", ret);
          return (false);
        }
        delay(1000);
      }

      // if other error
      else if (ret != ERR_OK) {
        ErrtoMess("Error during reading values: ", ret);
        return (false);
      }

    } while (ret != ERR_OK);

    // only print header first time
    if (header) {
      Serial.println(F("-------------Mass -----------    ------------- Number --------------   -Average-"));
      Serial.println(F("     Concentration [μg/m3]             Concentration [#/cm3]             [μm]"));
      Serial.println(F("P1.0\tP2.5\tP4.0\tP10\tP0.5\tP1.0\tP2.5\tP4.0\tP10\tPartSize\n"));
      header = false;
    }



    Serial.print(val.MassPM1);
    Serial.print(F("\t"));
    Serial.print(val.MassPM2);
    Serial.print(F("\t"));
    Serial.print(val.MassPM4);
    Serial.print(F("\t"));
    Serial.print(val.MassPM10);
    Serial.print(F("\t"));
    Serial.print(val.NumPM0);
    Serial.print(F("\t"));
    Serial.print(val.NumPM1);
    Serial.print(F("\t"));
    Serial.print(val.NumPM2);
    Serial.print(F("\t"));
    Serial.print(val.NumPM4);
    Serial.print(F("\t"));
    Serial.print(val.NumPM10);
    Serial.print(F("\t"));
    Serial.print(val.PartSize);
    Serial.print(F("\n"));

    pm25Stats.addValue(val.MassPM2);
    pm10Stats.addValue(val.MassPM10);
    pm25NStats.addValue(val.NumPM2);
    pm10NStats.addValue(val.NumPM10);
    partSizeStats.addValue(val.PartSize);

    //   Serial.println("Average PM10: " + String(pm10Stats.getAverage()) + ", PM2.5: "+ String(pm25Stats.getAverage()));
    delay(1500);
  }

  
  float pm25n = pm25Stats.getAverage();
  float pm10n = pm10Stats.getAverage();
  float pm25nn = pm25NStats.getAverage();
  float pm10nn = pm10NStats.getAverage();
  float partsize = partSizeStats.getAverage();

  WiFiClient client;
  if (!client.connect(server, httpPort)) {
    Serial.println("connection failed");
    // return;
    Riavvia();
  }
  Serial.println("\nconnected to server");
  String url = "POST /update?api_key=" + writeAPIKey + "&field1=" + String(pm10n) + "&field2=" + String(pm25n) + "&field3=" + String(pm10nn) + "&field4=" + String(pm25nn) + "&field5=" + String(partsize) + " HTTP/1.1\r\nHost: 184.106.153.149\r\nConnection: close\r\n\r\n";
  Serial.print("Requesting URL: ");
  Serial.println(url);
  client.print(url);
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  delay(5000);
  sps30.stop();
  
}


/**
    @brief : continued loop after fatal error
    @param mess : message to display
    @param r : error code

    if r is zero, it will only display the message
*/
void Errorloop(char *mess, uint8_t r)
{
  if (r) ErrtoMess(mess, r);
  else Serial.println(mess);
  Serial.println(F("Program on hold"));
  for (;;) delay(100000);
}

/**
    @brief : display error message
    @param mess : message to display
    @param r : error code

*/
void ErrtoMess(char *mess, uint8_t r)
{
  char buf[80];

  Serial.print(mess);

  sps30.GetErrDescription(r, buf, 80);
  Serial.println(buf);
}
