//#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <esp_timer.h>


typedef uint8_t byte;


//#define DEBUG

#ifdef DEBUG
#define debug(d) Serial.print(d)
#define debugln(d) Serial.println(d)
#else
#define debug(d)
#define debugln(d)
#endif

const byte NUM_COLUMNS = 6;
#define NUM_ROWS 32
const byte HALL_SENSOR = 2;

const byte ROW_SHIP = 3;
const byte ROW_COLISION = 7;

const int SUBDEGREES = 8192;
const int SUBDEGREES_MASK = 8191;

void pattern_init();
byte pattern_transform(byte b);
void pattern_randomize();
byte pattern_next_row();
bool pattern_is_finished();

void ledbar_setPixelColor(int pixel, long color);
bool ledbar_multicolored;
void ledbar_init();
void ledbar_clear();
void ledbar_reset();
void ledbar_set_win_state();
void ledbar_draw(byte num_row, bool value, bool alt_column);
void ledbar_update();

void board_init();
void board_reset();
void board_fill_patterns();
bool board_colision(int pos, byte num_row);
void board_step();
void board_step_back();
void board_draw_column(byte column);
void board_win_reset();
void board_win_step_back();


void display_reset();
void display_dump_debug();
void display_adjust_drift();
void display_tick(int64_t now);
void display_calibrate(bool calibrating);
bool display_ship_on(int current_pos);

typedef const char* (*get_name_fn_t)(void);
typedef void (*setup_fn_t)(void);
typedef void (*loop_fn_t)(void);

typedef struct {
		const char* name;
		setup_fn_t setup;
		loop_fn_t loop;
} State;

State* current_state;
void change_state(State* new_state);

extern State gameover_state;
extern State win_state;
extern State play_state;
extern State resetting_state;
extern State state_credits;

/*
struct GameoverState : public State {
  protected:
    bool keys_pressed;
  public:
    const char* name() {
      return "GAME OVER";
    }
    void apagar_todo();
    void setup();
    void loop();
};

struct WinState : public State {
  public:
    const char* name() {
      return "FOR THE WIN!";
    }
    void setup();
    void loop();
};

struct CreditsState : public State {
  public:
    const char* name() {
      return "Rolling Credits";
    }
    void setup();
    void loop();
};

struct PlayState : public State {
  public:

    const char* name() {
      return "RUNNING GAME";
    }
    void toggle_pause() {
      paused = !paused;
    }
    void setup();
    void loop();
};

struct ResettingState : public State {
  public:
    uint64_t last_step;
    byte counter;
    const char* name() {
      return "RESETTING";
    }
    void setup();
    void loop();
};
*/

typedef struct {
    bool on;
} Ship;
void ship_init();
void ship_prender();
void ship_apagar();

typedef int (*drift_fn_t)(int);

typedef struct Level_s {
    unsigned long step_delay;
    byte block_height;
    byte rotation_speed;
    char song;
    long color;
    long bg1, bg2;
    const byte* const* patterns;
    int num_patterns;
    drift_fn_t drift_calculator;
} Level;
int level_new_drift(int current_drift);

void audio_play_superventilagon();
void audio_play_win();
void audio_play_crash();
void audio_play_game_over();
void audio_begin();
void audio_stop_song();
void audio_stop_servo();
void audio_reset();
void audio_play_song(char song);

extern Ship ship;
extern const byte transformations[];
extern const Level* const levels[];
extern const Level* current_level;
extern byte new_level;
extern int nave_calibrate;
extern uint64_t last_step;
extern bool boton_cw;
extern bool boton_ccw;
extern int nave_pos;

#define elements_in(arrayname) (sizeof arrayname/sizeof *arrayname)
