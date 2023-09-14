#include "ventilagon.h"
bool keys_pressed;

void gameover_setup() {
  display_calibrate(true);
  audio_play_game_over();
  serial_send("arduino attract");
  keys_pressed = (boton_cw || boton_ccw);
}

void gameover_loop() {
  if (boton_cw == false && boton_ccw == false) {
    keys_pressed = false;
  }
  if (keys_pressed == false) {
   if (boton_cw && boton_ccw) {
     change_state(&resetting_state);
   } else {
     /* calibrate
     if (boton_cw) {
       nave_calibrate++;
     } else if (boton_ccw) {
       nave_calibrate--;
     }
     debugln(nave_calibrate);
     */
   }
  }
  int64_t now = esp_timer_get_time();
  display_tick(now);
}


State gameover_state = { "GAME OVER", gameover_setup, gameover_loop };
