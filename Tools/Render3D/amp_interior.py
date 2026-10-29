# Z.AMP — renders the "behind the glass" amp interior (valves, transformers, chassis) with Blender
# Cycles, as separate light layers that the app mixes at run time:
#   base_<valve>.jpg  : room light + reflections, heaters off   (one per power-valve type)
#   pre.jpg           : light from the preamp valve heaters only
#   pow_<valve>.jpg   : light from the power valve heaters only (one per power-valve type)
#   trafo.jpg         : warm glow of the transformer windings only
#   led.jpg           : purple LED edge light only
#   v1_<type>.jpg     : V1 (first preamp valve) of each preamp type, room light   (small patches)
#   v1g_<type>.jpg    : the same, preamp heater light only
# All layers use the "Standard" view transform so they can be added in linear light.
# Original artwork, procedurally modelled. Run:  python amp_interior.py <outdir> [scale] [samples]
import bpy, bmesh, math, sys, os
from mathutils import Vector

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
SCALE = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
SAMPLES = int(sys.argv[3]) if len(sys.argv) > 3 else 64
ONLY = sys.argv[4].split(",") if len(sys.argv) > 4 else None
os.makedirs(OUT, exist_ok=True)

RES_X, RES_Y = 3000, 1000         # full resolution (aspect 3 : 1); the app crops to the glass shape
POWER_TYPES = ["6L6", "EL34", "6V6", "EL84", "KT88"]

# ----------------------------------------------------------------------------- scene
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = SAMPLES
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 12
    sc.cycles.transmission_bounces = 12
    sc.cycles.glossy_bounces = 6
    sc.render.resolution_x = int(RES_X * SCALE)
    sc.render.resolution_y = int(RES_Y * SCALE)
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0
    sc.render.image_settings.file_format = "JPEG"
    sc.render.image_settings.quality = 92
    world = bpy.data.worlds.new("World")
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.0
    return sc

def mat(name, base=(0.5, 0.5, 0.5), metal=0.0, rough=0.5, trans=0.0, ior=1.45, emit=None, aniso=0.0, alpha=1.0, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*base, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    p.inputs["Transmission Weight"].default_value = trans
    p.inputs["IOR"].default_value = ior
    p.inputs["Anisotropic"].default_value = aniso
    p.inputs["Alpha"].default_value = alpha
    p.inputs["Coat Weight"].default_value = coat
    if emit is not None:
        p.inputs["Emission Color"].default_value = (*emit[0], 1)
        p.inputs["Emission Strength"].default_value = emit[1]
    return m

def link(obj, m):
    obj.data.materials.clear()
    obj.data.materials.append(m)
    return obj

def smooth(obj):
    for poly in obj.data.polygons:
        poly.use_smooth = True
    return obj

def box(name, loc, size, m, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(location=loc)
    o = bpy.context.object
    o.name = name
    o.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    bpy.ops.object.transform_apply(scale=True)
    if bevel > 0:
        b = o.modifiers.new("bevel", "BEVEL")
        b.width = bevel
        b.segments = 4
        smooth(o)
        o.data.shade_auto_smooth() if hasattr(o.data, "shade_auto_smooth") else None
    return link(o, m)

def cyl(name, loc, r, h, m, verts=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=h, location=(loc[0], loc[1], loc[2] + h / 2))
    o = bpy.context.object
    o.name = name
    smooth(o)
    return link(o, m)

def lathe(name, profile, loc, m, segs=64):
    """Revolves (radius, z) points around the vertical axis."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        rings.append([bm.verts.new((r * math.cos(a), r * math.sin(a), z)) for r, z in profile])
    for i in range(segs):
        r0, r1 = rings[i], rings[(i + 1) % segs]
        for j in range(len(profile) - 1):
            bm.faces.new((r0[j], r1[j], r1[j + 1], r0[j + 1]))
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    o.location = loc
    smooth(o)
    return link(o, m)

# ----------------------------------------------------------------------------- materials
def materials():
    M = {}
    M["glass"] = mat("glass", (1, 1, 1), rough=0.02, trans=1.0, ior=1.47)
    M["plate"] = mat("plate", (0.22, 0.22, 0.23), metal=0.85, rough=0.42)
    M["plate_light"] = mat("plate_light", (0.35, 0.35, 0.36), metal=1.0, rough=0.35)
    M["plate_black"] = mat("plate_black", (0.025, 0.025, 0.028), metal=0.3, rough=0.55)
    M["plate_bright"] = mat("plate_bright", (0.5, 0.51, 0.54), metal=1.0, rough=0.32)
    M["mica"] = mat("mica", (0.55, 0.52, 0.46), rough=0.5, trans=0.1, alpha=1.0)
    M["getter"] = mat("getter", (0.6, 0.6, 0.62), metal=1.0, rough=0.06)
    M["rod"] = mat("rod", (0.55, 0.55, 0.57), metal=1.0, rough=0.3)
    M["bakelite"] = mat("bakelite", (0.03, 0.025, 0.02), rough=0.35, coat=0.5)
    M["bakelite_brown"] = mat("bakelite_brown", (0.09, 0.04, 0.018), rough=0.35, coat=0.5)
    M["socket"] = mat("socket", (0.05, 0.045, 0.04), rough=0.6)
    M["chrome"] = mat("chrome", (0.9, 0.9, 0.92), metal=1.0, rough=0.08)
    M["bell"] = mat("bell", (0.02, 0.02, 0.022), rough=0.42, coat=0.3)
    M["lam"] = mat("lam", (0.07, 0.07, 0.075), metal=0.9, rough=0.45)
    M["deck"] = mat("deck", (0.75, 0.76, 0.78), metal=1.0, rough=0.22, aniso=0.6)
    M["wall"] = mat("wall", (0.035, 0.035, 0.04), metal=0.6, rough=0.45)
    M["cap"] = mat("cap", (0.8, 0.8, 0.82), metal=1.0, rough=0.25, aniso=0.5)
    M["cap_sleeve"] = mat("cap_sleeve", (0.01, 0.012, 0.02), rough=0.3, coat=0.6)
    M["print"] = mat("print", (0.9, 0.9, 0.88), rough=0.6)
    # Light sources (their strength is set per pass)
    M["heater_pre"] = mat("heater_pre", (0, 0, 0), emit=((1.0, 1.0, 1.0), 0.0))
    M["heater_pow"] = mat("heater_pow", (0, 0, 0), emit=((1.0, 1.0, 1.0), 0.0))
    M["coil"] = mat("coil", (0.45, 0.2, 0.08), metal=0.8, rough=0.4, emit=((1.0, 0.35, 0.1), 0.0))
    perforated_wall(M["wall"])
    lamination(M["lam"])
    return M

def perforated_wall(m):
    """Dark steel back panel with a staggered grid of round vent holes (holes are black)."""
    nt = m.node_tree
    n, l = nt.nodes, nt.links
    p = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    sep = n.new("ShaderNodeSeparateXYZ")
    l.new(tc.outputs["Object"], sep.inputs[0])
    pitch = 0.55
    def math_node(op, a, b=None):
        node = n.new("ShaderNodeMath")
        node.operation = op
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                node.inputs[i].default_value = v
            else:
                l.new(v, node.inputs[i])
        return node.outputs[0]
    row = math_node("FLOOR", math_node("DIVIDE", sep.outputs["Z"], pitch * 0.866))
    shift = math_node("MULTIPLY", math_node("MODULO", row, 2.0), pitch * 0.5)
    fx = math_node("SUBTRACT", math_node("FRACT", math_node("DIVIDE", math_node("ADD", sep.outputs["X"], shift), pitch)), 0.5)
    fz = math_node("SUBTRACT", math_node("FRACT", math_node("DIVIDE", sep.outputs["Z"], pitch * 0.866)), 0.5)
    d = math_node("SQRT", math_node("ADD", math_node("MULTIPLY", fx, fx), math_node("MULTIPLY", fz, fz)))
    hole = math_node("LESS_THAN", d, 0.3)
    mix = n.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    l.new(hole, mix.inputs["Factor"])
    mix.inputs[6].default_value = (0.035, 0.035, 0.04, 1)
    mix.inputs[7].default_value = (0.0, 0.0, 0.0, 1)
    l.new(mix.outputs[2], p.inputs["Base Color"])
    rough = math_node("ADD", math_node("MULTIPLY", hole, 0.55), 0.45)
    l.new(rough, p.inputs["Roughness"])

def lamination(m):
    nt = m.node_tree
    n, l = nt.nodes, nt.links
    tc = n.new("ShaderNodeTexCoord")
    wave = n.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "Z"
    wave.inputs["Scale"].default_value = 14.0
    wave.inputs["Distortion"].default_value = 0.4
    l.new(tc.outputs["Object"], wave.inputs["Vector"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.6
    l.new(wave.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], n["Principled BSDF"].inputs["Normal"])

# ----------------------------------------------------------------------------- parts
# Each valve type has its own look (glass shape, base, plate colour and length, getter), so the
# selectors visibly change the valves. Real valves of these types differ in much the same ways.
POWER = {
    #       radius, glass h, base h, glass shape, base material, plate material, plate height (x gh), getter
    "6L6":  dict(r=1.55, gh=9.2,  bh=2.4, shape="straight", base="bakelite",       plate="plate",        ph=0.42, getter="dome"),
    "EL34": dict(r=1.62, gh=9.8,  bh=2.5, shape="straight", base="bakelite_brown", plate="plate_black",  ph=0.48, getter="halo"),
    "6V6":  dict(r=1.40, gh=7.4,  bh=2.1, shape="coke",     base="bakelite",       plate="plate",        ph=0.36, getter="dome"),
    "EL84": dict(r=1.10, gh=6.6,  bh=0.0, shape="mini",     base=None,             plate="plate",        ph=0.42, getter="dome"),
    "KT88": dict(r=1.85, gh=10.8, bh=2.7, shape="shoulder", base="bakelite",       plate="plate_bright", ph=0.36, getter="dome"),
}
PREAMP = {
    #        plate material, plate height (x gh), mica spacers, plate ribs
    "12AX7": dict(plate="plate",        ph=0.32, micas=2, ribs=False),
    "5751":  dict(plate="plate_black",  ph=0.34, micas=3, ribs=False),
    "12AY7": dict(plate="plate",        ph=0.46, micas=2, ribs=False),
    "12AT7": dict(plate="plate_bright", ph=0.28, micas=2, ribs=True),
}
PREAMP_TYPES = list(PREAMP)

def dome(prof, top_r, hz, gh):
    for k in range(1, 9):
        a = k / 8 * math.pi / 2
        prof.append((top_r * math.cos(a), hz + (gh - hz) * math.sin(a)))
    prof[-1] = (0.0, gh)
    return prof

def print_type(name, text, x, y, z, size, M):
    """Type name printed on the front of the glass (thin white text just outside the envelope)."""
    bpy.ops.object.text_add(location=(x, y, z), rotation=(math.pi / 2, 0, 0))
    t = bpy.context.object
    t.name = name
    t.data.body = text
    t.data.align_x = "CENTER"
    t.data.size = size
    link(t, M["print"])

def power_valve(x, y, kind, M, idx):
    """Power valve: base (octal) or pins (miniature), glass envelope, plates, mica, getter, heater."""
    V = POWER[kind]
    r, gh, bh = V["r"], V["gh"], V["bh"]
    z0 = 0.35  # socket height
    cyl(f"socket{idx}", (x, y, 0), r * 1.25, z0, M["socket"])
    if bh > 0:
        lathe(f"base{idx}", [(0.0, 0), (r * 0.97, 0), (r * 1.0, 0.15), (r * 1.0, bh - 0.15), (r * 0.9, bh), (0.0, bh)], (x, y, z0), M[V["base"]])
    else:  # miniature: pins only
        for k in range(9):
            a = 2 * math.pi * k / 9
            cyl(f"pin{idx}_{k}", (x + 0.6 * r * math.cos(a), y + 0.6 * r * math.sin(a), 0), 0.04, z0 + 0.3, M["rod"], 8)
    zg = z0 + bh
    shape = V["shape"]
    if shape == "shoulder":   # KT88: wide body stepping in to a narrower top
        prof = [(0.0, 0.0), (r * 0.92, 0.0), (r, 0.25), (r, gh * 0.48), (r * 0.82, gh * 0.62), (r * 0.80, gh * 0.85)]
        top_r = r * 0.80
    elif shape == "coke":     # 6V6 "coke bottle": round belly, narrow neck
        prof = [(0.0, 0.0), (r * 0.80, 0.0), (r * 0.95, 0.25), (r * 1.06, gh * 0.30), (r * 1.08, gh * 0.45),
                (r * 0.96, gh * 0.60), (r * 0.76, gh * 0.74), (r * 0.72, gh * 0.84)]
        top_r = r * 0.72
    else:                     # straight bottle (6L6, EL34) or miniature (EL84)
        prof = [(0.0, 0.0), (r * 0.92, 0.0), (r, 0.25), (r, gh * 0.83)]
        top_r = r
    hz = prof[-1][1]
    prof = dome(prof, top_r, hz, gh)
    if shape == "mini":       # exhaust tip on top of miniature valves
        prof[-1] = (0.14, gh)
        prof += [(0.14, gh + 0.4), (0.0, gh + 0.48)]
    lathe(f"glass{idx}", prof, (x, y, zg), M["glass"])
    if V["getter"] == "dome":  # mirrored getter flash over the inside of the top
        gprof = [(top_r * 0.95 * math.cos(a), hz + (gh - hz) * 0.95 * math.sin(a)) for a in [k / 8 * math.pi / 2 for k in range(0, 9)]]
        gprof = [(top_r * 0.95, hz - gh * 0.05)] + gprof
        gprof[-1] = (0.0, hz + (gh - hz) * 0.95)
        lathe(f"getter{idx}", gprof, (x, y, zg), M["getter"])
    else:                      # halo getter: a ring above the plates, the dome stays clear
        bpy.ops.mesh.primitive_torus_add(major_radius=r * 0.62, minor_radius=0.07, location=(x, y, zg + hz - gh * 0.04))
        link(smooth(bpy.context.object), M["getter"])
        cyl(f"halo_post{idx}", (x - r * 0.62, y, zg + gh * 0.7), 0.03, hz - gh * 0.74, M["rod"], 8)
    # plates
    ph = gh * V["ph"]
    pz = zg + gh * (0.22 if shape != "coke" else 0.20)
    pw = r * 1.15
    pm = M[V["plate"]]
    for side in (-1, 1):
        box(f"plate{idx}{side}", (x, y + side * r * 0.32, pz + ph / 2), (pw, 0.12, ph), pm)
        box(f"wing{idx}{side}", (x + side * pw * 0.5, y, pz + ph / 2), (0.12, r * 0.36, ph), pm)
    for k in (-1, 1):  # ribs on the plate front
        box(f"rib{idx}{k}", (x + k * pw * 0.22, y - r * 0.32 - 0.07, pz + ph / 2), (0.08, 0.06, ph * 0.96), M["plate_light"])
    for mz in (pz - 0.12, pz + ph + 0.05):
        cyl(f"mica{idx}{mz:.2f}", (x, y, mz), r * 0.93, 0.06, M["mica"])
    for sx in (-0.75, 0.75):
        cyl(f"rod{idx}{sx}", (x + sx * r, y, zg + 0.1), 0.03, gh * 0.75, M["rod"], 8)
    # heater: visible at top/bottom of the plate structure and through the gap between plates
    cyl(f"heater{idx}", (x, y, pz - 0.35), 0.12, ph + 0.7, M["heater_pow"], 16)
    for hz2 in (pz - 0.2, pz + ph + 0.13):
        cyl(f"hglow{idx}{hz2:.2f}", (x, y, hz2 - 0.04), 0.2, 0.08, M["heater_pow"], 16)
    belly = r * (1.08 if shape == "coke" else 1.0)
    print_type(f"print{idx}", kind, x, y - belly - 0.02, zg + gh * 0.13, r * 0.36, M)

PRE_R, PRE_GH = 1.05, 5.2

def preamp_valve(x, y, M, idx, kind="12AX7"):
    P = PREAMP[kind]
    r, gh, z0 = PRE_R, PRE_GH, 0.35
    cyl(f"psocket{idx}", (x, y, 0), r * 1.35, z0, M["socket"])
    for k in range(9):
        a = 2 * math.pi * k / 9
        cyl(f"ppin{idx}_{k}", (x + 0.62 * r * math.cos(a), y + 0.62 * r * math.sin(a), 0), 0.035, z0 + 0.35, M["rod"], 8)
    zg = z0 + 0.2
    prof = [(0.0, 0.0), (r * 0.85, 0.0), (r, 0.3), (r, gh * 0.84)]
    for k in range(1, 9):
        a = k / 8 * math.pi / 2
        prof.append((r * math.cos(a), gh * 0.84 + gh * 0.16 * math.sin(a)))
    prof[-1] = (0.12, gh)
    prof += [(0.12, gh + 0.35), (0.0, gh + 0.42)]  # exhaust tip
    lathe(f"pglass{idx}", prof, (x, y, zg), M["glass"])
    gprof = [(r * 0.94 * math.cos(a), gh * 0.84 + gh * 0.15 * math.sin(a)) for a in [k / 8 * math.pi / 2 for k in range(0, 9)]]
    gprof = [(r * 0.94, gh * 0.78)] + gprof
    gprof[-1] = (0.0, gh * 0.99)
    lathe(f"pgetter{idx}", gprof, (x, y, zg), M["getter"])
    # dual triode: two plate assemblies side by side
    ph = gh * P["ph"]
    pz = zg + gh * 0.62 - ph  # plates hang from the same top mica, longer ones reach further down
    pm = M[P["plate"]]
    for sx in (-0.42, 0.42):
        for side in (-1, 1):
            box(f"pplate{idx}{sx}{side}", (x + sx * r, y + side * r * 0.25, pz + ph / 2), (r * 0.62, 0.08, ph), pm)
        if P["ribs"]:
            for k in (-1, 0, 1):
                box(f"prib{idx}{sx}{k}", (x + sx * r + k * r * 0.17, y - r * 0.25 - 0.05, pz + ph / 2), (0.05, 0.04, ph * 0.94), M["plate_light"])
        cyl(f"pheater{idx}{sx}", (x + sx * r, y, pz + 0.1), 0.06, ph - 0.2, M["heater_pre"], 12)
        box(f"pwing{idx}{sx}", (x + sx * r - 0.31 * r * (1 if sx > 0 else -1), y, pz + ph / 2), (0.06, r * 0.28, ph), pm)
        box(f"pwing2{idx}{sx}", (x + sx * r + 0.31 * r * (1 if sx > 0 else -1), y, pz + ph / 2), (0.06, r * 0.28, ph), pm)
    mz = [pz - 0.1, pz + ph + 0.04]
    if P["micas"] == 3:
        mz.append(pz + ph * 0.5)
    for z in mz:
        cyl(f"pmica{idx}{z:.2f}", (x, y, z), r * 0.92, 0.05, M["mica"])
    print_type(f"pprint{idx}", kind, x, y - r - 0.02, zg + gh * 0.08, r * 0.30, M)

def transformer(name, x, y, w, d, h, M):
    """Laminated core between two black end bells, with a glowing coil gap (glow pass only)."""
    bell_w = w * 0.27
    core_w = w - 2 * bell_w
    box(f"{name}_core", (x, y, h / 2), (core_w, d * 0.92, h * 0.94), M["lam"])
    for s in (-1, 1):
        box(f"{name}_bell{s}", (x + s * (core_w / 2 + bell_w / 2), y, h / 2), (bell_w, d, h), M["bell"], bevel=0.45)
        # coil visible in a slot between bell and core
        box(f"{name}_coil{s}", (x + s * (core_w / 2 + 0.02), y, h * 0.5), (0.18, d * 0.7, h * 0.55), M["coil"])
    for sx in (-1, 1):
        for sz in (-1, 1):
            bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.32, depth=0.25,
                                                location=(x + sx * core_w * 0.32, y - d * 0.46 - 0.1, h / 2 + sz * h * 0.36),
                                                rotation=(math.pi / 2, 0, 0))
            link(bpy.context.object, M["chrome"])
    # mounting feet
    for sx in (-1, 1):
        box(f"{name}_foot{sx}", (x + sx * (w / 2 + 0.6), y, 0.08), (1.4, d * 0.9, 0.16), M["bell"], bevel=0.05)

def filter_cap(x, y, M, h=6.0, r=1.4):
    cyl("capbody", (x, y, 0), r, h, M["cap_sleeve"])
    cyl("captop", (x, y, h), r * 0.98, 0.18, M["cap"])
    cyl("capring", (x, y, 0), r * 1.03, 0.5, M["cap"])

# ----------------------------------------------------------------------------- build
# Units are centimetres. The glass is at y = GLASS_Y; the valves stand about 9 cm behind it and
# the back wall about 10 cm behind them, as in a real head (the chassis is ~20 cm deep).
GLASS_Y = -7.0
PRE_X = (-15.6, -12.2, -8.8, -5.4)   # V1 is the leftmost, next to the power transformer
POW_X = (-0.2, 4.6, 9.4, 14.2)

def build(kind, v1="12AX7"):
    sc = reset()
    M = materials()
    # chassis: deck from the glass to the back wall, side walls
    box("deck", (0, (GLASS_Y + 10.5) / 2, -0.5), (64, 10.5 - GLASS_Y, 1.0), M["deck"])
    box("wall", (0, 10.0, 14), (64, 1.0, 30), M["wall"])
    for sx in (-1, 1):
        box(f"side{sx}", (sx * 30.5, (GLASS_Y + 10.5) / 2, 14), (1.0, 10.5 - GLASS_Y, 30), M["wall"])
    transformer("pt", -21.9, 3.2, 8.6, 7.0, 9.0, M)
    transformer("ot", 22.3, 3.4, 7.4, 6.0, 8.0, M)
    filter_cap(-17.0, 7.6, M)
    filter_cap(17.6, 7.8, M, h=5.0, r=1.2)
    for i, x in enumerate(PRE_X):
        preamp_valve(x, 1.5, M, i, v1 if i == 0 else "12AX7")
    for i, x in enumerate(POW_X):
        power_valve(x, 2.0, kind, M, i)

    # camera: a viewer about 45 cm in front of the glass, eyes a little above the valve plates
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 33.5
    cam_data.sensor_width = 36
    cam_data.dof.use_dof = True
    cam_data.dof.aperture_fstop = 4.0
    cam_data.dof.focus_distance = 52.0
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (0.0, -50.0, 9.5)
    cam.rotation_euler = (math.radians(90.0 - math.degrees(math.atan2(9.5 - 6.0, 52.0))), 0, 0)
    sc.camera = cam

    # lights: each pass enables one group
    L = {}
    def area(name, loc, rot, size, energy, color, shape="RECTANGLE", size_y=None):
        d = bpy.data.lights.new(name, "AREA")
        d.shape = shape
        d.size = size
        if size_y is not None:
            d.size_y = size_y
        d.energy = energy
        d.color = color
        o = bpy.data.objects.new(name, d)
        bpy.context.collection.objects.link(o)
        o.location = loc
        o.rotation_euler = rot
        return o
    L["room"] = [
        area("key", (-14, -36, 34), (math.radians(48), 0, math.radians(-18)), 24, 6000, (1.0, 0.97, 0.92)),
        area("fill", (26, -42, 8), (math.radians(85), 0, math.radians(32)), 16, 1100, (0.85, 0.9, 1.0)),
        area("rim", (0, 9, 24), (math.radians(-30), 0, 0), 56, 500, (1.0, 1.0, 1.0), size_y=3),
        # tall soft strips left and right of the camera: vertical highlights on the glass envelopes
        area("stripL", (-24, -50, 9), (math.radians(90), 0, math.radians(-26)), 3, 1800, (1.0, 0.98, 0.95), size_y=26),
        area("stripR", (24, -50, 9), (math.radians(90), 0, math.radians(26)), 3, 1300, (0.9, 0.95, 1.0), size_y=26),
    ]
    purple = (0.62, 0.25, 1.0)
    ly = GLASS_Y + 0.4
    L["led"] = [
        area("led_bottom", (0, ly, 0.6), (math.radians(80), 0, 0), 60, 300, purple, size_y=0.4),
        area("led_top", (0, ly, 19), (math.radians(110), 0, 0), 60, 220, purple, size_y=0.4),
        area("led_left", (-29.5, ly, 9), (math.radians(90), math.radians(-30), 0), 0.4, 35, purple, size_y=18),
        area("led_right", (29.5, ly, 9), (math.radians(90), math.radians(30), 0), 0.4, 35, purple, size_y=18),
    ]
    return sc, M, L

def v1_border(sc):
    """Render border (normalised, y up) around V1, snapped outwards to 1 % steps."""
    from bpy_extras.object_utils import world_to_camera_view
    bpy.context.view_layer.update()  # the camera's world matrix must be current
    x, y = PRE_X[0], 1.5
    pts = [Vector((x + dx, y + dy, z)) for dx in (-1.6, 1.6) for dy in (-1.6, 1.6) for z in (-0.3, PRE_GH + 1.4)]
    uv = [world_to_camera_view(sc, sc.camera, p) for p in pts]
    fl = lambda v: math.floor(v * 100) / 100
    ce = lambda v: math.ceil(v * 100) / 100
    return (max(0.0, fl(min(p.x for p in uv))), min(1.0, ce(max(p.x for p in uv))),
            max(0.0, fl(min(p.y for p in uv))), min(1.0, ce(max(p.y for p in uv))))

def render_pass(sc, M, L, name, room=False, led=False, pre=0.0, pow_=0.0, coil=0.0, border=None):
    path = os.path.join(OUT, name + ".jpg")
    if os.path.exists(path) and not FORCE:
        print("exists, skipped", path, flush=True)
        return
    for o in L["room"]:
        o.hide_render = not room
    for o in L["led"]:
        o.hide_render = not led
    M["heater_pre"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = pre
    M["heater_pow"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = pow_
    M["coil"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = coil
    sc.render.use_border = border is not None
    sc.render.use_crop_to_border = border is not None
    if border is not None:
        sc.render.border_min_x, sc.render.border_max_x, sc.render.border_min_y, sc.render.border_max_y = border
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("rendered", path, flush=True)

FORCE = os.environ.get("FORCE") == "1"
HEATER = 220.0
want = lambda n: ONLY is None or n in ONLY
for i, kind in enumerate(POWER_TYPES):
    if not any(want(n) for n in (f"base_{kind}", f"pow_{kind}", "pre", "trafo", "led")):
        continue
    sc, M, L = build(kind)
    if want(f"base_{kind}"):
        render_pass(sc, M, L, f"base_{kind}", room=True)
    if want(f"pow_{kind}"):
        render_pass(sc, M, L, f"pow_{kind}", pow_=HEATER)
    if i == 0:
        if want("pre"):
            render_pass(sc, M, L, "pre", pre=HEATER)
        if want("trafo"):
            render_pass(sc, M, L, "trafo", coil=40.0)
        if want("led"):
            render_pass(sc, M, L, "led", led=True)

# V1 (first preamp valve) in each type, as patches over the base and preamp-glow layers. The
# patch rectangle (normalised, y down) is written to v1_rect.txt; GlassWindow uses the same values.
for pre in PREAMP_TYPES:
    if not any(want(n) for n in (f"v1_{pre}", f"v1g_{pre}")):
        continue
    sc, M, L = build("6L6", v1=pre)
    b = v1_border(sc)
    with open(os.path.join(OUT, "v1_rect.txt"), "w") as f:
        f.write(f"{b[0]:.2f} {1 - b[3]:.2f} {b[1]:.2f} {1 - b[2]:.2f}\n")  # left top right bottom, y down
    print("V1 patch (l t r b, y down):", b[0], 1 - b[3], b[1], 1 - b[2], flush=True)
    if want(f"v1_{pre}"):
        render_pass(sc, M, L, f"v1_{pre}", room=True, border=b)
    if want(f"v1g_{pre}"):
        render_pass(sc, M, L, f"v1g_{pre}", pre=HEATER, border=b)
