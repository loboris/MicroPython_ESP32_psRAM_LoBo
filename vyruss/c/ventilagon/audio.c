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

#include "ventilagon.h"

void serial_send(char* text) {
  // FIXME: actually send something somewhere
}

void audio_play(char* song) {
  serial_send("sound ");
  serial_send(song);
  serial_send("\n");
}

void audio_play_song(char* song) {
  serial_send("music ");
  serial_send(song);
  serial_send("\n");
}

void audio_play_superventilagon() {
  audio_play("n");
}

void audio_play_crash() {
  audio_play("c");
}

void audio_play_win() {
  audio_play("d");
}

void audio_play_game_over() {
  audio_play("e");
}

void audio_stop_song() {
  audio_play("0");
}

void audio_begin() {
  audio_play("b");
}

void audio_reset() {
  serial_send("music off\n");
}

void audio_stop_servo() {
  serial_send("servo stop\n");
}
