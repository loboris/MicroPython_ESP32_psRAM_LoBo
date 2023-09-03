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

void serial_send(const char* text) {
  xQueueSend(queue_sending, &text, 0);
}

void audio_play(const char* command) {
  serial_send(command);
}

void audio_play_superventilagon() {
  audio_play("sound superhexagon");
}

void audio_play_crash() {
  audio_play("sound die");
}

void audio_play_win() {
  audio_play("sound excellent");
}

void audio_play_game_over() {
  audio_play("sound gameover");
}

void audio_stop_song() {
  serial_send("music off");
}

void audio_begin() {
  serial_send("sound begin");
}

void audio_reset() {
  serial_send("music off");
}

void audio_stop_servo() {
  serial_send("servo stop");
}
