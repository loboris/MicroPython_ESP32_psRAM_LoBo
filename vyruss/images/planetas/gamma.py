def gamma(value,gamma=2.5,offset=0.5):
    assert 0 <= value <= 255
    return int( pow( float(value) / 255.0, gamma ) * 255.0 + offset )
