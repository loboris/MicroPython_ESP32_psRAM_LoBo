ResettingState resetting_state;

const unsigned long reset_step_delay = (10L * 100 * 1000) / NUM_ROWS;


void ResettingState::setup() {
  last_step = micros();
  counter = 0;
  audio.reset();
}

void ResettingState::loop() {
  unsigned long now = micros();
  display.tick(now);
  
  // tirar las lineas para afuera durante medio segundo
  if ((now - last_step) > reset_step_delay) {
    board.step_back();
    last_step = now;
    counter++;
  }

  // despues de medio segundo, arrancar el juego
  if (counter > NUM_ROWS) {
    State::change_state(&play_state);
  }
}
