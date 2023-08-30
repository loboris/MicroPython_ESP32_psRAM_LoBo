const byte SHIP_PIN_R = 7;
const byte SHIP_PIN_G = 5;
const byte SHIP_PIN_B = 4;

Ship ship;

void Ship::init() {
  pinMode(SHIP_PIN_R, OUTPUT);
  pinMode(SHIP_PIN_G, OUTPUT);
  pinMode(SHIP_PIN_B, OUTPUT);
}

void Ship::prender() {
  digitalWrite(SHIP_PIN_R, LOW);
  digitalWrite(SHIP_PIN_G, LOW);
  digitalWrite(SHIP_PIN_B, LOW);
}

void Ship::apagar() {
  digitalWrite(SHIP_PIN_R, HIGH);
  digitalWrite(SHIP_PIN_G, HIGH);
  digitalWrite(SHIP_PIN_B, HIGH);
}


