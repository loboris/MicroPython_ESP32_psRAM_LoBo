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

/*
void circularbuffer_CircularBuffer(CircularBuffer* cb);
void circularbuffer_reset(CircularBuffer* cb);
void circularbuffer_push_back(CircularBuffer* cb, byte row);
void circularbuffer_push_front(CircularBuffer* cb, byte row);
byte circularbuffer_get_row(CircularBuffer* cb, byte row_num);
*/

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

/*
typedef struct {
    virtual const char* name() = 0;
    virtual void setup() = 0;
    virtual void loop() {}
} State;

State* current_state;
void change_state(State* new_state);

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
    int section;
    unsigned long section_init_time;
    unsigned long section_duration;
    bool paused;
    
    void check_section(unsigned long now);
    void advance_section(unsigned long now);

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
    long int last_step;
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
/*
extern GameoverState gameover_state;
extern WinState win_state;
extern PlayState play_state;
extern ResettingState resetting_state;
extern CreditsState state_credits;
*/
extern const byte transformations[];
extern const Level* const levels[];
extern const Level* current_level;
extern byte new_level;
extern int nave_calibrate;

#define elements_in(arrayname) (sizeof arrayname/sizeof *arrayname)
