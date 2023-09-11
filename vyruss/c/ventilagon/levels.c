// Roughly based on: "Super Hexagon 101 : Know your Patterns"
// http://steamcommunity.com/sharedfiles/filedetails/?id=121153531

// patterns 231 bytes, patterns_levels 118 bytes

#include "ventilagon.h"

#define PATTERN const byte

PATTERN solo_A[] = {
  10,
  0b010101,
  0b000000,
  0b010101,
  0b000000,
  0b000000,
  0b000000,
  0b111011,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN solo_B[] = {
  9,
  0b010101,
  0b010101,
  0b000000,
  0b000000,
  0b000000,
  0b111011,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN solo_C[] = {
  10,
  0b010010,
  0b000000,
  0b010010,
  0b000000,
  0b000000,
  0b000000,
  0b111011,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN solo_D[] = {
  9,
  0b010010,
  0b010010,
  0b000000,
  0b000000,
  0b000000,
  0b111011,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN triple_C[] = {
  12,
  0b101111,
  0b000000,
  0b000000,
  0b000000,
  0b111101,
  0b000000,
  0b000000,
  0b000000,
  0b101111,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN bat[] = {
  15,
  0b011111,
  0b001110,
  0b001110,
  0b001110,
  0b000100,
  0b100100,
  0b100100,
  0b110001,
  0b110001,
  0b111011,
  0b111011,
  0b010001,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN whirpool[] = {
  18,
  0b011111,
  0b001111,
  0b000111,
  0b000011,
  0b100001,
  0b110000,
  0b011000,
  0b001100,
  0b000110,
  0b000011,
  0b100001,
  0b110000,
  0b111000,
  0b111100,
  0b111110,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN double_whirpool[] = {
  12,
  0b110110,
  0b010010,
  0b001001,
  0b100100,
  0b010010,
  0b001001,
  0b100100,
  0b010010,
  0b011011,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN three_21[] = {
  19,
  0b110111,
  0b000000,
  0b000000,
  0b000000,
  0b011100,
  0b111110,
  0b111110,
  0b001000,
  0b001000,
  0b001001,
  0b101011,
  0b101011,
  0b001001,
  0b001001,
  0b011101,
  0b011101,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN ladder[] = {
  16,
  0b110110,
  0b010010,
  0b010010,
  0b011011,
  0b010010,
  0b010010,
  0b110110,
  0b010010,
  0b010010,
  0b011011,
  0b010010,
  0b010010,
  0b110110,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN four_spin[] = {
  16,
  0b011111,
  0b000001,
  0b000001,
  0b000001,
  0b000001,
  0b000001,
  0b111101,
  0b000001,
  0b000001,
  0b000001,
  0b000001,
  0b000001,
  0b011111,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN three_spin[] = {
  25,
  0b111101,
  0b100001,
  0b100001,
  0b100001,
  0b101111,
  0b100111,
  0b100011,
  0b100001,
  0b100001,
  0b111101,
  0b111001,
  0b110001,
  0b100001,
  0b100001,
  0b101111,
  0b100111,
  0b100011,
  0b100001,
  0b100001,
  0b111101,
  0b111001,
  0b110001,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN two_spin[] = {
  21,
  0b111100,
  0b100000,
  0b100000,
  0b100111,
  0b100000,
  0b100000,
  0b111100,
  0b100000,
  0b100000,
  0b100111,
  0b100000,
  0b100000,
  0b111100,
  0b100000,
  0b100000,
  0b100111,
  0b100000,
  0b100000,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN multi_C[] = {
  19,
  0b011111,
  0b000000,
  0b000000,
  0b101111,
  0b000000,
  0b000000,
  0b110111,
  0b000000,
  0b000000,
  0b111011,
  0b000000,
  0b000000,
  0b111101,
  0b000000,
  0b000000,
  0b111110,
  0b000000,
  0b000000,
  0b000000,
};

PATTERN rain[] = {
  20,
  0b010101,
  0b000000,
  0b000000,
  0b101010,
  0b000000,
  0b000000,
  0b010101,
  0b000000,
  0b000000,
  0b101010,
  0b000000,
  0b000000,
  0b010101,
  0b000000,
  0b000000,
  0b101010,
  0b101010,
  0b000000,
  0b000000,
  0b000000,
};


const byte * const patterns_level1[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  whirpool,
  bat,
  ladder,
};

const byte * const patterns_level2[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  multi_C,
  bat,
  three_21,
  ladder,
  two_spin,
  four_spin,
  rain,
};

const byte * const patterns_level3[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  double_whirpool,
  three_21,
  four_spin,
  rain,
};

const byte * const patterns_level4[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  whirpool,
  bat,
  ladder,
  three_21,
};

const byte * const patterns_level5[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  multi_C,
  double_whirpool,
  bat,
  three_21,
  ladder,
  three_spin,
  rain,
};

const byte * const patterns_level6[] = {
  solo_A,
  solo_B,
  solo_C,
  solo_D,
  triple_C,
  double_whirpool,
  three_21,
  four_spin,
  rain,
};

int no_drift(int drift_speed) {
  return 0;
};

int slow_drift(int drift_speed) {
  int drift_random = rand() % 375;
  if (drift_random < 2) {
    if (drift_speed == 0) {
      drift_speed = -1;
    }
  }
  return drift_speed;
}

int med_drift(int drift_speed) {
  int drift_random = rand() % 375;
  if (drift_random < 4) {
    drift_speed = drift_random - 2;
    if (drift_speed == 0) {
      drift_speed = 2;
    }
  }
  return drift_speed;
}

int high_drift(int drift_speed) {
  int drift_random = rand() % 375;
  if (drift_random < 6) {
    drift_speed = drift_random - 3;
    if (drift_speed == 0) {
      drift_speed = 3;
    }
  }
  return drift_speed;
}

int crazy_drift(int drift_speed) {
  int drift_random = rand() % 375;
  if (drift_random < 10) {
    drift_speed = drift_random - 5;
    if (drift_speed == 0) {
      drift_speed = 5;
    }
  }
  return drift_speed;
}

int level_new_drift(int current_drift) {
  return current_level->drift_calculator(current_drift);
}

/*
37500L
33750L
30000L
30000L
30000L
30000L
*/

const Level level1 = {37500, 6, 5, "music 1", 0x000066ff, 0x000000ff, 0x000004ff, patterns_level1, elements_in(patterns_level1), &no_drift};
const Level level2 = {33750, 6, 6, "music 2", 0x006600ff, 0x000000ff, 0x000400ff, patterns_level2, elements_in(patterns_level2), &slow_drift};
const Level level3 = {30000, 6, 7, "music 3", 0x444400ff, 0x000000ff, 0x040400ff, patterns_level3, elements_in(patterns_level3), &slow_drift};
const Level level4 = {29000, 6, 7, "music 1", 0x004444ff, 0x000000ff, 0x000404ff, patterns_level4, elements_in(patterns_level4), &med_drift};
const Level level5 = {28000, 6, 7, "music 2", 0x440044ff, 0x000000ff, 0x040004ff, patterns_level5, elements_in(patterns_level5), &high_drift};
const Level level6 = {27000, 6, 7, "music 3", 0x660000ff, 0x000000ff, 0x040000ff, patterns_level6, elements_in(patterns_level6), &crazy_drift};


const Level* const levels[] = {
  &level1, &level2, &level3, &level4, &level5, &level6, NULL
};

const Level* current_level = &level1;
byte new_level = 0;
