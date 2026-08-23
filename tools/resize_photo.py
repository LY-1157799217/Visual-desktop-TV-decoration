# resize_photo.py — 把任意图片缩放到 240x240 JPEG, 输出到 arduino/photo/data/
# 用法: python resize_photo.py 图1.png 图2.jpg 图3.webp ...
# 说明: 居中裁剪成正方形 -> 缩放到240x240 -> 命名 1.jpg 2.jpg 3.jpg
from PIL import Image
import sys, os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "arduino", "photo", "data")
OUT_DIR = os.path.abspath(OUT_DIR)

def resize(src, dst):
    img = Image.open(src)
    w, h = img.size
    side = min(w, h)                       # 居中裁剪成正方形
    left, top = (w - side) // 2, (h - side) // 2
    img = img.crop((left, top, left + side, top + side))
    img = img.resize((240, 240), Image.LANCZOS).convert("RGB")
    img.save(dst, "JPEG", quality=88)
    print(f"  {os.path.basename(src)} -> {os.path.basename(dst)}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python resize_photo.py 图1 图2 图3 ...")
        print(f"输出目录: {OUT_DIR}")
        sys.exit(1)
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"输出到: {OUT_DIR}\n")
    for i, src in enumerate(sys.argv[1:], start=1):
        if i > 9:
            break
        if not os.path.exists(src):
            print(f"  [跳过] {src} 不存在")
            continue
        resize(src, os.path.join(OUT_DIR, f"{i}.jpg"))
    print("\n完成! 现在 data/ 目录里有 1.jpg 2.jpg ... 可上传到 SPIFFS")
