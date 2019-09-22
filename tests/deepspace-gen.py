reference = [
    53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38,
    37, 36, 35, 35, 34, 33, 32, 31, 30, 30, 29, 28, 27, 27, 26, 25,
    25, 24, 23, 23, 22, 21, 21, 20, 20, 19, 18, 18, 17, 17, 16, 16,
    15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 11, 10, 10, 9, 9, 9, 8,
    8, 8, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3,
    3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
]

EMPTY_PIXELS = 16
PIXELS = 54
ROWS = 256 - EMPTY_PIXELS
GAMMA = 0.38

empty = [PIXELS] * EMPTY_PIXELS
new = [
    int(PIXELS * pow(float(n) / ROWS, 1/GAMMA) + 0.5)
    for n in range(ROWS-1, -1, -1)
]

print(reference)
print(empty + new)
print len(empty + new)
print(reference == new)
