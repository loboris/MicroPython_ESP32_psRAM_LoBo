GameoverState gameover_state;

void GameoverState::setup() {
  display.calibrate(true);
  audio.play_game_over();
  keys_pressed = (boton_cw || boton_ccw);
}

void GameoverState::loop() {
  if (boton_cw == false && boton_ccw == false) {
    keys_pressed = false;
  }
  if (keys_pressed == false) {
   if (boton_cw && boton_ccw) {
     State::change_state(&resetting_state);
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
  unsigned long now = micros();
  display.tick(now);
}


