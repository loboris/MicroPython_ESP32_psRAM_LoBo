#include <math.h>
#include <stdio.h>
#define COLUMNS 256
#define PIXELS 54
#define ROWS 256

int deepspace[ROWS];

void main() {
  const int EMPTY_PIXELS = 16;
  const int VISIBLE_ROWS = ROWS - EMPTY_PIXELS;
  const double GAMMA = 0.28;

  int n;
  for (n=0; n<EMPTY_PIXELS; n++) {
    deepspace[n] = PIXELS;
  }

  for (int j=VISIBLE_ROWS-1; j>-1; j--) {
    deepspace[n++] = PIXELS * pow((double)j / VISIBLE_ROWS, 1/GAMMA) + 0.5;
  }


  printf("[");
  for (int w=0; w<ROWS; w++) {
    printf("%d, ", deepspace[w]);
  }
  printf("]\n");
}
