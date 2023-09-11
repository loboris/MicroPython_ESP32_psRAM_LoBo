#include "ventilagon.h"
State* current_state;

void change_state(State* new_state) {
  //debugln(new_state->name());
  current_state = new_state;
  current_state->setup();
}
