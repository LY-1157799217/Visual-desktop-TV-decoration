import re, os, io
from PIL import Image

FONT_DIR = "arduino/wallpaper_clock/font"
OUT_DIR  = "arduino/wallpaper_clock/font_alpha"
os.makedirs(OUT_DIR, exist_ok=True)

def extract_h(path):
    data = open(path, 'rb').read().decode('latin1')
    m = re.search(r'\{\s*(.*?)\s*\}\s*;', data, re.DOTALL)
    return bytes(int(t,16) for t in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1)))

FONTS = {"O_3660":((36,60),"orange"), "W_3660":((36,60),"white"), "W_1830":((18,30),"white")}

def stroke_mask(px, orange):
    r,g,b = px
    if orange:
        return (g >= 12 or b >= 12)   # 笔画: 非纯红伪影(g=0)
    return max(r,g,b) >= 60            # 白色: 亮度>=60

def build_alpha(px, w, h, orange):
    mask = [[1 if stroke_mask(px[x,y], orange) else 0 for x in range(w)] for y in range(h)]
    # 去噪: 清除孤立笔画像素(自身+邻域<2)
    clean = [[0]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if mask[y][x]:
                nb = 0
                for dy in (-1,0,1):
                    for dx in (-1,0,1):
                        ny,nx = y+dy, x+dx
                        if 0<=ny<h and 0<=nx<w and mask[ny][nx]:
                            nb += 1
                if nb >= 2:
                    clean[y][x] = 1
    # 纯二值: 笔画255, 背景0 (无边缘半透明, 硬边)
    return [255 if clean[y][x] else 0 for y in range(h) for x in range(w)]

def gen(prefix, size, orange):
    w,h = size
    for i in range(10):
        jpeg = extract_h(f"{FONT_DIR}/{prefix}_i{i}.h")
        img = Image.open(io.BytesIO(jpeg)).convert("RGB")
        px = img.load()
        alpha = build_alpha(px, w, h, orange)
        name = f"A_{prefix}_i{i}"
        with open(f"{OUT_DIR}/{name}.h", "w") as f:
            f.write("#include <pgmspace.h>\n")
            f.write(f"const uint8_t {name}[] PROGMEM = {{\n")
            line=[]
            for a in alpha:
                line.append(f"0x{a:02X}")
                if len(line)==24: f.write("  "+", ".join(line)+",\n"); line=[]
            if line: f.write("  "+", ".join(line)+"\n")
            f.write("};\n")
    print(f"{prefix}: v3 纯二值 完成")

for p,(sz,ct) in FONTS.items():
    gen(p, sz, ct=="orange")
print("全部完成")
