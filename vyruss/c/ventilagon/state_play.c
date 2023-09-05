#include <limits.h>
#include "ventilagon.h"

int section;
int64_t section_init_time;
int64_t section_duration;
uint64_t last_move;
bool paused;

#define CENTISECONDS (10UL * 1000UL) // in microseconds

int64_t section_durations[] = {
  1325 * CENTISECONDS,
  1325 * CENTISECONDS,
  1325 * CENTISECONDS,
  1325 * CENTISECONDS,
  1325 * 2 * CENTISECONDS,
  1325 * 3 * CENTISECONDS,
  ULONG_MAX
};

const char* section_sounds[] = {
  "sound -",
  "sound line",
  "sound triangle",
  "sound square",
  "sound pentagon",
  "sound hexagon"
};

void play_setup() {
  current_level = levels[new_level];
  paused = false;
  board_reset();
  audio_begin();
  display_reset();
  display_calibrate(false);
  audio_play(current_level->song);
  section = 0;
  section_init_time = esp_timer_get_time();
  section_duration = section_durations[section];
}

void advance_section(int64_t now) {
  section++;
  section_init_time = now;
  section_duration = section_durations[section];
  audio_play(section_sounds[section]);
  if (levels[section] == NULL) {
    // ganaste
    audio_play_win();
    audio_stop_servo();
    // FIXME: volver a poner los creditos
    // change_state(&credits_state);
    change_state(&win_state);
    return;
  }
  current_level = levels[section];
}

void check_section(int64_t now) {
  if (now - section_init_time > section_duration) {
    advance_section(now);
  }
}

void play_loop() {
  int64_t now = esp_timer_get_time();

  if (now > (last_move + current_level->step_delay/156)) {
    if (boton_cw != boton_ccw) {
      int new_pos = 0;

      if (boton_cw) {
	new_pos = nave_pos + current_level->rotation_speed;
      }
      if (boton_ccw) {
	new_pos = nave_pos - current_level->rotation_speed;
      }

      new_pos = (new_pos + SUBDEGREES) & SUBDEGREES_MASK;

      bool colision_futura = board_colision(new_pos, ROW_SHIP);
      if (!colision_futura) {
	nave_pos = new_pos;
      }
    }
    last_move = now;
  }


  if (now > (last_step + current_level->step_delay)) {
    // FIXME
    board_step();
    if (!board_colision(nave_pos, ROW_SHIP)) {
      if (!paused) {
        board_step();
      }
    } else {
      // crash boom bang
      ledbar_reset();
      audio_play_crash();
      audio_stop_song();
      change_state(&gameover_state);
    }
    last_step = now;
  }

  display_tick(now);
  display_adjust_drift();

  check_section(now);
  /*
  while (1) {
    int wait = esp_timer_get_time()-(loop_start+slice);
    if (wait > 16000) {
      delayMicroseconds(10000);
    } else {
      delayMicroseconds(esp_timer_get_time()-(loop_start+slice));
      break;
    }
  }
  */

  //colorizer.step();
}

void toggle_pause() {
  paused = !paused;
}

State play_state = { "RUNNING GAME", play_setup, play_loop };
