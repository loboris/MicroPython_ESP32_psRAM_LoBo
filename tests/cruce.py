

def collision(x1, w1, x2, w2):
    delta = min(x1, x2)
    x1 = (x1 - delta + 128) % 256
    x2 = (x2 - delta + 128) % 256
    ret = x1 < x2 + w2 and x1 + w1 > x2
    return ret


testcases = [
    ((0,10, 5,10), True),
    ((5,10, 0,10), True),
    ((245,10, 0,10), False),
    ((0,10, 245,10), False),
    ((0,10, 250,10), True),
    ((250,10, 0,10), True),
    ((118,10, 124,10), True),
    ((0,10, 250,10), True),
    ((124,10, 118,10), True),
]

for k, v in testcases:
    print(k, "==", v)
    assert collision(*k) == v
