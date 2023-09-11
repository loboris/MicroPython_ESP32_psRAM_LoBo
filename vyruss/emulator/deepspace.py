EMPTY_PIXELS = 16
PIXELS = 54
ROWS = 256 - EMPTY_PIXELS
GAMMA = 0.28

empty = [PIXELS] * EMPTY_PIXELS
deepspace = empty + [
    int(PIXELS * pow(float(n) / ROWS, 1/GAMMA) + 0.5)
    for n in range(ROWS-1, -1, -1)
]
print(deepspace)