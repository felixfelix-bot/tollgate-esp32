BASE32 = "0123456789bcdefghjkmnpqrstuvwxyz"


def encode(lat, lon, prec):
    lat_r = [-90.0, 90.0]
    lon_r = [-180.0, 180.0]
    bits = []
    even = True
    while len(bits) < prec * 5:
        if even:
            mid = (lon_r[0] + lon_r[1]) / 2
            if lon >= mid:
                bits.append(1)
                lon_r[0] = mid
            else:
                bits.append(0)
                lon_r[1] = mid
        else:
            mid = (lat_r[0] + lat_r[1]) / 2
            if lat >= mid:
                bits.append(1)
                lat_r[0] = mid
            else:
                bits.append(0)
                lat_r[1] = mid
        even = not even
    out = ""
    for i in range(prec):
        v = 0
        for b in bits[i * 5:i * 5 + 5]:
            v = (v << 1) | b
        out += BASE32[v]
    return out


for lat, lon, tag in [
    (48 + 7.038 / 60, 11 + 31.0 / 60, "wiki GGA/NMEA"),
    (-(33 + 52.0 / 60), -(151 + 12.0 / 60), "sydney-ish"),
    (48.1351, 11.5820, "Munich ref"),
    (-(37 + 51.65 / 60), 145 + 7.36 / 60, "melbourne"),
]:
    print(tag, encode(lat, lon, 9))
