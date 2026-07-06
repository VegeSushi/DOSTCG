import os
import struct

try:
    from PIL import Image
except ImportError:
    Image = None

SPRITE_SIZE = 64  # 64x64 = 4096 bytes, matches the game's far-memory sprite buffer

# Standard 16-color EGA/VGA palette (matches the color indices already used
# elsewhere in the game's UI code: 1=blue, 2=green, 4=red, 7=light gray,
# 8=dark gray, 10=light green, 11=light cyan, 12=light red, 14=yellow, 15=white)
EGA_PALETTE = [
    (0,   0,   0),    # 0  Black
    (0,   0,   170),  # 1  Blue
    (0,   170, 0),    # 2  Green
    (0,   170, 170),  # 3  Cyan
    (170, 0,   0),    # 4  Red
    (170, 0,   170),  # 5  Magenta
    (170, 85,  0),    # 6  Brown
    (170, 170, 170),  # 7  Light Gray
    (85,  85,  85),   # 8  Dark Gray
    (85,  85,  255),  # 9  Light Blue
    (85,  255, 85),   # 10 Light Green
    (85,  255, 255),  # 11 Light Cyan
    (255, 85,  85),   # 12 Light Red
    (255, 85,  255),  # 13 Light Magenta
    (255, 255, 85),   # 14 Yellow
    (255, 255, 255),  # 15 White
]


# Must match the TYPE_* constants in types.h (Character.elem_type)
CHAR_TYPES = {
    "NORMAL": 0,
    "FIRE": 1,
    "WATER": 2,
    "GRASS": 3,
    "EARTH": 4,
    "ELECTRIC": 5,
}


def _nearest_ega_index(rgb):
    r, g, b = rgb
    best_i, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(EGA_PALETTE):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best_d is None or d < best_d:
            best_d, best_i = d, i
    return best_i


def png_to_raw_bytes(png_path, size=SPRITE_SIZE):
    """Convert a PNG into `size*size` raw bytes, one EGA palette index per pixel."""
    if Image is None:
        raise RuntimeError("Pillow is required for PNG conversion. Install with: pip install pillow")

    img = Image.open(png_path).convert("RGB")

    # Fit into a square canvas (center, black-padded) then resize down/up to target size.
    w, h = img.size
    side = max(w, h)
    canvas = Image.new("RGB", (side, side), (0, 0, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2))
    canvas = canvas.resize((size, size), Image.NEAREST)

    pixels = canvas.load()
    out = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            out[y * size + x] = _nearest_ega_index(pixels[x, y])
    return bytes(out)


def get_sprite_bytes(char_folder):
    """Prefer sprite.raw if present; otherwise auto-convert sprite.png; otherwise fallback."""
    raw_path = os.path.join(char_folder, "sprite.raw")
    png_path = os.path.join(char_folder, "sprite.png")

    if os.path.exists(raw_path):
        with open(raw_path, "rb") as f:
            data = bytearray(f.read(4096))
    elif os.path.exists(png_path):
        print(f"  Converting {png_path} -> raw sprite data...")
        data = bytearray(png_to_raw_bytes(png_path))
    else:
        data = bytearray([16] * 4096)  # fallback marker (out-of-palette black square)
        return data

    if len(data) < 4096:
        data.extend([0] * (4096 - len(data)))
    return data[:4096]


def pack_character(char_folder, output_file):
    print(f"Packing {char_folder} into {output_file}...")
    stats_path = os.path.join(char_folder, 'stats.txt')
    moves_path = os.path.join(char_folder, 'moves.txt')

    raw_name = os.path.basename(char_folder).encode('ascii')[:15]
    name = raw_name.ljust(16, b'\0')
    max_hp, hp, base_atk, base_def = 100, 100, 10, 10
    elem_type = CHAR_TYPES["NORMAL"]

    if os.path.exists(stats_path):
        with open(stats_path, 'r') as f:
            for line in f:
                if '=' in line:
                    k, v = line.strip().split('=')
                    if k == 'MAX_HP': max_hp = hp = int(v)
                    if k == 'BASE_ATK': base_atk = int(v)
                    if k == 'BASE_DEF': base_def = int(v)
                    if k == 'TYPE':
                        v = v.strip().upper()
                        if v not in CHAR_TYPES:
                            print(f"  Warning: unknown TYPE '{v}' in {stats_path}, defaulting to NORMAL")
                        elem_type = CHAR_TYPES.get(v, CHAR_TYPES["NORMAL"])

    moves = []
    if os.path.exists(moves_path):
        with open(moves_path, 'r') as f:
            for line in f:
                if line.startswith('#') or not line.strip(): continue
                parts = line.strip().split(',')
                m_name = parts[0].encode('ascii')[:15].ljust(16, b'\0')
                moves.append((m_name, int(parts[1]), int(parts[2])))

    while len(moves) < 4: moves.append((b"None\0\0\0\0\0\0\0\0\0\0\0\0", 0, 0))

    sprite_data = get_sprite_bytes(char_folder)

    with open(output_file, 'wb') as f:
        f.write(b'CHAR')
        f.write(struct.pack('<H', 1))
        f.write(struct.pack('<H', 1))
        f.write(name)
        f.write(struct.pack('<I', 28))
        f.write(struct.pack('<I', 4096 + 56))
        f.write(struct.pack('<B', 0))
        f.write(b'\0'*3)
        f.write(name)
        # Field order matches Character in types.h: max_hp, hp, base_atk,
        # base_def, elem_type, pad2 (pad2 stays reserved/0).
        f.write(struct.pack('<iiiiii', max_hp, hp, base_atk, base_def, elem_type, 0))
        for m in moves[:4]:
            f.write(m[0])
            f.write(struct.pack('<ii', m[1], m[2]))
        f.write(sprite_data)


def pack_core():
    # Packs 3 backgrounds into core.pak
    output = "content/core.pak"
    print(f"Packing CORE backgrounds into {output}...")
    bgs = [
        (b"Grassland\0\0\0\0\0\0\0", 2), # 2 = Green
        (b"Ocean\0\0\0\0\0\0\0\0\0\0\0", 1), # 1 = Blue
        (b"Volcano\0\0\0\0\0\0\0\0\0", 4)  # 4 = Red
    ]
    with open(output, 'wb') as f:
        f.write(b'PACK')
        f.write(struct.pack('<H', 1)) # Version
        f.write(struct.pack('<H', 3)) # 3 Backgrounds
        for bg in bgs:
            f.write(bg[0])
            f.write(struct.pack('<B', bg[1]))


if __name__ == "__main__":
    os.makedirs("content", exist_ok=True)
    assets_dir = "./assets"
    if os.path.exists(assets_dir):
        for item in os.listdir(assets_dir):
            char_path = os.path.join(assets_dir, item)
            if os.path.isdir(char_path):
                pack_character(char_path, f"content/{item}.chr")
    pack_core()
    print("Done! All files generated in ./content")
    