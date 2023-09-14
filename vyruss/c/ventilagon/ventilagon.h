//#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef uint8_t byte;


//#define DEBUG

#ifdef DEBUG
#define debug(d) Serial.print(d)
#define debugln(d) Serial.println(d)
#else
#define debug(d)
#define debugln(d)
#endif

#define NUM_COLUMNS 6
#define NUM_ROWS 48
#define HALL_SENSOR 2

#define ROW_SHIP 3
#define ROW_COLISION 7

#define SHIP_COLOR 0xff0000ff

#define SUBDEGREES 8192
#define SUBDEGREES_MASK 8191

void pattern_init();
byte pattern_transform(byte b);
void pattern_randomize();
byte pattern_next_row();
bool pattern_is_finished();

void ledbar_setPixelColor(uint32_t* buffer, int pixel, uint32_t color);
bool ledbar_multicolored;
void ledbar_init();
void ledbar_reset();
void ledbar_set_win_state();
void ledbar_draw(uint32_t* buffer, byte num_row, bool value, bool alt_column);

void board_init();
void board_reset();
void board_fill_patterns();
bool board_colision(int pos, byte num_row);
void board_step();
void board_step_back();
void board_draw_column(byte column, uint32_t* pixels);
void board_win_reset();
void board_win_step_back();


void display_init();
void display_reset();
void display_dump_debug();
void display_adjust_drift();
void display_tick(int64_t now);
void display_calibrate(bool calibrating);

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
extern State credits_state;

void ship_draw();

typedef int (*drift_fn_t)(int);

typedef struct Level_s {
    unsigned long step_delay;
    byte block_height;
    byte rotation_speed;
    char* song;
    uint32_t color;
    uint32_t bg1, bg2;
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
void audio_play(const char* command);
extern const char* section_sounds[];

extern const byte transformations[];
extern const Level* const levels[];
extern const Level* current_level;
extern byte new_level;
extern int nave_calibrate;
extern uint64_t last_step;
extern uint64_t last_move;
extern bool boton_cw;
extern bool boton_ccw;
extern int nave_pos;
extern void serial_send(const char* line);
extern int queued_steps;

extern QueueHandle_t queue_received;
extern QueueHandle_t queue_sending;

extern uint32_t colors[];

void ventilagon_init();
void ventilagon_enter();
void ventilagon_loop();
void ventilagon_exit();

extern uint32_t* extra_buf;
extern uint32_t* pixels0;
extern uint32_t* pixels1;
void spi_write_HSPI();

#define elements_in(arrayname) (sizeof arrayname/sizeof *arrayname)
