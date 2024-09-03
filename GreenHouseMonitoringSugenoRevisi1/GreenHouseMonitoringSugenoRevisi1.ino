#include "math.h"
#include "soc/sens_reg.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <WiFi.h>
#include "ThingSpeak.h"
#include <SPI.h>
#include <Wire.h>
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"
#include "DHT.h"

#define DHTPin 14
#define SoilPin 36 // ADC1_CHANNEL_0
#define TombolPin 17
#define Pump 5


#define DHTTYPE DHT11   // DHT 11

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     4 // Reset pin
#define SCREEN_ADDRESS 0x3c

const char* ssid = "Pohon Belimbing"; //"IOT Sawi";   // Nama SSID
const char* password = "klentang25"; //"iotsawi2022";   // Password SSID

WiFiClient  client;

unsigned long myChannelNumber = 3;
const char * myWriteAPIKey = "PL1DG8LN0APHAYMX"; // Kode API Key untuk update ke Thingspeak

DHT dht(DHTPin, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Timer variables
unsigned long lastTime = 0, lastDetik = 0, lastPump = 0;
#define timerDetik 1000  // Timer untuk penghitungan tiap detik

bool StartPump = false, PumpON = false, PumpOK = false, Kirim = false;
uint8_t TimePump = 0;
int Report = 0;
char timee[10];

float keputusan;
float soil = 0.0, suhu = 0.0;
float A, B;

// Member fuzzy
float udingin[] = {0, 0, 25};
float unormal[] = {20, 27, 36};
float upanas[] = {35, 50, 50};

float ukering[] = {0, 0, 45};
float usedang[] = {40, 60, 80};
float ubasah[] = {75, 85, 85};

float pompacepat = 0.5;
float pompasedang = 0.6;
float pompalama = 1;
float minr[10];
float Rule[10];

#define Timer       38 // detik, Lama maksimum pompa menyala ( sebanyak 100ml )

uint8_t JAM = 0, MENIT = 0, DETIK = 0;

float fudingin()
{
  if (suhu < udingin[1])
  {
    return 1;
  }
  else if (suhu >= udingin[1] && suhu <= udingin[2])
  {
    return (udingin[2] - suhu) / (udingin[2] - udingin[1]);
  }
  else if (suhu > udingin[2])
  {
    return 0;
  }
}
float funormal()
{
  if (suhu < unormal[0])
  {
    return 0;
  }
  else if (suhu >= unormal[0] && suhu <= unormal[1])
  {
    return (suhu - unormal[0]) / (unormal[1] - unormal[0]);
  }
  else if (suhu >= unormal[1] && suhu < unormal[2])
  {
    return (unormal[2] - suhu) / (unormal[2] - unormal[1]);
  }
  else if (suhu > unormal[2])
  {
    return 0;
  }
}
float fupanas()
{
  if (suhu < upanas[0])
  {
    return 0;
  }
  else if (suhu >= upanas[0] && suhu <= upanas[1])
  {
    return (suhu - upanas[0]) / (upanas[1] - upanas[0]);
  }
  else if (suhu > upanas[1])
  {
    return 1;
  }
}

float fukering()
{
  if (soil < ukering[1])
  {
    return 1;
  }
  else if (soil >= ukering[1] && soil <= ukering[2])
  {
    return (ukering[2] - soil) / (ukering[2] - ukering[1]);
  }
  else if (soil > ukering[2])
  {
    return 0;
  }
}
float fusedang()
{
  if (soil < usedang[0])
  {
    return 0;
  }
  else if (soil >= usedang[0] && soil <= usedang[1])
  {
    return (soil - usedang[0]) / (usedang[1] - usedang[0]);
  }
  else if (soil >= usedang[1] && soil <= usedang[2])
  {
    return (usedang[2] - soil) / (usedang[2] - usedang[1]);
  }
  else if (soil > usedang[2])
  {
    return 0;
  }
}
float fubasah()
{
  if (soil <= ubasah[0])
  {
    return 0;
  }
  else if (soil > ubasah[0] && soil < ubasah[1])
  {
    return (soil - ubasah[0]) / (ubasah[1] - ubasah[0]);
  }
  else if (soil >= ubasah[1])
  {
    return 1;
  }
}

float Min(float a, float b)
{
  if (a < b)
  {
    return a;
  }
  else if (b < a)
  {
    return b;
  }
  else
  {
    return a;
  }
}
void rule()
{
  minr[1] = Min(fudingin(), fukering());
  Rule[1] = pompasedang;

  minr[2] = Min(fudingin(), fusedang());
  Rule[2] = pompasedang;

  minr[3] = Min(fudingin(), fubasah());
  Rule[3] = pompacepat;

  minr[4] = Min(funormal(), fukering());
  Rule[4] = pompasedang;

  minr[5] = Min(funormal(), fusedang());
  Rule[5] = pompasedang;

  minr[6] = Min(funormal(), fubasah());
  Rule[6] = pompacepat;

  minr[7] = Min(fupanas(), fukering());
  Rule[7] = pompalama;

  minr[8] = Min(fupanas(), fusedang());
  Rule[8] = pompasedang;

  minr[9] = Min(fupanas(), fubasah());
  Rule[9] = pompasedang;
}

float defuzzyfikasi()
{
  rule();
  A = 0;
  B = 0;
  for (int i = 1; i <= 9; i++)
  {
    A += Rule[i] * minr[i];
    B += minr[i];
  }
  return A / B;
}

void setup()
{
  Serial.begin(9600);
  dht.begin();
  pinMode(SoilPin, INPUT);
  pinMode(Pump, OUTPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) // Deteksi OLED
  {
    for (;;); // Looping terus ketika tidak mendeteksi OLED
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  delay(1000);
  ThingSpeak.begin(client);  // Inisialisasi ThingSpeak
  
  display.display(); delay(2000);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(50,0);
  display.println(F("IoT"));
  display.setTextSize(1);
  display.setCursor(30,23);
  display.print(F("MONITORING SAWI"));
  display.display();
  delay(2000);
  Serial.println(F("DHTxx test!"));
  display.clearDisplay();
}

uint8_t CountConecting = 0, CountKirim = 3, CountReport = 3;
void loop()
{
  if (millis() - lastDetik >= timerDetik) // Update tiap Detik
  {
    lastDetik = millis();

    DETIK++;
    if (DETIK >= 60) // Update Menit
    {
      DETIK = 0;
      MENIT++;

      if (MENIT >= 60) // Update Jam
      {
        MENIT = 0;
        JAM++;
        if (JAM == 24)
        { JAM = 0; }

//        Cek fuzzy tiap jam
        keputusan = defuzzyfikasi();

        // Hasil fuzzy untuk menentukan durasi nyala pompa
        if(keputusan == pompacepat)
        {
          TimePump = Timer * pompacepat;
          StartPump = false;
          PumpOK = false;
          PumpON = false;
          lastPump = 0;
        }
        else if(keputusan == pompasedang)
        {
          TimePump = Timer * pompasedang;
          StartPump = true;
          PumpOK = true;
          PumpON = true;
          lastPump = 0;
        }
        else if(keputusan == pompalama)
        {
          TimePump = Timer * pompalama;
          StartPump = true;
          PumpOK = true;
          PumpON = true;
          lastPump = 0;
        }
      }
    }

    uint16_t adcValue = analogRead(SoilPin); // Cek kelembapan
    soil = 100.0 - ((adcValue/4095.0) * 100.0); // Kalkulasi nilai sensor ke prosentase
    
    suhu = dht.readTemperature(); // Cek suhu DHT11
  
    if (isnan(suhu)) // Jika nilai DHT11 tidak terbaca
    {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }

    // Update tampilan OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(80,0);
    sprintf(timee, "%02d:%02d:%02d", JAM, MENIT, DETIK);
    display.print(timee);
    display.setCursor(0,12);
    display.print(F("T: ")); display.printf("%0.1f",suhu); display.print(" C  ");
    display.print(F("S: ")); display.printf("%0.1f",soil); display.print(" %");
    display.display();

    // Update tampilan untuk status pengiriman ke Thingspeak
    if(CountReport < 3)
    {
      CountReport++;
      display.setCursor(0,24);
      if(Report == 200)
      {
        display.print(F("Update Success"));
      }
      else
      {
        display.print(F("Update Fail"));
      }
      display.display();
    }

    // Update tampilan ketika sedang proses mengirim ke Thingspeak
    if(CountKirim < 3)
    {
      CountKirim++;
      display.setCursor(0,24);
      display.print(F("Update To Thingspeak"));
      display.display();
      if(CountKirim == 3) { CountReport = 0; }
    }

    // Update untuk pompa menyala sesuai durasi / mati
    if(StartPump)
    {
      if(lastPump <= TimePump)
      {
        lastPump++;
        if(PumpON && PumpOK)
        {
          digitalWrite(Pump, HIGH);
          PumpON = false;
        }
        display.setCursor(0,0);
        display.print(F("Pompa ON ")); display.print(TimePump); display.print("s");
        display.display();
      }
      else
      {
        lastPump = 0;
        TimePump = 0;
        digitalWrite(Pump, LOW);
        display.setCursor(0,0);
        display.print(F("Pompa OFF"));
        display.display();
        PumpON = true;
        StartPump = false;
      }
    }
    else
    {
      display.setCursor(0,0);
      display.print(F("Pompa OFF"));
      display.display();
    }

    // Cek timer jika sudah detik kelipatan 15 maka akan update ke Thingspeak
    if (DETIK % 15 == 0)
    { CountKirim = 0; Kirim = true; }

    // Ketika sudah waktunya kirim ke Thingspeak
    if(Kirim)
    {  
      if(WiFi.status() != WL_CONNECTED) // Cek koneksi ke Wifi
      {
        CountConecting++;
        if(CountConecting > 3) { CountConecting = 0; Kirim = false; }

        Serial.print("Attempting to connect");
        if(WiFi.status() != WL_CONNECTED)
        {
          WiFi.begin(ssid, password); delay(500);
        }
        else
        {
          Serial.println("\nConnected.");
        }
      }

       // Mengatur setField data ke Thingspeak. Fieald 1 untuk suhu, Field 2 untuk kelembapan, Field 3 untuk durasi pompa
      ThingSpeak.setField(1, suhu);
      ThingSpeak.setField(2, soil);
      ThingSpeak.setField(3, TimePump);

      // Update ke Thingspeak sesuai data Field
      Report = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
      if(Report == 200) { Serial.println("Channel update successful."); Kirim = false; }
      else { Serial.println("Problem updating channel. HTTP error code " + String(Report)); }
    }
  }
}
