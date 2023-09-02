#include "ventilagon.h"

uint64_t last_step = 0;

void ventilagon_setup() {
  //pinMode(HALL_SENSOR, INPUT_PULLUP);
  //attachInterrupt(0, handle_interrupt, FALLING);
  //Serial.begin(57600);

  //randomSeed(analogRead(0));
  //randomSeed(83);

  ledbar_init();
  display_init();
  board_fill_patterns();
  new_level = 0;

  ledbar_reset();
  audio_stop_song();
  audio_play_superventilagon();
  change_state(&play_state);
}

bool boton_cw = false;
bool boton_ccw = false;

char inChar = 0;

// void serialEvent() {
//   while (Serial.available()) {
//     inChar = (char)Serial.read();
// 
//     switch (inChar) {
//       case 'L':
//         boton_ccw = true;
//         break;
//       case 'l':
//         boton_ccw = false;
//         break;
//       case 'R':
//         boton_cw = true;
//         break;
//       case 'r':
//         boton_cw = false;
//         break;
//       case ' ':
//         play_state.toggle_pause();
//         break;
// /***************
//  * probablemente no se necesite esto
//       case '-':
//         finetune_minus();
//         break;
//       case '=':
//         finetune_plus();
//         break;
//       case 'x':
//         finetune_next();
//         break;
// ***************/
//     }
// 
//     if (inChar != 0) {
//       if (inChar >= '1' && inChar <= '6') {
//         selectLevel(inChar - '1');
//       }
//       if (inChar == ' ') {
//         display.dump_debug();
//       }
//       if (inChar == 'n') {
//         board_fill_patterns(&board);
//       }
//       inChar = 0;
//     }
//   }
// }

void selectLevel(byte level) {
  new_level = level;
  audio_play_crash();
  change_state(&resetting_state);
}

void ventilagon_loop() {
  current_state->loop();
}
