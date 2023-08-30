#include "ventilagon.h"

const byte* transformation_base;
int current_height;
int block_height;
int row;
const byte* rows_base;
byte rows_len;

void pattern_init() {
  pattern_randomize();
  block_height = current_level->block_height;
  current_height = block_height;
}

void pattern_randomize() {
  // init current_height to max, so first call to next_row() calculates the value of row zero
  current_height = block_height = current_level->block_height;
  row = 0;
  transformation_base = transformations + ((rand() % 12) << 6);
  int new_pattern = rand() % current_level->num_patterns;
  rows_base = (const byte*) *(current_level->patterns + new_pattern);
  rows_len = *(rows_base++);
}

byte pattern_transform(byte b) {
  return *(transformation_base + b);
}

byte pattern_next_row() {
  static byte value;
  if (current_height++ >= block_height) {
    current_height = 0;
    byte base_value = *(rows_base + row++);
    value = pattern_transform(base_value);
  }
  return value;
}

bool pattern_is_finished() {
  return (row >= rows_len) && (current_height >= block_height) ;
}
