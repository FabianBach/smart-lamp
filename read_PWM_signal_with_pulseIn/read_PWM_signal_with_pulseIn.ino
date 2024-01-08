#define PWM_PIN_1 2
#define PWM_PIN_2 3

int pwm1_value = 0;
int pwm2_value = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PWM_PIN_1, INPUT_PULLUP);
  pinMode(PWM_PIN_2, INPUT_PULLUP);
}

void loop() {
  pwm1_value = readHighPulseDuration(PWM_PIN_1);
  pwm2_value = readHighPulseDuration(PWM_PIN_2);

  Serial.print(-50); // just as a stable point of reference
  Serial.print(" ");
  Serial.print(300); // just as a stable point of reference
  Serial.print(" ");
  Serial.print(pwm1_value);
  Serial.print(" ");
  Serial.println(pwm2_value);
}


int readHighPulseDuration(byte pin) {
  unsigned long highTime = pulseIn(pin, HIGH, 2000);  // 2000 micros timeout, I measured a total pulse lenght (high to high) of 1000

  if (highTime == 0)
    highTime = digitalRead(pin) ? 1000 : 0;  // HIGH == 100%,  LOW = 0%

  if (highTime > 1000)
    highTime = 1000;

  return (255 * highTime)/1000; // map to range of 0 - 255
  // return highTime;
}

// byte GetPWM(byte pin) {
//   unsigned long highTime = pulseIn(pin, HIGH, 1010);  // 1010 micro timeout
//   unsigned long lowTime = pulseIn(pin, LOW, 1010);

//   // pulseIn() returns zero on timeout
//   if (highTime == 0 || lowTime == 0)
//     return digitalRead(pin) ? 100 : 0;  // HIGH == 100%,  LOW = 0%

//   return (100 * highTime) / (highTime + lowTime);  // highTime as percentage of total cycle time
// }