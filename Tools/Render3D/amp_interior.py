# Z.AMP — renders the "behind the glass" amp interior (valves, transformers, chassis) with Blender
# Cycles, as separate light layers that the app mixes at run time:
#   base_<valve>.jpg  : room light + reflections, heaters off   (one per power-valve type)
#   pre.jpg           : light from the preamp valve heaters only
#   pow_<valve>.jpg   : light from the power valve heaters only (one per power-valve type)
#   trafo.jpg         : warm glow of the transformer windings only
#   led.jpg           : purple LED edge light only
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
    M["mica"] = mat("mica", (0.55, 0.52, 0.46), rough=0.5, trans=0.1, alpha=1.0)
    M["getter"] = mat("getter", (0.6, 0.6, 0.62), metal=1.0, rough=0.06)
    M["rod"] = mat("rod", (0.55, 0.55, 0.57), metal=1.0, rough=0.3)
    M["bakelite"] = mat("bakelite", (0.03, 0.025, 0.02), rough=0.35, coat=0.5)
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
def power_valve(x, y, kind, M, idx):
    """Octal power valve: bakelite base, glass envelope (shape per type), plates, mica, getter, heater."""
    shapes = {
        #        radius, glass height, base height, shoulder (KT88-style), profile tweak
        "6L6":  (1.55, 9.2, 2.4, 0.0),
        "EL34": (1.65, 9.0, 2.4, 0.0),
        "6V6":  (1.40, 7.4, 2.3, 0.0),
        "EL84": (1.10, 6.8, 0.0, 0.0),
        "KT88": (1.80, 10.6, 2.6, 1.0),
    }
    r, gh, bh, shoulder = shapes[kind]
    z0 = 0.35  # socket height
    cyl(f"socket{idx}", (x, y, 0), r * 1.25, z0, M["socket"])
    if bh > 0:
        lathe(f"base{idx}", [(0.0, 0), (r * 0.97, 0), (r * 1.0, 0.15), (r * 1.0, bh - 0.15), (r * 0.9, bh), (0.0, bh)], (x, y, z0), M["bakelite"])
    else:  # miniature: pins only
        for k in range(9):
            a = 2 * math.pi * k / 9
            cyl(f"pin{idx}_{k}", (x + 0.6 * r * math.cos(a), y + 0.6 * r * math.sin(a), 0), 0.04, z0 + 0.3, M["rod"], 8)
    zg = z0 + bh
    # glass profile (outer); dome on top
    prof = [(0.0, 0.0), (r * 0.92, 0.0), (r, 0.25)]
    if shoulder:
        prof += [(r, gh * 0.48), (r * 0.82, gh * 0.62), (r * 0.80, gh * 0.85)]
        top_r = r * 0.80
    else:
        prof += [(r, gh * 0.83)]
        top_r = r
    hz = prof[-1][1]
    for k in range(1, 9):
        a = k / 8 * math.pi / 2
        prof.append((top_r * math.cos(a), hz + (gh - hz) * math.sin(a)))
    prof[-1] = (0.0, gh)
    lathe(f"glass{idx}", prof, (x, y, zg), M["glass"])
    # getter flash: mirrored inner top
    gprof = [(top_r * 0.95 * math.cos(a), hz + (gh - hz) * 0.95 * math.sin(a)) for a in [k / 8 * math.pi / 2 for k in range(0, 9)]]
    gprof = [(top_r * 0.95, hz - gh * 0.05)] + gprof
    gprof[-1] = (0.0, hz + (gh - hz) * 0.95)
    lathe(f"getter{idx}", gprof, (x, y, zg), M["getter"])
    # plates
    ph = gh * (0.42 if not shoulder else 0.38)
    pz = zg + gh * 0.22
    pw = r * 1.15
    for side in (-1, 1):
        box(f"plate{idx}{side}", (x, y + side * r * 0.32, pz + ph / 2), (pw, 0.12, ph), M["plate"])
        box(f"wing{idx}{side}", (x + side * pw * 0.5, y, pz + ph / 2), (0.12, r * 0.36, ph), M["plate"])
    # ribs on the plate front
    for k in (-1, 1):
        box(f"rib{idx}{k}", (x + k * pw * 0.22, y - r * 0.32 - 0.07, pz + ph / 2), (0.08, 0.06, ph * 0.96), M["plate_light"])
    # mica spacers
    for mz in (pz - 0.12, pz + ph + 0.05):
        cyl(f"mica{idx}{mz:.2f}", (x, y, mz), r * 0.93, 0.06, M["mica"])
    # support rods
    for sx in (-0.75, 0.75):
        cyl(f"rod{idx}{sx}", (x + sx * r, y, zg + 0.1), 0.03, gh * 0.75, M["rod"], 8)
    # heater: visible at top/bottom of the plate structure and through the gap between plates
    cyl(f"heater{idx}", (x, y, pz - 0.35), 0.12, ph + 0.7, M["heater_pow"], 16)
    for hz2 in (pz - 0.2, pz + ph + 0.13):
        cyl(f"hglow{idx}{hz2:.2f}", (x, y, hz2 - 0.04), 0.2, 0.08, M["heater_pow"], 16)
    # printed type on the glass (thin white text just outside the envelope)
    bpy.ops.object.text_add(location=(x, y - r - 0.02, zg + gh * 0.16), rotation=(math.pi / 2, 0, 0))
    t = bpy.context.object
    t.data.body = kind
    t.data.align_x = "CENTER"
    t.data.size = r * 0.36
    link(t, M["print"])

def preamp_valve(x, y, M, idx):
    r, gh, z0 = 1.05, 5.2, 0.35
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
    # dual triode: two plate assemblies stacked side by side
    ph, pz = gh * 0.36, zg + gh * 0.3
    for sx in (-0.42, 0.42):
        for side in (-1, 1):
            box(f"pplate{idx}{sx}{side}", (x + sx * r, y + side * r * 0.25, pz + ph / 2), (r * 0.62, 0.08, ph), M["plate"])
        cyl(f"pheater{idx}{sx}", (x + sx * r, y, pz + 0.1), 0.06, ph - 0.2, M["heater_pre"], 12)
        box(f"pwing{idx}{sx}", (x + sx * r - 0.31 * r * (1 if sx > 0 else -1), y, pz + ph / 2), (0.06, r * 0.28, ph), M["plate"])
        box(f"pwing2{idx}{sx}", (x + sx * r + 0.31 * r * (1 if sx > 0 else -1), y, pz + ph / 2), (0.06, r * 0.28, ph), M["plate"])
    for mz in (pz - 0.1, pz + ph + 0.04):
        cyl(f"pmica{idx}{mz:.2f}", (x, y, mz), r * 0.92, 0.05, M["mica"])

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
def build(kind):
    sc = reset()
    M = materials()
    # chassis deck and back wall
    deck = box("deck", (0, -10, -0.5), (170, 60, 1.0), M["deck"])
    box("wall", (0, 13.5, 15), (170, 1.0, 40), M["wall"])
    transformer("pt", -24.0, 3.5, 8.6, 7.0, 9.0, M)
    transformer("ot", 24.5, 3.5, 7.4, 6.0, 8.0, M)
    filter_cap(-18.3, 8.0, M)
    filter_cap(20.0, 9.5, M, h=5.0, r=1.2)
    for i, x in enumerate((-14.8, -10.9, -7.0, -3.1)):
        preamp_valve(x, 1.5, M, i)
    for i, x in enumerate((2.6, 7.4, 12.2, 17.0)):
        power_valve(x, 2.0, kind, M, i)

    # camera: front-on, slightly above, like looking through the glass
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 44.0
    cam_data.sensor_width = 36
    cam_data.dof.use_dof = True
    cam_data.dof.aperture_fstop = 5.6
    cam_data.dof.focus_distance = 92.0
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (0.0, -92.0, 16.0)
    cam.rotation_euler = (math.radians(90.0 - math.degrees(math.atan2(16.0 - 6.6, 94.0))), 0, 0)
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
        area("key", (-20, -40, 40), (math.radians(50), 0, math.radians(-20)), 30, 9000, (1.0, 0.97, 0.92)),
        area("fill", (35, -50, 8), (math.radians(85), 0, math.radians(35)), 20, 1500, (0.85, 0.9, 1.0)),
        area("rim", (0, 11, 25), (math.radians(-30), 0, 0), 60, 800, (1.0, 1.0, 1.0), size_y=4),
        # tall soft strips left and right of the camera: vertical highlights on the glass envelopes
        area("stripL", (-30, -60, 10), (math.radians(90), 0, math.radians(-26)), 3, 2500, (1.0, 0.98, 0.95), size_y=30),
        area("stripR", (30, -60, 10), (math.radians(90), 0, math.radians(26)), 3, 1800, (0.9, 0.95, 1.0), size_y=30),
    ]
    purple = (0.62, 0.25, 1.0)
    L["led"] = [
        area("led_bottom", (0, -9.6, 0.6), (math.radians(80), 0, 0), 110, 350, purple, size_y=0.4),
        area("led_top", (0, -9.6, 21), (math.radians(110), 0, 0), 110, 250, purple, size_y=0.4),
        area("led_left", (-33, -9.6, 10), (math.radians(90), math.radians(-30), 0), 0.4, 40, purple, size_y=20),
        area("led_right", (33, -9.6, 10), (math.radians(90), math.radians(30), 0), 0.4, 40, purple, size_y=20),
    ]
    return sc, M, L

def render_pass(sc, M, L, name, room=False, led=False, pre=0.0, pow_=0.0, coil=0.0):
    for o in L["room"]:
        o.hide_render = not room
    for o in L["led"]:
        o.hide_render = not led
    M["heater_pre"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = pre
    M["heater_pow"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = pow_
    M["coil"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = coil
    sc.render.filepath = os.path.join(OUT, name + ".jpg")
    bpy.ops.render.render(write_still=True)
    print("rendered", sc.render.filepath, flush=True)

HEATER = 220.0
for i, kind in enumerate(POWER_TYPES):
    want = lambda n: ONLY is None or n in ONLY
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
