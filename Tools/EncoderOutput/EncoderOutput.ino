// Transport adapter only. Wire an encoder library's debounced events to these functions.
// No GPIO assignments are assumed: choose them after verifying the exact board/encoder.
// One invocation means ONE virtual quarter-turn, not one raw electrical edge.
void emitTurn(char face, int direction) {
  if ((face != 'A' && face != 'B' && face != 'C') || (direction != 1 && direction != -1)) return;
  Serial.print("ROTATE,"); Serial.print(face); Serial.print(','); Serial.println(direction);
}
void emitAssemble() { Serial.println("ASSEMBLE"); }
void emitUndo() { Serial.println("UNDO"); }
void setup() { Serial.begin(115200); }
void loop() {
  // Example: when the top encoder completes one chosen detent, call emitTurn('A', 1).
  // Decode A/B phases and debounce buttons before calling this transport adapter.
}
