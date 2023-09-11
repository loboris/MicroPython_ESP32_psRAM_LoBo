from PIL import Image

def sprite_coords(x, y):
    return (x*16, y*16, x*16+16, y*16+16)

src = Image.open("galaga-rotated-sprites.png")

nombres = [
    "galaga",
    "captured",
    "greenboss",
    "blueboss",
    "redmoth",
    "bluebee",
    "galaxian",
    "skorpion",
    "greenshit",
    "dumbbug",
    "newsat",
    "spock"
]

def rotpos(j):
    rems = [6, 7, 5, 4, 3, 2, 1]
    rotations = [None, Image.ROTATE_90, Image.ROTATE_180, Image.ROTATE_270]
    quot, rem = divmod(j, 7)
    print(quot, rem)
    return rems[rem], rotations[quot]

for y in range(12):
    dest = Image.new("RGBA", (28*16, 16), (0,0,0,0))
    for j in range(28):
        x, rot = rotpos(j)
        crp = src.crop(sprite_coords(x,y))
        if rot:
            crp = crp.transpose(rot)
        dest.paste(crp, sprite_coords(27-j, 0))
    filename = nombres[y]
    dest.save("recorte/%02d_%s.png" % (y, filename))

