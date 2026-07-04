import os
import struct

def pack_character(char_folder, output_file):
    print(f"Packing {char_folder} into {output_file}...")
    stats_path = os.path.join(char_folder, 'stats.txt')
    moves_path = os.path.join(char_folder, 'moves.txt')
    sprite_path = os.path.join(char_folder, 'sprite.raw')
    
    name = os.path.basename(char_folder).encode('ascii').ljust(16, b'\0')[:16]
    max_hp, hp, base_atk, base_def = 100, 100, 10, 10
    
    if os.path.exists(stats_path):
        with open(stats_path, 'r') as f:
            for line in f:
                if '=' in line:
                    k, v = line.strip().split('=')
                    if k == 'MAX_HP': max_hp = hp = int(v)
                    if k == 'BASE_ATK': base_atk = int(v)
                    if k == 'BASE_DEF': base_def = int(v)
                    
    moves = []
    if os.path.exists(moves_path):
        with open(moves_path, 'r') as f:
            for line in f:
                if line.startswith('#') or not line.strip(): continue
                parts = line.strip().split(',')
                m_name = parts[0].encode('ascii').ljust(16, b'\0')[:16]
                moves.append((m_name, int(parts[1]), int(parts[2])))
    
    while len(moves) < 4: moves.append((b"None\0\0\0\0\0\0\0\0\0\0\0\0", 0, 0))
        
    sprite_data = bytearray([16] * 4096) # 16 = Black square fallback
    if os.path.exists(sprite_path):
        with open(sprite_path, 'rb') as f:
            read_data = f.read(4096)
            sprite_data[:len(read_data)] = read_data
            
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
        f.write(struct.pack('<iiiiii', max_hp, hp, base_atk, base_def, 0, 0))
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