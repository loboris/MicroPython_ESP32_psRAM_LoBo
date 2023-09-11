from pprint import pformat
n_led = 54

def gamma(value, gamma=2.5, offset=0.5):
    assert 0 <= value <= 255
    return int( pow( float(value) / 255.0, gamma ) * 255.0 + offset )

intensidad = [ 1 - i**2 / (n_led)**2 for i in range(n_led)]
print(intensidad)

def value(v, px):
    return gamma(v * intensidad[n_led - 1 - px])

valores = [ [ int(value(v, px)) for v in range(256)] for px in range(n_led) ]

text = pformat(valores, compact=True, width=150)
text = text.replace("[", "{").replace("]", "}") + ";"
#print(text)
