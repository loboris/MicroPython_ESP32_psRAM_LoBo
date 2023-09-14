#include "ventilagon.h"

uint64_t last_step = 0;

#define JOY_LEFT 1
#define JOY_RIGHT 2
#define JOY_UP 4
#define JOY_DOWN 8
#define BUTTON_A 16
#define BUTTON_B 32
#define BUTTON_C 64
#define BUTTON_D 128

QueueHandle_t queue_received;
QueueHandle_t queue_sending;

void ventilagon_init() {
  queue_received = xQueueCreate(20, sizeof(char));
  queue_sending = xQueueCreate(20, sizeof(char*));
  board_init();
}

void ventilagon_enter() {
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
  //audio_stop_song();
  audio_play_superventilagon();
  change_state(&resetting_state);
  // DEBUG
  // change_state(&credits_state);
  // change_state(&win_state);
}

void ventilagon_exit() {
  serial_send("arduino stop");
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

void ventilagon_process_received() {
    byte in_byte;
    if( xQueueReceive( queue_received, &in_byte, 0 ) ) {
      boton_ccw = in_byte & JOY_LEFT ;
      boton_cw = in_byte & JOY_RIGHT;
      if (in_byte >= BUTTON_A) {
          boton_cw = boton_ccw = true;
      }
    }
}

void ventilagon_loop() {
  ventilagon_process_received();
  current_state->loop();
}

void ventilagon_received(char c) {
}
