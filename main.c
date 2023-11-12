#include <Adafruit_SCD30.h>
#include <Wire.h>
#include "rgb_lcd.h"
#include "Adafruit_MPR121.h"
#include "RTC.h"
#include <WiFiUdp.h>
#include "WiFiS3.h"
#include <NTPClient.h>
#include "arduino_secrets.h" 

#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

#ifndef _DEBUG_ON
#define _DEBUG_ON
#endif

Adafruit_SCD30  scd30;
rgb_lcd lcd;
Adafruit_MPR121 cap = Adafruit_MPR121();

const unsigned int max_array = 240; //Max size for Uno Rev4 32 Ko is 1100 but much more can be stored on 256 kB flash memory with a specific implementation 
                                   // 262 144 bits / (3 x 16 bits) = 5 461 write by day
                                  // EEPROM memory has a specified life of 100,000 write/erase cycles
                                 // If you store the last 24 hours in this memory, EEPROM can stand only 18 days!
                                // consider using a SD Card or a database..
const unsigned int one_second = 1000;
const unsigned int one_minute = 60000;
//const unsigned long one_hour = 3600000;
//const unsigned long one_day = 86400000;
const unsigned long wait_time = one_minute * 2; //if you want 24 hours of measures, divide one day in seconds (86400) by max_array
//const unsigned long interval = wait_time / 2;

// Déclaration des variables
struct atmospheric_reading
{
  String       HH_MM_SS;
  unsigned int co2;
  unsigned int temperature;
  unsigned int humidity;
};
bool already_looped = false;
unsigned int cur_reading = 0;
atmospheric_reading tab_reading[max_array];

//tempo between each measure
unsigned long tempo = 0;

// Keeps track of the last pins touched
// so we know when buttons are 'released'
const unsigned int max_pins = 4;
unsigned int last_touched = 0;
unsigned int curr_touched = 0;
bool pin_touched[max_pins] = { 0 };
bool pin_released[max_pins] = { 0 };
bool released = true;    //Force first reading

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;       // your network password (use for WPA, or use as key for WEP)
int wifiStatus = WL_IDLE_STATUS;
WiFiServer server(80);

//const long utcOffsetWinter = 3600;  // Offset from UTC in seconds (3600 seconds = 1h) -- UTC+1 (Central European Winter Time)
//const long utcOffsetSummer = 7200; // Offset from UTC in seconds (7200 seconds = 2h) -- UTC+2 (Central European Summer Time)
//unsigned long lastupdate = 0UL;
// Define NTP Client to get time
WiFiUDP Udp;
NTPClient timeClient(Udp, "pool.ntp.org"); //, utcOffsetWinter);
auto timeZoneOffsetHours = 1;

void setup(void) {
  init_serial();

  init_lcd();

  init_captor();

  init_touch_sensor();

  init_network();

  init_clock();
}

void loop() {  
  // Get the currently touched pads
  curr_touched = cap.touched();
  
  for (uint8_t i=0; i<max_pins; i++) {
    // if it is touched and wasnt touched before
    if ((curr_touched & _BV(i)) && !(last_touched & _BV(i)) ) {
      pin_touched[i] = true;
    }
    // if it was touched and now isnt
    if (!(curr_touched & _BV(i)) && (last_touched & _BV(i)) ) {
      released = true;
      pin_touched[i] = false;
      pin_released[i] = true;
    }
  }
  // reset our state
  last_touched = curr_touched;

  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    debug_println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        debug_print(String(c).c_str());     // print it out to the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            client.println("<html><head>");
            client.println("<script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>");
            client.println("<script type=\"text/javascript\">");
            client.println("google.charts.load('current', {'packages':['corechart']});");
            client.println("google.charts.setOnLoadCallback(drawChart);");
            client.println("function drawChart() {");

            client.println("var dataCO2 = google.visualization.arrayToDataTable([['i', 'PPM']");
            //Simple technique to rotate over previous values if exists but the cur_reading is missed. just need a bool to bypass this problem if needed
            uint8_t i = cur_reading + 1;
            while (i != cur_reading){
              if (i >= max_array) i = 0;
              if (tab_reading[i].co2) { //Print the value if it exists
                client.print(", [\""); client.print(tab_reading[i].HH_MM_SS); client.print("\","); client.print(tab_reading[i].co2); client.println("]");
              }
              i++;
            }
            client.println("]);");

            client.println("var data = google.visualization.arrayToDataTable([['i', 'C', '%']");
            //Simple technique to rotate over previous values if exists but the cur_reading is missed
            uint8_t j = cur_reading + 1; //i may be used as is
            while (j != cur_reading){
              if (j >= max_array - 1) j = 0;
              if (tab_reading[j].temperature and tab_reading[j].humidity) { //Print the value if it exists
                client.print(", [\""); client.print(tab_reading[j].HH_MM_SS); client.print("\","); client.print(tab_reading[j].temperature); client.print(","); client.print(tab_reading[j].humidity);client.println("]");
              }
             j++;
            }
            client.println("]);");

	          client.println("var optionsCO2 = {title: 'CO2',curveType: 'function',legend: { position: 'bottom' }};");
	          client.println("var options = {title: 'Temperature & Humidity',curveType: 'function',legend: { position: 'bottom' }};");
	          client.println("var chartCO2 = new google.visualization.LineChart(document.getElementById('curve_chartCO2'));chartCO2.draw(dataCO2, optionsCO2);");
	          client.println("var chart = new google.visualization.LineChart(document.getElementById('curve_chart'));chart.draw(data, options);");
	          client.print("}</script></head><body>");
            client.print("<text font-family=\"Arial\" font-size=\"14\" stroke=\"none\" stroke-width=\"0\" fill=\"#222222\">");
            if (already_looped){client.print("Over ");client.print(max_array);}
            else client.print(cur_reading);
            client.println(" measurements done.</text><br />");
	          client.println("<div id=\"curve_chartCO2\" style=\"width: 1500px; height: 450px\"></div>");
	          client.println("<div id=\"curve_chart\" style=\"width: 1500px; height: 450px\"></div>");
	          client.println("</body></html>");
            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
      
    }
    // close the connection:
    client.stop();
    debug_println("client disconnected");
  }

  if (pin_released[1]){
    if (timeZoneOffsetHours >= 14){
      lcd_printXY(0, 0, "Error max");
      lcd_printXY(0, 1, "GMT+14");
      debug_println("GMT+14 Max");
    } else{
      timeZoneOffsetHours++;
      debug_print("GMT=");debug_println(String(timeZoneOffsetHours).c_str());
    }
    pin_released[1] = false;
  }

  if (pin_released[2]){
    if (timeZoneOffsetHours <= -12){
      lcd_printXY(0, 0, "Error min");
      lcd_printXY(0, 1, "GMT-12");
      debug_println("GMT-22 Min");
    } else{
      timeZoneOffsetHours--;
      debug_print("GMT=");debug_println(String(timeZoneOffsetHours).c_str());
    }
    pin_released[2] = false;
  }

  if (pin_released[3]){
    init_clock();
    debug_println("RTC synced");
    pin_released[3] = false;
  }

  if (((millis() - tempo) >= wait_time) or pin_released[0]){
    released = false;
    pin_released[0] = false;
    tempo = millis();

    if (scd30.dataReady()){
      debug_println("Data available!");

      if (!scd30.read()){
        lcd_printXY(0, 0, "Error reading");
        lcd_printXY(0, 1, "sensor data");
        return;
      }
      tab_reading[cur_reading].HH_MM_SS = read_time();
      tab_reading[cur_reading].co2 = scd30.CO2;
      tab_reading[cur_reading].temperature = scd30.temperature;
      tab_reading[cur_reading].humidity = scd30.relative_humidity;
      lcd.clear();    
      lcd.setCursor(0, 0);
      if (tab_reading[cur_reading].co2 > 1500) {
        lcd.print("Alert ");
      }
      else if (tab_reading[cur_reading].co2 > 1000) {
        lcd.print("Warning ");
      }    
      else if (tab_reading[cur_reading].co2 > 800) {
        lcd.print("Info ");
      }
      lcd.print("CO2:");
      lcd.print(tab_reading[cur_reading].co2);
      lcd_printXY(0, 1, "Temp:");
      lcd.print(tab_reading[cur_reading].temperature);
      lcd.print("c Hum:");
      lcd.print(tab_reading[cur_reading].humidity);
      lcd.print("%");

      cur_reading++;
      if (cur_reading>=max_array) {
        cur_reading = 0;
        already_looped = true;
      }
    }
  }
  delay(10);
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  debug_print("SSID: ");
  debug_println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  debug_print("IP Address: ");
  debug_println(ip.toString().c_str());

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  debug_print("signal strength (RSSI):");
  debug_print(String(rssi).c_str());
  debug_println(" dBm");
  // print where to go in a browser:
  debug_print("To see this page in action, open a browser to http://");
  debug_println(ip.toString().c_str());
}

void init_serial() {
  Serial.begin(115200);
  while (!Serial) delay(10);     // will pause Zero, Leonardo, etc until serial console opens
  Serial.flush();
}

void debug_print(const char *string) {
#ifdef _DEBUG_ON
  Serial.print(string);
#endif
}

void debug_println(const char *string) {
#ifdef _DEBUG_ON
  Serial.println(string);
#endif
}

void init_lcd(void) {
  lcd.begin(16, 2);
  lcd.clear();
  lcd_printXY(0, 0, "Init");
  lcd_printXY(0, 1, "LCD");
  delay(1000);
}

void init_captor(void) {
  debug_println("Init Adafruit SCD30");
  while (!scd30.begin()) {
    debug_println("SCD30 not found, check wiring?");
    lcd_printXY(0, 0, "Failed to find");
    lcd_printXY(0, 1, "SCD30 chip");
    delay(1000);
  }

  debug_println("SCD30 Found!");
  lcd.clear();
  lcd_printXY(0, 0, "Init");
  lcd_printXY(0, 1, "captor");
  scd30.setMeasurementInterval(wait_time / 2);
  delay(1000);
}

void lcd_printXY(int x, int y, const char *string) {
    lcd.setCursor(x, y);
    lcd.print(string);
}

void init_touch_sensor(void) {
  debug_println("Init Adafruit MPR121 Capacitive Touch sensor"); 
  
  // Default address is 0x5A, if tied to 3.3V its 0x5B
  // If tied to SDA its 0x5C and if SCL then 0x5D
  if (!cap.begin(0x5A)) {
    debug_println("MPR121 not found, check wiring?");
    lcd_printXY(0, 0, "Failed to find");
    lcd_printXY(0, 1, "MPR121 chip");
    while (1);
  }
  debug_println("MPR121 found!");
  lcd.clear();
  lcd_printXY(0, 0, "Init");
  lcd_printXY(0, 1, "touch buttons");
}

void init_network(void) {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    debug_println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    debug_println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    debug_print("Attempting to connect to Network named: ");
    debug_println(ssid);                   // print the network name (SSID);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(5000);
  }
  server.begin();                           // start the web server on port 80
  printWifiStatus();                        // you're connected now, so print out the status
  lcd.clear();
  lcd_printXY(0, 0, "Init");
  lcd_printXY(0, 1, "wifi");
}

void init_clock(void){
  RTC.begin();
  Serial.println("\nStarting connection to server...");
  timeClient.begin();
  timeClient.update();

  // Get the current date and time from an NTP server and convert
  // it to UTC +2 by passing the time zone offset in hours.
  // You may change the time zone offset to your local one.
  auto unixTime = timeClient.getEpochTime() + (timeZoneOffsetHours * 3600);
  Serial.print("Unix time = ");
  Serial.println(unixTime);
  RTCTime timeToSet = RTCTime(unixTime);
  RTC.setTime(timeToSet);
}

String read_time(void){
  // Retrieve the date and time from the RTC
  RTCTime currentTime;
  RTC.getTime(currentTime);
  String HMS = String(currentTime.getHour()) + ":" + String(currentTime.getMinutes()) + ":" + String(currentTime.getSeconds());
  //Serial.print("HMS=");Serial.println(HMS);
  return HMS;
}
