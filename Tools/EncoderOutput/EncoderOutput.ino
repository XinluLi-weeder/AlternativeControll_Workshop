// Transport adapter only. Wire an encoder library's debounced events to these functions.
// No GPIO assignments are assumed: choose them after verifying the exact board/encoder.
// One invocation means ONE virtual quarter-turn, not one raw electrical edge.
void emitTurn(char face, int direction) {
  if ((face < 'A' || face > 'E') || (direction != 1 && direction != -1)) return;
  Serial.print("ROTATE,"); Serial.print(face); Serial.print(','); Serial.println(direction);
}
void emitAssemble() { Serial.println("ASSEMBLE"); }
void emitUndo() { Serial.println("UNDO"); }
// Optional motor output through a transistor/MOSFET motor driver, never directly to a motor.
// Set a PWM-capable pin for your actual board after wiring the driver. -1 keeps output disabled.
const int HAPTIC_PWM_PIN = -1;
unsigned long hapticUntil = 0;
char feedbackLine[48];
byte feedbackLength = 0;
void setup() {
  Serial.begin(115200);
  if (HAPTIC_PWM_PIN >= 0) { pinMode(HAPTIC_PWM_PIN, OUTPUT); analogWrite(HAPTIC_PWM_PIN, 0); }
}
void loop() {
  // Example: when the top encoder completes one chosen detent, call emitTurn('A', 1).
  // Decode A/B phases and debounce buttons before calling this transport adapter.
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      feedbackLine[feedbackLength] = '\0';
      int duration = 0, strength = 0;
      if (sscanf(feedbackLine, "HAPTIC,%d,%d", &duration, &strength) == 2 && HAPTIC_PWM_PIN >= 0) {
        analogWrite(HAPTIC_PWM_PIN, constrain(strength, 0, 255));
        hapticUntil = millis() + constrain(duration, 0, 500);
      }
      feedbackLength = 0;
    } else if (feedbackLength < sizeof(feedbackLine)-1) feedbackLine[feedbackLength++] = c;
    else feedbackLength = 0;
  }
  if (hapticUntil && (long)(millis() - hapticUntil) >= 0) {
    if (HAPTIC_PWM_PIN >= 0) analogWrite(HAPTIC_PWM_PIN, 0);
    hapticUntil = 0;
  }
}
