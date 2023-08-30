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

Audio audio;

void Audio::play_superventilagon() {
  Serial.print("n");
}

void Audio::play_crash() {
  Serial.print("c");
}

void Audio::play_win() {
  Serial.print("d");
}

void Audio::play_game_over() {
  Serial.print("e");
}

void Audio::stop_song() {
  Serial.print("0");
}

void Audio::begin() {
  Serial.print("b");
}

void Audio::play_song(char song) {
  Serial.print(song);
}

void Audio::reset() {
  Serial.print("0R");
}

void Audio::stop_servo() {
  Serial.print("r");
}
