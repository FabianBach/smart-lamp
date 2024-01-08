#include "Tuyav.h"
#include "global.h"

//selection of Serial port
#if defined(ARDUINO_AVR_UNO)    //Arduino UNO board: use SoftwareSerial with pins you select, see https://www.arduino.cc/en/Reference/softwareSerial
SoftwareSerial mySWserial(2,3); //RX,TX (2 and 3 are recommended)
Tuyav tuyav(&mySWserial);         
#else                           //Arduino Mega board: User can choose HardwareSerial: Serial1/Serial2/Serial3
  Tuyav tuyav(&Serial1);        //Serial1 is pin 18/19 on a Arduino Mega or pin TX1/RX0 on a Arduino Nano Every
#endif

// To connect the tuya board to your gateway, press and hold to button on the board for about 5 seconds. The blue light should then flash.
// In the app, do not just use the automatic search, but enter the extended "add new device" dialogue and add something like "other" or whatever. It will then find the Tya module.

bool connected = false;

#define PWM_READ_PIN_WW 2
#define PWM_READ_PIN_CW 3

int pwmWW_value = 0;
int pwmCW_value = 0;

int pwmTemperature = -1;
int pwmBrightness = -1;

int pwmTemperatureAverage = -10;
int pwmBrightnessAverage = -10;

bool manualBrightness = false;
bool manualTemperature = false;

// loop update restrictions
unsigned long lastUpdateTimestamp = 0;
unsigned long updateCycleDuration = 1000;    //3 seconds by default. Min 1 second or you will overflow the serial communication!
unsigned long lastLEDUpdate = 0;
unsigned long lastLogTimestamp = 0;

// Init LED values
int led_minBrightness = 50;
int led_brightness = 0; // 0 == off; 255 == full
int led_targetBrightness = 100; // 0 == off; 255 == full

int led_temperature = 0; // 0 == warm; 128 == neutral; 255 = cold;
int led_targetTemperature = 0;

// Init sun-position values
char * sunSet; // will be like "2023-12-23 16:19:36"
char * sunRise; // will be like "2023-12-23 08:10:27"

int sunSet_hour = -1;
int sunSet_minute = -1;
int sunRise_hour = -1;
int sunRise_minute = -1;
int currentTime_hour = -1;
int currentTime_minute = -1;

int currentTimeInMinutes = -1;
int sunRiseTimeInMinutes = -1;
int sunSetTimeInMinutes = -1;

int sunriseDuration = 60;
int sunsetDuration = 60;

void setup(){
  pinMode(PWM_READ_PIN_WW, INPUT_PULLUP);
  pinMode(PWM_READ_PIN_CW, INPUT_PULLUP);

  // try to keep dark during startup
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  analogWrite(5, 0);
  analogWrite(6, 0);

  initTuya();

  //start serial for debugging
  Serial.begin(500000);
  Serial.println("Starting Tuya CCT Dimmer");
}

void loop() {
  pwmCW_value = readHighPulseDuration(PWM_READ_PIN_CW);
  pwmWW_value = readHighPulseDuration(PWM_READ_PIN_WW);

  // Serial.print(-50); // just as a stable point of reference
  // Serial.print(" ");
  // Serial.print(300); // just as a stable point of reference
  // Serial.print(" ");
  // Serial.print(pwmCW_value);
  // Serial.print(" ");
  // Serial.println(pwmWW_value);

  //Should be called continuously 
  tuyav.tuyaUpdate();
  connectTuya();
  checkSunPosition();

  if (connected) {
    setLightMode();
    calculatePWMValues();
  }

  setLEDValues();
  updateLEDs();

  logDebounced();
}

void setLightMode() {
  // if lamp-hack is turned of, use automatic mode
  if (pwmWW_value == 0 && pwmCW_value == 0) {
    if (manualBrightness || manualTemperature)
      Serial.println("Setting from manual to automatic mode.");

    manualBrightness = false;
    manualTemperature = false;
  }
}

void calculatePWMValues() {
  bool isTurnedOn = !((pwmCW_value == 0) && (pwmWW_value == 0));

  if (!isTurnedOn) {
    if (pwmBrightnessAverage > -1 && pwmTemperatureAverage > -1) { Serial.println("Not turned on, resetting averages"); }
    pwmBrightnessAverage = -10;
    pwmTemperatureAverage = -10;
    return;
  }

  // coming from -10 after getting warm reading the values
  if (pwmBrightnessAverage < -1 && pwmTemperatureAverage < -1) {
    pwmTemperatureAverage++;
    pwmBrightnessAverage++;
    return;
  }

  // NEW BRIGHTNESS
  // each value maxes out at around 255, but never at the same time, they sum always stays below 255.
  int newBrightness = pwmWW_value + pwmCW_value;
  if (newBrightness > 255) {
    newBrightness = 255; // just in case.
  }

  // NEW TEMPERATURE
  // coldWhite < warmWhite => 0
  // coldWhite == warmWhite => 128
  // coldWhite > warmWhite => 255
  // The cold-white channel simply behaves like the temperature in this programm, so should be fine to just take that as reference
  // if the brightness is low, we read a low pwm value and we have to compensate for that 
  int newTemperature = ((pwmCW_value*255L) / newBrightness); // THE L AFTER 255 IS NEEDED TO CAST IT AS "LONG", SO THE COMPILER DOES NOT CAST 255 TO A SMALLER TYPE

  // initially set the values if we are just turning on to not trigger a switch to manual
  if (pwmBrightnessAverage == -1) {
    pwmBrightnessAverage = newBrightness;
    pwmBrightness = newBrightness;
  } 
  if (pwmTemperatureAverage == -1) {
    pwmTemperatureAverage = newTemperature;
    pwmTemperature = newTemperature;
  }

  // ignore small changes by making an average
  pwmBrightnessAverage = (newBrightness + (pwmBrightnessAverage*4L)) / 5;
  pwmTemperatureAverage = (newTemperature + (pwmTemperatureAverage*4L)) / 5;

  // Serial.print(-50); // just as a stable point of reference
  // Serial.print(" ");
  // Serial.print(300); // just as a stable point of reference
  // Serial.print(" ");

  // Serial.print(pwmWW_value);
  // Serial.print(" ");
  // Serial.print(pwmCW_value);
  // Serial.print(" ");
  
  // Serial.print(newBrightness);
  // Serial.print(" ");
  // Serial.print(pwmBrightnessAverage);
  // Serial.print(" ");
  // Serial.print(pwmBrightness);
  
  // Serial.print(" ");
  // Serial.print(newTemperature);
  // Serial.print(" ");
  // Serial.print(pwmTemperatureAverage);
  // Serial.print(" ");
  // Serial.print(pwmTemperature);

  // Serial.println();

  int diffBrightness = pwmBrightnessAverage - pwmBrightness;
  if (abs(diffBrightness) > 10 && isTurnedOn) {
    manualBrightness = true;
    pwmBrightness = pwmBrightnessAverage;
    Serial.print("Manual brightness set. Diff: \t");
    Serial.println(abs(diffBrightness));
  }

  int diffTemperature = pwmTemperatureAverage - pwmTemperature;
  if (abs(diffTemperature) > 10 && isTurnedOn) {
    manualTemperature = true;
    pwmTemperature = pwmTemperatureAverage;
    Serial.print("Manual temperature set. Diff: \t");
    Serial.println(abs(diffTemperature));
  }
}

void setLEDValues() {
  bool hasTime = (currentTimeInMinutes != -1);
  bool hasWeather = (sunRiseTimeInMinutes != -1) && (sunSetTimeInMinutes != -1);

  if (hasTime && hasWeather) {
    // Use automatic temperature setting

    // start before sunrise
    int timeToSunrise = (sunRiseTimeInMinutes - sunriseDuration) - currentTimeInMinutes;
    // start at sunrise
    int timeToDaytime = sunRiseTimeInMinutes - currentTimeInMinutes;
    // start at sunset (not before)
    int timeToSunset = sunSetTimeInMinutes - currentTimeInMinutes;
    // start after sunset is finished
    int tomeToNight = (sunSetTimeInMinutes + sunsetDuration) - currentTimeInMinutes;

    // MIDNIGHT to BEFORE SUNRISE
    bool earlyMorning = timeToSunrise > 0;
    // BEFORE SUNRISE to SUNRISE
    bool sunrise = timeToSunrise <= 0 && timeToDaytime > 0;
    // DAYTIME (SUNRISE TO SUNSET)
    bool daytime = timeToDaytime <= 0 && timeToSunset > 0;
    // SUNSET to AFTER SUNSET
    bool sunset = timeToSunset <= 0 && tomeToNight > 0;
    // AFTER SUNSET TO MIDNIGHT
    bool nighttime = tomeToNight <= 0;

    if (earlyMorning){
      // Serial.println("Early morning.");
      led_targetTemperature = 0; // warm
    }

    if (sunrise) {
      // Serial.print("Sunrise."); Serial.print(timeToSunrise); Serial.println(sunriseProgress);

      float sunriseProgress = ((currentTimeInMinutes - sunRiseTimeInMinutes)/sunriseDuration);
      if (sunriseProgress > 1){ sunriseProgress = 1; }

      // set new target temperature
      int sunriseTemperature = 255.0 * sunriseProgress;
      led_targetTemperature = (sunriseTemperature > led_targetTemperature) ? sunriseTemperature : led_targetTemperature;

      int sunriseBrightness = 255.0 * sunriseProgress; 
      led_targetBrightness = (sunriseBrightness > led_targetBrightness) ? sunriseBrightness : led_targetBrightness;
    }

    if (daytime) {
      // Serial.println("Daytime.");
      led_targetTemperature = 255; // cold
    }
    
    if (sunset) {
      // Serial.print("Sunset."); Serial.print(timeToSunset); Serial.println(sunsetProgress);
      float sunsetProgress = (currentTimeInMinutes+sunsetDuration - sunSetTimeInMinutes)/sunsetDuration;
      if (sunsetProgress > 1){ sunsetProgress = 1; }
      // set new target temperature
      int sunsetTemperature = 255 * (1 - sunsetProgress);
      led_targetTemperature = (sunsetTemperature < led_targetTemperature) ? sunsetTemperature : led_targetTemperature;

      int sunsetBrightness = 255 * (1 - sunsetProgress); 
      led_targetBrightness = (sunsetBrightness < led_targetBrightness) ? sunsetBrightness : led_targetBrightness;
    }

    if (nighttime){
      // Serial.println("Nighttime.");
      led_targetTemperature = 0; // warm
    }
  }

  if (manualBrightness) {
    led_targetBrightness = pwmBrightness;
  }

  if (manualTemperature) {
    led_targetTemperature = pwmTemperature;
  }

  // never turn off completely, to do that cut the power instead...
  if (led_targetBrightness < led_minBrightness) { 
    led_targetBrightness = led_minBrightness; 
  }
}

void updateLEDs(){
  bool dirty = false;
  
  // change vaule by 1 every x ms
  if (millis() > lastLEDUpdate + 10){
    lastLEDUpdate = millis();
    
    if (led_brightness < led_targetBrightness) {
      led_brightness++;
      dirty = true;
    }
    if (led_brightness > led_targetBrightness) {
      led_brightness--;
      dirty = true;
    }
    if (led_temperature < led_targetTemperature) {
      led_temperature++;
      dirty = true;
    }
    if (led_temperature > led_targetTemperature) {
      led_temperature--;
      dirty = true;
    }
  }

  if (dirty) {
    // set correct mix of cold and warm LEDs
    float led_coldValue = led_temperature < 128 ? (led_temperature*2) : 255;
    float led_warmValue = led_temperature > 128 ? ((255-led_temperature)*2) : 255;

    // apply bightness to LED-mix
    led_coldValue = (led_brightness/255.0) * led_coldValue;
    led_warmValue = (led_brightness/255.0) * led_warmValue;

    // analogWrite(A0, led_coldValue);
    analogWrite(5, led_warmValue);
    analogWrite(6, led_coldValue);
  }
}

void checkSunPosition(){
  if (tuyav.WeatherReceived() && sunRiseTimeInMinutes == -1 && sunSetTimeInMinutes == -1) { //Check if new weather info is received, On startup and after that a 30min interval
    //To force the Tuya module to send the weather immediately power cycle the Tuya module
    Serial.println(F("Weather received from Tuya module: "));

    /*
      Available Weather Info
      char* SunSet
      char* SunRise
      ... and more, see weather example.
    */
    weather_info weather = tuyav.getWeatherInfo();
    sunRise = weather.SunRise;
    sunSet = weather.SunSet;

    // something like "2023-12-23 08:10:27"
    // we want hour:minute ...[12][13]:[15][16]...

    // to get the number (0-9) from a character subtract '0' (or 48 (dec) or 0x30 (hex))
    sunRise_hour = ((weather.SunRise[11]-'0') * 10) + (weather.SunRise[12]-'0');
    sunRise_minute = ((weather.SunRise[14]-'0') * 10) + (weather.SunRise[15]-'0');
    sunRiseTimeInMinutes = (sunRise_hour * 60) + sunRise_minute;
    // Serial.print("Sunrise at: "); Serial.print(sunRise_hour); Serial.println(sunRise_minute);
    sunSet_hour = ((weather.SunSet[11]-'0') * 10) + (weather.SunSet[12]-'0');
    sunSet_minute = ((weather.SunSet[14]-'0') * 10) + (weather.SunSet[15]-'0');
    sunSetTimeInMinutes = (sunSet_hour * 60) + sunSet_minute;
    // Serial.print("Sunset at: "); Serial.print(sunSet_hour); Serial.println(sunSet_minute);

    char printBuffer[100];
    sprintf(printBuffer,"The Sun will rise at %i (%s)", sunRiseTimeInMinutes, weather.SunRise);
    Serial.println(printBuffer);
    sprintf(printBuffer,"The Sun will set at %i (%s)", sunSetTimeInMinutes, weather.SunSet);
    Serial.println(printBuffer);
    Serial.print("Current time: "); Serial.println(currentTimeInMinutes);
    Serial.println("- - - - - - - - - - \n");

    tuyav.setWeatherReceived(false); // Reset flag if we want new weather info
  }

  if (tuyav.newTime[0]) { // we are certain that the time info has been received (value of tuyav.newTime[0] is the update flag)
    currentTime_hour = tuyav.newTime[4];    // 0 - 23
    currentTime_minute = tuyav.newTime[5];  // 0 - 59
    currentTimeInMinutes = (currentTime_hour * 60) + currentTime_minute;

    // Serial.print(F("Time received from Tuya module: "));
    // Serial.print(currentTime_hour); Serial.print(":"); Serial.println(currentTime_minute);
    // Serial.println("- - - - - - - - - - \n");

    tuyav.newTime[0] = 0; // reset update flag
  }
}

int readHighPulseDuration(byte pin) {
  unsigned long highTime = pulseIn(pin, HIGH, 2000);  // 2000 micros timeout, I measured a total pulse lenght (high to high) of 1000

  if (highTime == 0)
    highTime = digitalRead(pin) ? 1000 : 0;  // HIGH == 100%,  LOW = 0%

  if (highTime > 1000)
    highTime = 1000;

  return (255 * highTime)/1000; // map to range of 0 - 255
}

void initTuya(){
  //if ArduinoMega or ArduinoNanoEvery, start Serial1
  #if defined(ARDUINO_AVR_UNO)
  #else 
    Serial1.begin(9600);
  #endif
  
  // Define the TUYA pins
  // There are 3 digital inputs, 3 analog inputs, 5 digital output and 3 analog outputs available
  // If you do not use a pin, set the pin as PIN_UNUSED
  tuyav.setDigitalInputs(PIN_UNUSED, PIN_UNUSED, PIN_UNUSED);                    //Set DigitalInputs
  tuyav.setAnalogInputs(PIN_UNUSED, PIN_UNUSED, PIN_UNUSED);                  //Set ManualInputs
  tuyav.setDigitalOutputs(PIN_UNUSED, PIN_UNUSED, PIN_UNUSED, PIN_UNUSED, PIN_UNUSED);  //SetDigitalOutputs
  tuyav.setAnalogOutputs(PIN_UNUSED, PIN_UNUSED, PIN_UNUSED);                  //Set AnalogOutputs (PWM digital pins)
  // tuyav.setUpdateRateMs(2000) // does not allow anything below 2000

  //init the chip
  tuyav.initialize();
}

void connectTuya() {
  if (tuyav.getNetworkStatus() == 4 && !connected) {
    connected = true;
    Serial.println("CONNECTED");
    Serial.println("Activating the weather service");
    tuyav.startWeather();
    tuyav.getTime();      //request update of global tuyav.newTime[] vars
  }

  // call some things just once in a while
  if (connected && (millis() - lastUpdateTimestamp > updateCycleDuration)) {
    tuyav.getTime();      //request update of global tuyav.newTime[] vars
    lastUpdateTimestamp = millis();
  }
}

void logDebounced(){
  if (millis() - lastLogTimestamp > 500) {
  
    if (led_brightness !=led_targetBrightness || led_temperature != led_targetTemperature ) {
      lastLogTimestamp = millis();

      char printBuffer[100];
      sprintf(printBuffer,"Current brightness: %i (Target: %i)", led_brightness, led_targetBrightness);
      Serial.println(printBuffer);
      sprintf(printBuffer,"Current temperature: %i (Target: %i)", led_temperature, led_targetTemperature);
      Serial.println(printBuffer);
      // Serial.print("Cold: "); Serial.print(led_coldValue);Serial.print(", Warm: "); Serial.println(led_warmValue);
      Serial.println("- - -");
    }
  }
}
