#include <UnixTime.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

//#include <WebServer.h>
#include <ESPmDNS.h>
//#include <Update.h>


/*
 * --------------------------------
 * THIS IS ESP32 WEATHER DISPLAY V3
 * --------------------------------
 * 
 */


LiquidCrystal_I2C lcd(0x3F, 16, 2);

int light_strip_pin = 17;

int sun_pixels = 44; // Set to the number of pixels on your led strip. I have 44 total
int side_pixels = 17; // On each side window there are 17 led pixels.

int pir_sensor_pin = 34;

int servo_1_pin = 15;
int servo_2_pin = 2;
int servo_3_pin = 4;
int servo_4_pin = 16;

int demo_switch_pin = 25;
bool demo_mode = false;

Servo servo_1;
Servo servo_2;
Servo servo_3;
Servo servo_4;

const char* ssid = "*******";       //Your Wifi name here
const char* password = "*******!";  //Your Wifi Password here
const char* host = "esp32weather";

const String endpoint = "https://api.openweathermap.org/data/2.5/weather?lat=********&lon=*******&appid="; //replace the ****** with your latitude and longitude
const String key = "*****************************";  //Your openWeatherMap key here
const String endpointForcast = "http://api.openweathermap.org/data/2.5/forecast?lat=********&lon=******&cnt=1&appid="; //replace the ****** with your latitude and longitude 


const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  //Replace with your GMT offset (seconds)
const int daylightOffset_sec = 0;   //Replace with your daylight offset (seconds)

unsigned long currentMillis;
unsigned long previousMinuteMillis = 0;
unsigned long previousFadeMillis = 0;
unsigned long previousMotionMillis = 0;
unsigned long previousServoAttachMillis = 0;
unsigned long previousServoWriteMillis = 0;
int servoAttachDelay = 100;
int servoWriteDelay = 1000;
bool servoAttachTrigger = false;
bool servoWriteTrigger = false;
int fade_delay = 50;
int update_weather_time = 10;  //10 loops of the 1 minute loop for pulling new weather data
int update_loop_counter = 0;
int motion_timer = 10;  //seconds

int demo_old_time = 0;
int time_since_updating_servos = 0;

int dusk_time = 20;
int minute_timer_delay = 60000;  //60000;
//const int weatherSampleTime = 10;

bool bedtime = true;

long sys_sunrise = 0;
long sys_sunset = 0;
int current_min = 0;
float main_temp = 0;
int main_pressure = 0;
int main_humidity = 0;
int visibility = 0;
float wind_speed = 0;
float wind_gust = 0;
int clouds_all = 0;
float rain_percent = 0;
char forcast_time;
int precipitation_type = 0;  //0 == none, 1 == rain, 2 == snow, 3 == rain and snow

boolean isMotionDetected = false;
int side_fade_count = 0;
int side_fade_trigger = 0;
bool turn_on_backlight_trigger = false;
bool updateServoQueue = false;
int lcd_display_counter = 0;

int total_pixels = sun_pixels + side_pixels + side_pixels;
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(total_pixels, light_strip_pin, NEO_GRB + NEO_KHZ800);

UnixTime stamp(-5);

// Checks if motion was detected
void IRAM_ATTR detectsMovement() {
  //Serial.println("MOTION DETECTED!!!");
  if (!bedtime) {
    isMotionDetected = true;
    previousMotionMillis = millis();
    if (!demo_mode) {
      side_fade_trigger = 1;
      turn_on_backlight_trigger = true;
    }
  } else {
    //Serial.println("bedtime");
  }
}

void setup() {

  Serial.begin(115200);

  pixels.begin();

  //Init demo pin switch and check if it is in demo mode or not.
  pinMode(demo_switch_pin, INPUT_PULLUP);
  demo_mode = !digitalRead(demo_switch_pin);

  if (!demo_mode) {
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Connecting to WiFi..");
    }

    Serial.println("Connected to the WiFi network");
    delay(1000);

    /*use mdns for host name resolution*/
    if (!MDNS.begin(host)) {  //http://esp32weather.local
      Serial.println("Error setting up MDNS responder!");
      while (1) {
        delay(1000);
      }
    }
    Serial.println("mDNS responder started");


    if ((WiFi.status() == WL_CONNECTED)) {

      parse_json(getWeatherRequest());
      parse_forcast_json(getWeatherForcastRequest());
      delay(100);
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      delay(100);

      for (int i = 0; i < 10; i++) {
        current_min = getLocalTimeMin();
        if (current_min != -1) {
          break;
        }
        Serial.print("failed to get time, trying again: ");
        Serial.println(i);
      }

      if (current_min == -1) {
        for (int t = 0; t < sun_pixels + side_pixels + side_pixels; t++) {
          pixels.setPixelColor(t, pixels.Color(20, 20, 20));
          pixels.show();
        }
        ESP.restart();
      }
      //current_min = 390;
      calculate_sunrise_sunset_math(sys_sunrise, sys_sunset, current_min);  //current_min
    }

    if (current_min >= 420 && current_min <= 1200) {
      bedtime = false;
    }
  } else {
    //DEMO MODE
    current_min = 350;  //5:30AM
    bedtime = true;
    minute_timer_delay = 100;
    main_temp = 297.14;
    main_pressure = 0;
    main_humidity = 37;
    visibility = 0;
    wind_speed = 0.44704 * 18;
    wind_gust = 0;
    clouds_all = 30;
    rain_percent = 25;
    sys_sunrise = 1683540000;
    sys_sunset = 1683590400;
    dusk_time = 60;
    calculate_sunrise_sunset_math(sys_sunrise, sys_sunset, current_min);
    update_weather_time = 250;
    Serial.println("demo mode");
  }

  servo_1.attach(servo_1_pin);
  servo_2.attach(servo_2_pin);
  servo_3.attach(servo_3_pin);
  servo_4.attach(servo_4_pin);

  pinMode(pir_sensor_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pir_sensor_pin), detectsMovement, RISING);
  if (!bedtime) {
    setServos();
  }

  lcd.init();
  if (!bedtime) {
    lcd.backlight();
  }
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Humidity: %" + String(main_humidity));
  lcd.setCursor(0, 1);
  lcd.print("mins");

  delay(1000);
  if (!bedtime) {
    lcd.noBacklight();
  }

  Serial.println("Lets Rock and Role!");
}

void loop() {

  currentMillis = millis();

  if (demo_mode && digitalRead(demo_switch_pin)) {
    //If leaving demo mode then reboot the esp32 incase it never set up wifi stuff.
    delay(100);
    ESP.restart();
  } else if (!demo_mode && !digitalRead(demo_switch_pin)){
    // if the enabling demo mode, just reboot the esp. this is a quick addition, and not properly implemented hence the reboot.
    delay(100);
    ESP.restart();
  }
  //demo_mode = !digitalRead(demo_switch_pin);

  //Motion detection timer
  if (isMotionDetected && (currentMillis - previousMotionMillis > (motion_timer * 1000))) {
    isMotionDetected = false;
    if (!demo_mode) {
      side_fade_trigger = 2;
      lcd.noBacklight();
    }
    Serial.println("motion stopped");
  }

  if (turn_on_backlight_trigger) {
    lcd.backlight();
    turn_on_backlight_trigger = false;
  }

  //1 minute loop
  if ((currentMillis - previousMinuteMillis) >= minute_timer_delay) {  //60000
    previousMinuteMillis = currentMillis;
    //Code below will run every time this loop runs

    //update our current time variable
    current_min++;
    calculate_sunrise_sunset_math(sys_sunrise, sys_sunset, current_min);

    time_since_updating_servos++;

    //if it is between 7:00 AM and 7:30 PM then bedtime mode is false
    if (current_min >= 420 && current_min <= 1170) { // 1170/60 min = 7:30 PM
      bedtime = false;
    } else {
      bedtime = true;
    }
 
    //if Demo mode is on then change some time values.
    if (current_min >= 420 - 60 && current_min <= 120 + 60 && demo_mode) {
      minute_timer_delay = 125;
    } else if (demo_mode) {
      minute_timer_delay = 20;
    }
    if (current_min == 420) {
      demo_old_time = 1;
      side_fade_trigger = 1;
      lcd.backlight();
    } else if (current_min == 1200) {
      demo_old_time = 2;
      side_fade_trigger = 2;
    }

    //Reset the current min counter at 1:00AM
    if (current_min >= 1500) {
      current_min = 0;
    }

    //update servo position if it is queued
    if (updateServoQueue) {
      if (!bedtime) {
        if (!isMotionDetected) {
          setServos();
          time_since_updating_servos = 0;
          updateServoQueue = false;
        } else if (time_since_updating_servos > 15) {
          setServos();
          time_since_updating_servos = 0;
          updateServoQueue = false;
        }
      }
      Serial.println("updating servo position because no motion is detected");
    }

    if (update_loop_counter < update_weather_time - 1) {
      //Code below will only run every time the main loop runs if it is not an update loop.
      update_loop_counter++;
      Serial.println("minute loop hit");

    } else if (update_loop_counter >= update_weather_time - 1) {
      update_loop_counter = 0;
      //Pull new weather data
      if (!demo_mode) {
        if (!bedtime) {
          //if not bedtime then pull new weather data
          parse_json(getWeatherRequest());
          parse_forcast_json(getWeatherForcastRequest());
        }
        int curnt_min = getLocalTimeMin();
        if (curnt_min != -1) {
          current_min = curnt_min;
        }
      } else {
        //if demo mode is on then randomize the values
        main_temp = random(266, 305);
        main_humidity = random(0, 100);
        wind_speed = 0.44704 * random(0, 70);
        clouds_all = random(0, 100);
        rain_percent = random(0, 100);
      }

      Serial.println("update weather loop hit");

      if (isMotionDetected) {
        updateServoQueue = true;
        Serial.println("cannot update servos due to motion");
      } else {
        //set servos to position
        if (!bedtime) {
          setServos();
          time_since_updating_servos = 0;
        }
      }
    }
    
    //check and update lcd with display info
    if (!demo_mode) {
      if (!bedtime) {
        lcd.clear();

        float fTemp = 0;
        float cTemp = main_temp - 273.15;
        fTemp = (cTemp * 1.8) + 32;
        lcd.print("Temp: " + String(fTemp));
        lcd.setCursor(0, 1);
        lcd.print("Humidity: %" + String(main_humidity));

        //For debugging purposes cycle through different values
        /*
        if (lcd_display_counter >= 3) {
          lcd_display_counter = 0;
        }
        if (lcd_display_counter == 0) {
          lcd.print("Humidity: %" + String(main_humidity));
          lcd.setCursor(0, 1);
          //float windMph = wind_speed / 0.44704;
          lcd.print("time_since: " + String(time_since_updating_servos));
        } else if (lcd_display_counter == 1) {
          float windMph = wind_speed / 0.44704;
          lcd.print("Wind: " + String(windMph));
          lcd.setCursor(0, 1);
          lcd.print("time_since: " + String(time_since_updating_servos));
        } else if (lcd_display_counter == 2) {
          float fTemp = 0;
          float cTemp = main_temp - 273.15;
          fTemp = (cTemp * 1.8) + 32;
          lcd.print("temp: " + String(fTemp));
          lcd.setCursor(0, 1);
          lcd.print("time_since: " + String(time_since_updating_servos));
        }
        lcd_display_counter++;
        */
      }
    } else {
      if (!bedtime) {
        if (demo_old_time == 1) {
          lcd.clear();
          lcd.print("Humidity: %" + String(main_humidity));
           side_fade_trigger = 1;
          demo_old_time = 0;
        }
      } else if (demo_old_time == 2) {
        lcd.clear();
        lcd.print("Bedtime Mode");
        demo_old_time = 0;
      }
    }
  }

  //run fade loop
  if ((currentMillis - previousFadeMillis) >= fade_delay) {
    previousFadeMillis = currentMillis;

    if (side_fade_trigger == 0) {
      //ignore, 0 is idle state (NOTHING SHOULD SET SIDE_FADE_TRIGGER TO 0 EXCEPT THIS LOOP)
    } else if (side_fade_trigger == 1) {
      //fade lights in
      if (side_fade_count < side_pixels * 2) {
        if (side_fade_count < side_pixels) {
          //if (main_humidity != 0) {
            /*
            float contrast = map(side_fade_count, 0, side_pixels - 1, 0, 100);
            contrast = contrast * (float(main_humidity)/100.00);
            blue = map(contrast, 0, 100, 0, 150);
            green = map(contrast, 100, 0, 0, 150);

            //green = map(main_humidity, 0, 100, 
            green = constrain(green, 0, 150);
            blue = constrain(blue, 0, 150);
            */

            float humidity_scale_value = (float(side_pixels) / 100) * float(main_humidity);
            int humidity_decimal = (humidity_scale_value - int(humidity_scale_value)) * 100;

            int blue = 0;
            int green = 150;

            //if (humidity_scale_value < 1 && side_fade_count == 0) {
            //  blue = map(humidity_decimal, 0, 100, 0, 150);
            //  green = map(humidity_decimal, 0, 100, 150, 0);
            if (humidity_scale_value == 17 && side_fade_count == 16) {
              blue = 150;
              green = 0;
            } else if (humidity_scale_value - side_fade_count >= 1) {
              blue = 150;
              green = 0;
            } else if ((humidity_scale_value - i) < 1 && (humidity_scale_value - i) > 0) {
              blue = map(humidity_decimal, 0, 100, 0, 150);
              green = map(humidity_decimal, 0, 100, 150, 0);
            }

            /*
            float contrast = map(side_fade_count, 0, side_pixels - 1, 0, 100); //throughout the loop the value "contrast" will = 0 for the first pixel and 100 for the last pixel on the left side.
            // Side fade count starts at 0 and loops to 16 because there is 17 pixels. That is why side_pixels is -1.
            //float line = map(contrast, 0, 100, 0, main_humidity);
            //contrast = contrast * (float(main_humidity)/100.00);
            int blueN = map(0, 0, 100, 150, 0); //main_humidity
            int greenN = map(0, 0, 100, 0, 150);

            green = blueN * (contrast / 50);
            blue = greenN / (contrast / 50);

            blue = constrain(blue, 0, 150);
            green = constrain(green, 0, 150);
            */

          //} else {
          //  blue = 0;
          //  green = 150;
          //}
          pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(0, green, blue));
          pixels.show();
        } else {
          //precipitation_type = 3;
          if (precipitation_type == 0) {
            float contrast = map(side_fade_count - side_pixels, 0, side_pixels - 1, 0, 100);
            //contrast = contrast * (float(main_humidity)/100.00);
            float blue = map(contrast, 100, 0, 0, 150);
            float red = map(contrast, 0, 100, 0, 100);
            red = constrain(red, 0, 150);
            blue = constrain(blue, 0, 150);
            pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(red, 0, blue));
            pixels.show();
          } else if (precipitation_type == 1) {
            float contrast = map(side_fade_count - side_pixels, 0, side_pixels - 1, 0, 100);
            //contrast = contrast * (float(main_humidity)/100.00);
            float blue = map(contrast, 100, 0, 0, 150);
            //float red = map(contrast, 0, 100, 0, 100);
            //red = constrain(red, 0, 150);
            blue = constrain(blue, 0, 150);
            pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(0, 0, blue));
            pixels.show();
          } else if (precipitation_type == 2) {
            float contrast = map(side_fade_count - side_pixels, 0, side_pixels - 1, 0, 100);
            //contrast = contrast * (float(main_humidity)/100.00);
            float white = map(contrast, 100, 0, 0, 150);
            //float blue = map(contrast, 0, 100, 0, 150);
            //red = constrain(red, 0, 150);
            white = constrain(white, 0, 150);
            pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(white, white, white));
            pixels.show();
          } else if (precipitation_type == 3) {
            Serial.println("snow rain lights");
            float contrast = map(side_fade_count - side_pixels, 0, side_pixels - 1, 0, 100);
            //contrast = contrast * (float(main_humidity)/100.00);
            float white = map(contrast, 100, 0, 0, 60);
            float blue = map(contrast, 0, 100, 0, 180);
            float whiteR = white;
            float whiteG = white;
            float whiteB = white;
            if (whiteB <= blue) {
              whiteB = blue;
            }
            //red = constrain(red, 0, 150);
            //white = constrain(white, 0, 150);
            pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(whiteR, whiteG, whiteB));
            pixels.show();
          }
        }

        Serial.println(side_fade_count);
        side_fade_count++;
      } else {
        side_fade_count = (side_pixels * 2) - 1;
        side_fade_trigger = 0;
      }
    } else if (side_fade_trigger == 2) {
      //fade lights out
      if (side_fade_count >= 0) {
        pixels.setPixelColor((sun_pixels + (side_pixels * 2)) - side_fade_count - 1, pixels.Color(0, 0, 0));
        pixels.show();
        Serial.println(side_fade_count);
        side_fade_count--;
      } else {
        side_fade_count = 0;
        side_fade_trigger = 0;
      }
    }
  }

  if (((currentMillis - previousServoAttachMillis) >= servoAttachDelay) && servoAttachTrigger) {
    servoAttachTrigger = false;

    float fTemp = 0;
    float cTemp = main_temp - 273.15;
    fTemp = (cTemp * 1.8) + 32;
    //Serial.println(fTemp);

    //there are 0.44704 m/s in 1 mph
    float windMph = wind_speed / 0.44704;

    int pos3;
    int pos4;
    int pos1;
    int pos2;

    //Because the dial pointers are not at the center of the circle where the labels are, I just decided to map the values. Not a very good way to do it.

    if (fTemp <= 30) {
      pos2 = map(fTemp, 20, 30, 99, 86);
    } else if (fTemp > 30 && fTemp <= 40) {
      pos2 = map(fTemp, 30, 40, 86, 75);
    } else if (fTemp > 40 && fTemp <= 50) {
      pos2 = map(fTemp, 40, 50, 75, 63);
    } else if (fTemp > 50 && fTemp <= 60) {
      pos2 = map(fTemp, 50, 60, 63, 51);
    } else if (fTemp > 60 && fTemp <= 70) {
      pos2 = map(fTemp, 60, 70, 51, 40);
    } else if (fTemp > 70 && fTemp <= 80) {
      pos2 = map(fTemp, 70, 80, 40, 29);
    } else if (fTemp > 80 && fTemp <= 90) {
      pos2 = map(fTemp, 80, 90, 29, 18);
    }

    if (rain_percent <= 20) {
      pos3 = map(rain_percent, 0, 20, 19, 35);
    } else if (rain_percent > 20 && rain_percent <= 40) {
      pos3 = map(rain_percent, 20, 40, 35, 52);
    } else if (rain_percent > 40 && rain_percent <= 60) {
      pos3 = map(rain_percent, 40, 60, 52, 71);
    } else if (rain_percent > 60 && rain_percent <= 80) {
      pos3 = map(rain_percent, 60, 80, 71, 92);
    } else if (rain_percent > 80 && rain_percent <= 100) {
      pos3 = map(rain_percent, 80, 100, 92, 108);
    }

    if (windMph <= 10) {
      pos4 = map(windMph, 0, 10, 13, 23);  //windMph
    } else if (windMph > 10 && windMph <= 20) {
      pos4 = map(windMph, 10, 20, 23, 34);  //windMph
    } else if (windMph > 20 && windMph <= 30) {
      pos4 = map(windMph, 20, 30, 34, 47);  //windMph
    } else if (windMph > 30 && windMph <= 40) {
      pos4 = map(windMph, 30, 40, 47, 60);  //windMph
    } else if (windMph > 40 && windMph <= 50) {
      pos4 = map(windMph, 40, 50, 60, 74);  //windMph
    } else if (windMph > 50 && windMph <= 60) {
      pos4 = map(windMph, 50, 60, 74, 86);  //windMph
    } else if (windMph > 60 && windMph <= 70) {
      pos4 = map(windMph, 60, 70, 86, 97);  //windMph
    }

    if (clouds_all <= 20) {
      pos1 = map(clouds_all, 0, 20, 96, 85);
    } else if (clouds_all > 20 && clouds_all <= 40) {
      pos1 = map(clouds_all, 20, 40, 85, 69);
    } else if (clouds_all > 40 && clouds_all <= 60) {
      pos1 = map(clouds_all, 40, 60, 69, 52);
    } else if (clouds_all > 60 && clouds_all <= 80) {
      pos1 = map(clouds_all, 60, 80, 52, 36);
    } else if (clouds_all > 80 && clouds_all <= 100) {
      pos1 = map(clouds_all, 80, 100, 34, 18);
    }
    
    pos1 = constrain(pos1, 18, 98);
    pos2 = constrain(pos2, 18, 99);
    pos3 = constrain(pos3, 21, 108);
    pos4 = constrain(pos4, 13, 97);
    servo_1.write(pos1);
    servo_2.write(pos2);
    servo_3.write(pos3);
    servo_4.write(pos4);

    servoWriteTrigger = true;
    previousServoWriteMillis = currentMillis;
  }

  if (((currentMillis - previousServoWriteMillis) >= servoWriteDelay) && servoWriteTrigger) {
    servoWriteTrigger = false;
    servo_1.detach();
    servo_2.detach();
    servo_3.detach();
    servo_4.detach();
  }
}

void setServos() {
  if (!servo_1.attached()) {
    servo_1.attach(servo_1_pin);
  }
  if (!servo_2.attached()) {
    servo_2.attach(servo_2_pin);
  }
  if (!servo_3.attached()) {
    servo_3.attach(servo_3_pin);
  }
  if (!servo_4.attached()) {
    servo_4.attach(servo_4_pin);
  }
  servoAttachTrigger = true;
  previousServoAttachMillis = millis();

}

String getWeatherRequest() {
  if ((WiFi.status() == WL_CONNECTED)) {  //Check the current connection status

    HTTPClient http;

    http.begin(endpoint + key);  //Specify the URL
    int httpCode = http.GET();   //Make the request

    if (httpCode > 0) {  //Check for the returning code

      String payload = http.getString();
      return payload;
    }

    else {
      Serial.println("Error on HTTP request");
    }
    http.end();  //Free the resources
  }
}

String getWeatherForcastRequest() {
  if ((WiFi.status() == WL_CONNECTED)) {  //Check the current connection status

    HTTPClient http;

    http.begin(endpointForcast + key);  //Specify the URL
    int httpCode = http.GET();          //Make the request

    if (httpCode > 0) {  //Check for the returning code

      String payload = http.getString();
      return payload;
    }

    else {
      Serial.println("Error on HTTP request");
    }
    http.end();  //Free the resources
  }
}

void parse_json(String input) {
  StaticJsonDocument<1024> doc;

  DeserializationError error = deserializeJson(doc, input);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject main = doc["main"];
  main_temp = main["temp"];  // 294.62
  //Serial.println(main_temp);
  main_pressure = main["pressure"];  // 1020
  main_humidity = main["humidity"];  // 81

  visibility = doc["visibility"];  // 10000

  JsonObject wind = doc["wind"];
  wind_speed = wind["speed"];  // 4.63
  wind_gust = wind["gust"];    // 7.72

  if (doc.containsKey("rain")) {
    Serial.println("rain");
    precipitation_type = 1;
  } else {
    precipitation_type = 0;
  }
  if (doc.containsKey("snow")) {
    Serial.println("snow");
    if (precipitation_type == 1) {
      precipitation_type = 3;
    } else {
      precipitation_type = 2;
    }
  } else {
    precipitation_type = 0;
  }

  clouds_all = doc["clouds"]["all"];  // 100

  long dt = doc["dt"]; 

  JsonObject sys = doc["sys"];
  sys_sunrise = sys["sunrise"];  
  sys_sunset = sys["sunset"];    
}

void parse_forcast_json(String input) {
  StaticJsonDocument<1536> doc;

  DeserializationError error = deserializeJson(doc, input);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject list_0 = doc["list"][0];

  //JsonObject list_0_main = list_0["main"];

  //JsonObject list_0_weather_0 = list_0["weather"][0];

  rain_percent = list_0["pop"];  // 0
  rain_percent = rain_percent * 100;

  //const char* list_0_sys_pod = list_0["sys"]["pod"]; // "d"

  forcast_time = list_0["dt_txt"];  // "2022-11-13 21:00:00"
}

void calculate_sunrise_sunset_math(long sunrise, long sunset, int current_min) {
  /*
  stamp.getDateTime(1667771985);
  Serial.println(stamp.year);
  Serial.println(stamp.month);
  Serial.println(stamp.day);
  Serial.println(stamp.hour);
  Serial.println(stamp.minute);
  Serial.println(stamp.second);
  Serial.println(stamp.dayOfWeek);
  */

  stamp.getDateTime(sunrise);
  int startMinute = stamp.minute + (stamp.hour * 60);
  //Serial.println(startMinute);

  stamp.getDateTime(sunset);
  int endMinute = stamp.minute + (stamp.hour * 60);
  //Serial.print("minute: ");
  //Serial.println(stamp.minute);
  //Serial.print("hour: ");
  //Serial.println(stamp.hour);
  //Serial.println(endMinute);
  if (current_min >= endMinute + dusk_time) {
    Serial.println("Night Time, sun is dark");
    for (int i = 0; i < sun_pixels; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 0));
    }
    pixels.show();
  } else if (current_min <= startMinute - dusk_time) {
    Serial.println("Morning dark, sun hasent risen yet");
    for (int i = 0; i < sun_pixels; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 0));
    }
    pixels.show();
  } else if (current_min <= startMinute && current_min > startMinute - dusk_time) {
    Serial.println("morning dusk time");
    int brightness = map(current_min, startMinute - dusk_time, startMinute, 0, 100);
    int red = map(brightness, 0, 100, 0, 250);
    int green = map(brightness, 50, 100, 0, 150);
    green = constrain(green, 0, 255);
    pixels.setPixelColor(0, pixels.Color(red, green, 0));
    pixels.show();
  } else if (current_min > endMinute && current_min < endMinute + dusk_time) {
    Serial.println("night dusk time");
    int brightness = map(current_min, endMinute, endMinute + dusk_time, 100, 0);
    int red = map(brightness, 100, 0, 250, 0);
    int green = map(brightness, 100, 35, 150, 10);
    green = constrain(green, 0, 255);
    Serial.println(red);
    Serial.println(green);
    pixels.setPixelColor(sun_pixels - 1, pixels.Color(red, green, 0));
    pixels.show();
  } else {
    int light_output = map(current_min, startMinute, endMinute, 0, sun_pixels - 1);
    Serial.print("sun light position: ");
    Serial.println(light_output);
    pixels.setPixelColor(light_output, pixels.Color(250, 150, 0));
    pixels.setPixelColor(light_output - 1, pixels.Color(0, 0, 0));
    pixels.show();
  }
}

int getLocalTimeMin() {
  if ((WiFi.status() == WL_CONNECTED)) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      //return 0;
      return -1;
    }
    int minutes = timeinfo.tm_min + (timeinfo.tm_hour * 60);
    Serial.print("Current Minutes: ");
    Serial.println(minutes);
    return minutes;
  } else {
    return -1;
  }
}
