State* State::current_state;

void State::change_state (State* new_state) {
  //debugln(new_state->name());
  State::current_state = new_state;
  State::current_state->setup();
}
