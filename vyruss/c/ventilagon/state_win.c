WinState win_state;

const long win_step_delay = 25;
unsigned long win_last_step = 0;
unsigned long win_started;
const long win_delay_1 = 45000 - 7000; // 45 seconds
const long win_delay_2 = win_delay_1 + 7000; // un poquin mas para que se vaya todo

void WinState::setup() {
  display.calibrate(true);
  board.reset();
  ledbar.set_win_state();
  win_started = millis();
}

void WinState::loop() {
  unsigned long now_ms = millis();
  if ((now_ms - win_last_step) > win_step_delay) {
    win_last_step = now_ms;
    if ((now_ms - win_started) > win_delay_1) {
      board.step_back();
    } else {
      board.win_step_back();
    }
  }

  if ((now_ms - win_started) > win_delay_2) {
    State::change_state(&gameover_state);
  }
  unsigned long now = micros();
  display.tick(now);
  display.adjust_drift();
}


