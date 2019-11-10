import matplotlib.image as mpimg
from numpy import sin, cos, pi, ndarray
from struct import pack
from gamma import gamma
import pdb

"""
La funcion toma un archivo *.png y devuelve en binario (RGBI) la secuencia 
de n_leds prendidos para n_ang diferentes
"""
def null_gamma(value):
    return value

gamma = null_gamma

def to_led(path=None, n_led=25, n_ang = 256):
    
    src = mpimg.imread(path)        # Levanta la imagen
    dst = ndarray((n_led, n_ang, 3))        # Levanta la imagen

    wx, wy, dim = src.shape         # Me da las dimensiones del frame en pixeles

    center_x = int((wx-1)/2)        # Calcula la cordenada x del centro de la imagen
    center_y = int((wy-1)/2)        # Calcula la cordenada y del centro de la imagen
    rad = min(center_x, center_y)   # Calcula el radio que barre la imagen dentro del frame

    led = []
    
    intensidad = [ int(31 * i**2 / n_led**2) for i in range(n_led)]
    
    for m in range(0,n_ang):
        for n in range(0,n_led):
            x = center_x + int(rad * (n+1)/n_led * cos(m * 2*pi/n_ang)) 
            y = center_y + int(rad * (n+1)/n_led * sin(m * 2*pi/n_ang))
            tupla_rgb = tuple(gamma(int(255 * g)) for g in src[x,y,0:3])
            tupla_led = tupla_rgb + (intensidad[n],)
            led.append(pack('BBBB', *tupla_led))
            dst[n, m] = tupla_rgb

    outfilename = path.rsplit(".", 1)[0] + "_flat.png"
    mpimg.imsave(outfilename, dst/256)
    return b''.join(led)
