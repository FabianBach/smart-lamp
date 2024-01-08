#include "Tuyav.h"
#include "global.h"

//selection of Serial port
#if defined(ARDUINO_AVR_UNO)    //Arduino UNO board: use SoftwareSerial with pins you select, see https://www.arduino.cc/en/Reference/softwareSerial
SoftwareSerial mySWserial(2,3); //RX,TX (2 and 3 are recommended)
Tuyav tuyav(&mySWserial);         
#else                           //Arduino Mega board: User can choose HardwareSerial: Serial1/Serial2/Serial3
  Tuyav tuyav(&Serial1);        //Serial1 is pin 18/19 on a Arduino Mega or pin TX1/RX0 on a Arduino Nano Every
#endif

bool connected = false;

// loop update restiriction
unsigned long lastUpdateTimestamp = 0;
unsigned long updateCycleDuration = 1000;    //3 seconds by default. Min 1 second or you will overflow the serial communication!
unsigned long lastLEDUpdate = 0;
unsigned long lastLogTimestamp = 0;

// Init LED values
int led_minBrightness = 50;
int led_brightness = 0; // 0 == off; 255 == full
int led_targetBrightness = 50; // 0 == off; 255 == full
int led_temperature = 0; // 0 == warm; 128 == neutral; 255 = cold;
int led_targetTemperature = 0;
int led_stepSize = 255/5; // Number of steps

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

// Keep track of changes
enum lightMode {MODE_AUTOMATIC, MODE_MANUAL};
int currentLightMode = MODE_AUTOMATIC;
unsigned long lightModeResetTimeout = 1800000; // 30 mins in millis - (1000 * 60 * 30) did not work

enum inputMode {MODE_BRIGHTNESS, MODE_TEMPERATURE};
int currentInputMode = MODE_BRIGHTNESS;
unsigned long inputModeResetTimeout = 180000; // 3 mins in millis - (1000 * 60 * 3) did not work

int lastSwitchValue = 0;
unsigned long lastSwitchInputTime = 0;

int lastDimmerValue = 0;
unsigned long lastDimmerInputTime = 0;

void setup() 
{
  // keep dark during startup
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  analogWrite(5, 0);
  analogWrite(6, 0);

  //start serial for debugging
  Serial.begin(9600);
  Serial.println("Starting Tuya CCT Dimmer");

  //if ArduinoMega or ArduinoNanoEvery, start Serial1
  #if defined(ARDUINO_AVR_UNO)
  #else 
    Serial1.begin(9600);
  #endif
  
  //define the TUYA pins
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

void loop() {

  //Should be called continuously 
  tuyav.tuyaUpdate();

  if (tuyav.getNetworkStatus() == 4 && !connected) {
    connected = true;
    Serial.println("CONNECTED");
    Serial.println("Activating the weather service");
    tuyav.startWeather();
    tuyav.getTime();      //request update of global tuyav.newTime[] vars
    lastDimmerValue = tuyav.ANALOG_OUT[1];
    lastSwitchValue = tuyav.DIGITAL_OUT[0];
  }

  if ((millis() > lastSwitchInputTime + inputModeResetTimeout) 
        && (currentInputMode != MODE_BRIGHTNESS)) {
    currentInputMode = MODE_BRIGHTNESS; // reset dimmer mode
    Serial.println("Dimmer mode reset to brightness.");
  }

  if (connected && currentLightMode == MODE_AUTOMATIC) {
    setAutomaticTemperature();
  }

  // call some things just once in a while
  if (millis() - lastUpdateTimestamp > updateCycleDuration) {
    tuyav.getTime();      //request update of global tuyav.newTime[] vars
  }

  checkSunPosition();

  checkTuyaInputs();

  // never turn off completely, cut power instead...
  if (led_targetBrightness < led_minBrightness) { 
    led_targetBrightness = led_minBrightness; 
  }

  updateLEDs();

  logDebounced();
}

void checkTuyaInputs(){
  int dimmerValue = tuyav.ANALOG_OUT[1];
  int switchValue = tuyav.DIGITAL_OUT[0];

  if (connected && (switchValue != lastSwitchValue)) {
    lastSwitchInputTime = millis();
    currentInputMode = (currentInputMode == MODE_TEMPERATURE) ? MODE_BRIGHTNESS : MODE_TEMPERATURE;
    lastSwitchValue = switchValue;
    Serial.print("Dimmer mode changed to: "); Serial.println((currentInputMode == MODE_TEMPERATURE) ? "Temperature" : "Brightness");
  }

  if (connected && (dimmerValue != lastDimmerValue)) {
    lastDimmerInputTime = millis();

    Serial.println("Dimmer input");
    lastDimmerValue = dimmerValue;

    if (dimmerValue == 254 || dimmerValue == 255 ) {
      // increase
      switch(currentInputMode){
        case MODE_BRIGHTNESS:
          led_targetBrightness = increase(led_targetBrightness);
          break;
        case MODE_TEMPERATURE:
          led_targetTemperature = increase(led_targetTemperature);
      }
    }

    if (dimmerValue == 0 || dimmerValue == 1 ) {
      // decrease
      switch(currentInputMode){
        case MODE_BRIGHTNESS:
          led_targetBrightness = decrease(led_targetBrightness);
          break;
        case MODE_TEMPERATURE:
          led_targetTemperature = decrease(led_targetTemperature);
      }
    }
  }

  bool timeoutResolved = (millis() > lastDimmerInputTime + lightModeResetTimeout) || lastDimmerInputTime == 0;
  currentLightMode = timeoutResolved ? MODE_AUTOMATIC : MODE_MANUAL;
}

void setAutomaticTemperature() {
  bool hasTime = (currentTimeInMinutes != -1);
  bool hasWeather = (sunRiseTimeInMinutes != -1) && (sunSetTimeInMinutes != -1);

  if (hasTime && hasWeather) {
    // Use automatic temperature setting

    // start before sun has risen
    int timeToSunrise = (sunRiseTimeInMinutes - sunriseDuration) - currentTimeInMinutes;
    
    // MIDNIGHT TO X before SUNRISE
    if (timeToSunrise > 0){
      // Serial.println("MIDNIGHT TO SUNRISE");
      led_targetTemperature = 0; // warm
      led_targetBrightness = 100;
    }

    // X before SUNRISE AND DAYTIME
    if (timeToSunrise <= 0) {

      // sunrisetime == 100;
      // startSunriseAt == sunristetime - duration
      // 
      float sunriseProgress = ((currentTimeInMinutes - sunRiseTimeInMinutes)/sunriseDuration);
      if (sunriseProgress > 1){ sunriseProgress = 1; }
      // set new target temperature
      led_targetTemperature = 255 * sunriseProgress;
      led_targetBrightness = 255 * sunriseProgress;

      // Serial.print("SUNRISE AND DAYTIME"); Serial.print(timeToSunrise); Serial.println(sunriseProgress);
    }

    // start when sun is setting, not before
    int timeToSunset = sunSetTimeInMinutes - currentTimeInMinutes;
    
    // SUNSET TO MIDNIGHT
    if (timeToSunset < 0) {
      float sunsetProgress = (currentTimeInMinutes+sunsetDuration - sunSetTimeInMinutes)/sunsetDuration;
      if (sunsetProgress > 1){ sunsetProgress = 1; }
      // set new target temperature
      led_targetTemperature = 255 * (1 - sunsetProgress);
      led_targetBrightness = 255 * (1 - sunsetProgress);
      // Serial.print("SUNSET TO MIDNIGHT"); Serial.print(timeToSunset); Serial.println(sunsetProgress);
    }
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

int increase(int currentValue){
  currentValue += led_stepSize;
  if (currentValue > 255) currentValue = 255;
  return currentValue;
}

int decrease(int currentValue){
  currentValue -= led_stepSize;
  if (currentValue < 0) currentValue = 0;
  return currentValue;
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
