# Z.AMP — turns the raw interior renders into the app's image assets (Resources/Interior/).
# Adds a lens-style bloom to the light-only layers (heaters, transformer glow, LED), done in linear
# light, then writes JPEGs. Usage: python make_assets.py <render-dir> <out-dir>
import sys, os, numpy as np
from PIL import Image, ImageFilter

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)

def to_lin(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)

def to_srgb(a):
    a = np.clip(a, 0, 1)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055)

def blur_lin(lin, radius):
    # Blur a gamma-encoded 8-bit copy (PIL filters 8-bit RGB) and return it in linear light.
    im = Image.fromarray((np.clip(lin, 0, 1) ** (1 / 2.2) * 255 + 0.5).astype(np.uint8))
    out = np.asarray(im.filter(ImageFilter.GaussianBlur(radius))).astype(np.float32) / 255
    return out ** 2.2

for name in sorted(os.listdir(src)):
    if not name.endswith(".jpg"):
        continue
    img = np.asarray(Image.open(os.path.join(src, name)).convert("RGB")).astype(np.float32) / 255
    if not name.startswith("base_"):
        lin = to_lin(img)
        w = lin.shape[1]
        lin = lin + 0.6 * blur_lin(lin, w * 0.005) + 0.45 * blur_lin(lin, w * 0.02)
        img = to_srgb(lin)
    Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(dst, name), quality=88, optimize=True, progressive=False)
    print("wrote", name)
