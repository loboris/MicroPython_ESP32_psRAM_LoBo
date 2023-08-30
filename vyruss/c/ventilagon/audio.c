/*
a) awesome.wav
b) begin.wav
c) die.wav
d) excellent.wav
e) gameover.wav
f) hexagon.wav
g) line.wav
h) menuchoose.wav
i) menuselect.wav
j) pentagon.wav
k) rankup.wav
l) square.wav
m) start.wav
n) superhexagon.wav
o) triangle.wav
p) wonderful.wav
*/

void audio_play_superventilagon() {
  Serial.print("n");
}

void audio_play_crash() {
  Serial.print("c");
}

void audio_play_win() {
  Serial.print("d");
}

void audio_play_game_over() {
  Serial.print("e");
}

void audio_stop_song() {
  Serial.print("0");
}

void audio_begin() {
  Serial.print("b");
}

void audio_play_song(char song) {
  Serial.print(song);
}

void audio_reset() {
  Serial.print("0R");
}

void audio_stop_servo() {
  Serial.print("r");
}
