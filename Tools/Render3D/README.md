# Amp interior renders

`amp_interior.py` models the inside of the amp seen through the glass front (chassis, perforated back
panel, brushed deck, preamp and power valves, transformers, filter caps) and renders it with Blender
Cycles as separate light layers. `make_assets.py` adds a lens-style bloom to the light-only layers and
writes the JPEGs the app bundles from `Resources/Interior/`.

| File | Contents |
|---|---|
| `base_<valve>.jpg` | room light and reflections, heaters off (one per power-valve type) |
| `pow_<valve>.jpg` | light from the power-valve heaters only |
| `pre.jpg` | light from the preamp-valve heaters only |
| `trafo.jpg` | transformer winding glow only |
| `led.jpg` | purple LED edge light only |

The app (`GlassWindow` in `Source/UI/AmpHead.cpp`) crops the layers to the glass shape and adds them
in linear light: heater layers tinted by the model's heat colour (faint orange → white hot), the
transformer layer on the modern high-gain models, the LED layer while the amp is live. The etched
logo, edge lighting and glass reflections are drawn on top in code. Without the images the app falls
back to the interior drawn in code.

```bash
pip install bpy                      # Blender as a Python module (5.x; needs the matching Python)
python Tools/Render3D/amp_interior.py /tmp/renders 1.0 128      # full size, 128 samples (~1.5 h, 4 cores)
python Tools/Render3D/make_assets.py /tmp/renders Resources/Interior
```

Arguments: output folder, resolution scale (0.4 for quick previews), samples, and optionally a
comma-separated list of layers to render (e.g. `base_6L6,pow_6L6`). Everything is modelled
procedurally; no third-party models, textures or photos are used.
