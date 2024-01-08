#define PWM_PIN_1 2
#define PWM_PIN_2 3

int pwm1_value = 0;
volatile unsigned long pwm1_lastHighPulseTime = 0;
volatile unsigned long pwm1_lastLastHighPulseTime = 0;
volatile unsigned long pwm1_lastLowPulseTime = 0;

int pwm2_value = 0;
volatile unsigned long pwm2_lastHighPulseTime = 0;
volatile unsigned long pwm2_lastLastHighPulseTime = 0;
volatile unsigned long pwm2_lastLowPulseTime = 0;

int totalPulseDuration = 1000; // we measured this value of 1000 micros
int totalPulseDurationMin = 990;
int totalPulseDurationMax = 1010;

int pwm1_highPulseDurationAverage = 0; // will be in range of 0 to 1000
int pwm2_highPulseDurationAverage = 0; // will be in range of 0 to 1000

int overflow = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PWM_PIN_1, INPUT_PULLUP);
  pinMode(PWM_PIN_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PWM_PIN_1), pwm1_pulseTimer, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PWM_PIN_2), pwm2_pulseTimer, CHANGE);
}


// STARTUP:
// Wait some time till accepting manual input. Just make sure to let everything connect and read and so on...


void loop() {
  unsigned long currentTime = micros();

  bool hasOverflow = pwm1_lastHighPulseTime > currentTime 
    || pwm1_lastLastHighPulseTime > currentTime 
    || pwm2_lastHighPulseTime > currentTime
    || pwm2_lastLastHighPulseTime > currentTime;

  if (!hasOverflow) {
    pwm1_highPulseDurationAverage = calculateLatestAverage(pwm1_lastHighPulseTime, pwm1_lastLastHighPulseTime, pwm1_lastLowPulseTime, pwm1_highPulseDurationAverage);
    pwm1_value = getByteFromPWM(pwm1_highPulseDurationAverage);

    pwm2_highPulseDurationAverage = calculateLatestAverage(pwm2_lastHighPulseTime, pwm2_lastLastHighPulseTime, pwm2_lastLowPulseTime, pwm2_highPulseDurationAverage);
    pwm2_value = getByteFromPWM(pwm2_highPulseDurationAverage);
  }

  Serial.print(0); // just as a stable point of reference
  Serial.print(" ");
  Serial.print(1200); // just as a stable point of reference
  Serial.print(" ");
  // Serial.print(pwm1_latestDuration);
  // Serial.print(" ");
  // Serial.print(pwm2_latestDuration);
  // Serial.print(" ");
  Serial.print(pwm1_highPulseDurationAverage);
  Serial.print(" ");
  Serial.println(pwm2_highPulseDurationAverage);

  // Serial.print(pwm1_value);
  // Serial.print(" ");
  // Serial.println(pwm2_value);
}

int calculateLatestAverage(unsigned long lastHighPulseTime, unsigned long lastLastHighPulseTime, unsigned long lastLowPulseTime, int highPulseDurationAverage){
  unsigned long currentTime = micros();
  int latestDuration = 0;

  // TODO: what if we miss a interrupt?

  if (lastHighPulseTime > lastLowPulseTime){
    // |  __________               ______
    // | |          |             |
    // |_|__________|_____________|______
    // We can measure the LAST COMLETE HIGH PULSE DURATION before the LOW GAP
    // We can measure the LAST LOW GAP DURATION
    // We can measure the CURRENT HIGH DURATION
    // We can measure the LAST COMPLETE PULSE DURATION (high to high)

    int lastCompleteHighPulseDuration = lastLowPulseTime - lastLastHighPulseTime; 
    int lastLowGapDuration = lastHighPulseTime - lastLowPulseTime;
    int currentHighDuration = currentTime - lastHighPulseTime;
    int latestPulseDuration = lastHighPulseTime - lastLastHighPulseTime;

    bool isOutOfTime = (latestPulseDuration < totalPulseDurationMin) || (latestPulseDuration > totalPulseDurationMax);

    if (currentHighDuration > totalPulseDurationMax){
      // low pulse missing or to short to measure, current high pulse is constant
      latestDuration = totalPulseDuration;
    }

    else if (lastLowGapDuration > totalPulseDurationMax){
      // if current high is really short, me have a constant low 
    }

    return (((highPulseDurationAverage*9) + latestDuration)/10);
    // return latestDuration;
  }

  return highPulseDurationAverage;
}

int getByteFromPWM(int pwmValue){
  if (pwmValue < 200){ return 0; }
  if (pwmValue < 400){ return 64; }
  if (pwmValue < 600){ return 128; }
  if (pwmValue < 800){ return 192; }
  if (pwmValue < 1000){ return 255; }
}

void pwm1_pulseTimer() {
  unsigned long currentTime = micros();
  switch(digitalRead(PWM_PIN_1)){
    case HIGH:
      pwm1_lastLastHighPulseTime = pwm1_lastHighPulseTime;
      pwm1_lastHighPulseTime = currentTime;
      break;
    case LOW:
      pwm1_lastLowPulseTime = currentTime;
      break;
  }
}

void pwm2_pulseTimer() {
  unsigned long currentTime = micros();
  switch(digitalRead(PWM_PIN_2)){
    case HIGH:
      pwm2_lastLastHighPulseTime = pwm2_lastHighPulseTime;
      pwm2_lastHighPulseTime = currentTime;
      break;
    case LOW:
      pwm2_lastLowPulseTime = currentTime;
      break;
  }
}
