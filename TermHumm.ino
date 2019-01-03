#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include "Adafruit_Si7021.h"
#include <FS.h> 
#include "Gsender.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

Gsender *gsender = Gsender::Instance();

Adafruit_Si7021 sensor = Adafruit_Si7021();

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#undef DEBUG
#undef GS_SERIAL_LOG_1         
#undef GS_SERIAL_LOG_2  
#undef VERIFY

const char* exceptionFile = "/exceptions.txt";
const char* ssid     = "SSID your router";
const char* password = "Password to router";

const String api_key = "Thingspeak.com write API key to sensor channel";
const String location = "Your sensor location name";

const String destinationEmail = "Your e-mail";

char* host = "api.thingspeak.com";

ADC_MODE(ADC_VCC);
 
float humidity, temperature;  

const int httpsPort = 443;
const char* fingerprint = "f9c2656cf9ef7f668bf735fe15ea829f5f55543e";

///////////////////////
void print(String message)
{
  #ifdef DEBUG
  Serial.print(message);
  #endif
}

////////////////////////
void println(String message)
{
 #ifdef DEBUG
  Serial.println(message);
  #endif
}

////////////////////////
void deepSleep(int interval)
{
    println("Deep sleep");
    ESP.deepSleep(1e6 * interval, WAKE_RF_DEFAULT);
}

///////////////////////
void deepSleepMax()
{
    println("Deep sleep max");
    ESP.deepSleep(ESP.deepSleepMax(), WAKE_RF_DEFAULT);
}

///////////////////////
void emailExceptions(String exceptions)
{
  char* last_error = "";
  
  timeClient.update();
  String currentTime = timeClient.getFormattedTime();

  String subject = "Error alert["+currentTime+"]: "+location;
  
  if (gsender->Subject(subject)->Send(destinationEmail, exceptions) == true) {

    println("Exceptions sent successfully.");

    SPIFFS.remove(exceptionFile);
  }
  else 
  {
    strcpy(last_error,gsender->getError());

    println(last_error);
  }
}

////////////////////////
void sendExceptions()
{
  File file = SPIFFS.open(exceptionFile, "r");

  String exceptions = "";
  
  if(file)
  {
    while (file.available())
    {
      exceptions += char(file.read());
    }
          
    file.close();

    println(exceptions);
    
    emailExceptions(exceptions);
    
    
  }
}

//////////////////
void registerException(String exception)
{
  File file = SPIFFS.open(exceptionFile, "w");

  if(file)
  {
     file.println(exception);
     println(exception);
     file.close();
  }
}

////////////////////////
void setup(void)
{
  
  Serial.begin(9600); 

  WiFi.begin(ssid, password);
  print("\n\r \n\rWorking to connect");

  int counter = 0;
  
  if(SPIFFS.begin() == false)
  {
    println("\n\r \n\SPIFFS was not mounted");
  }
  else
  {
    println("\n\r \n\SPIFFS was mounted");

    if (!SPIFFS.exists("/formatComplete.txt")) {
      println("Please wait 30 secs for SPIFFS to be formatted");
      SPIFFS.format();
      println("Spiffs formatted");
      
      File f = SPIFFS.open("/formatComplete.txt", "w");
      if (!f) 
      {
          println("file open failed");
      } else 
      {
          f.println("Format Complete");
          f.close();
      }
    } 
    else 
    {
      println("SPIFFS is formatted. Moving along...");
    }
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    print(".");

    counter++;

    if(counter > 50)
    {
      registerException("WiFi connection expired");
      deepSleep(60);
    }
  }

  timeClient.begin();

  timeClient.setTimeOffset(3600);

  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  
  if (SPIFFS.exists(exceptionFile)) 
  {
     println("Send error messages");
     sendExceptions();
  }

  println("");
  println("Temperature & hunidity watcher");
  print("Connected to ");
  println(ssid);
  print("IP address: ");
  println(WiFi.localIP().toString());

  WiFiClientSecure client;
  print("connecting...");

  if (!client.connect(host, httpsPort)) {
    println("failed");
    registerException("Connection failed!");
    deepSleep(60);
  }
  else
  {
    println("connected");
  }

#ifdef DEBUG
  if (client.verify(fingerprint, host)) {
    println("certificate matches");
  } else {
    println("certificate doesn't match");
  }
#endif

  bool dataToSend = false;

  pinMode(13, OUTPUT);

  digitalWrite(13, HIGH);

  delay(500);

  int checkCount = 0;
  
  while(true)
  {
    if (!sensor.begin()) 
    {
      checkCount++;
    }
    else
    {
      break;
    }

    if(checkCount > 3)
    {
      println("Did not find Si7021 sensor!");
      registerException("Did not find Si7021 sensor!");
      deepSleep(60);
    }

    delay(100);
  }
  
  humidity = sensor.readHumidity(); 
  temperature = sensor.readTemperature();

  float battery;

  if (!isnan(humidity) && !isnan(temperature)) {
    dataToSend = true;
  }

  battery = ESP.getVcc();

  if(dataToSend)
  {
    String url = "/update?api_key="+api_key+"&field1="+String(temperature,DEC)+"&field2="+String(humidity,DEC)+"&field3="+String(battery,DEC); 
    println(url);
    
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: GoraTempHumidity\r\n" +
                 "Connection: close\r\n\r\n");

    println("request sent");
  }
  else
  {
    String url = "/update?api_key="+api_key+"&field3="+String(battery,DEC); 
    println(url);
    
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: GoraBattery\r\n" +
                 "Connection: close\r\n\r\n");

    println("request sent");
  }
  
  println("++++++++++++++++++++++++++++++++++++++++++++++++++");

  counter = 0;
  int http200OKfound = 0;

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    println(line);
    if (line == "\r") {
      println("headers received");
      break;
    }
    else
    {
      if (line == "HTTP/1.1 200 OK") 
      {
        http200OKfound = 1;
      }
    }

    counter++;

    if(counter > 100)
    {
      println("headers didn't received");
      registerException("headers didn't received");
      deepSleep(60);
    }
  }

  if(http200OKfound = 0)
  {
    println("Http 200 OK didn't received");
    registerException("Http 200 OK didn't received");
  }
  
  digitalWrite(13, LOW);

  if(battery > 2600)
  {
    deepSleep(900);
  }
  else
  {
    println("Battery low");
    registerException("Battery low");
    deepSleepMax();
  }
}

////////////////////////
void loop(void)
{
  
}



