#!/usr/bin/env python3
"""Generates Z.AMP factory presets (original settings, schema v2).

Each preset only lists parameters that differ from the parameter defaults; everything else
is filled in with defaults so the files stay readable. Output levels ("out.level") are
calibrated with Tools/Render.cpp (`ampforge_render --calibrate`) so presets play at a
similar loudness; the calibrated values are stored in LEVELS below.
"""
import json, os, sys

OUT = os.path.join(os.path.dirname(__file__), "..", "Resources", "FactoryPresets")

# Model indices: 0 Tweed, 1 JC120, 2 AC30, 3 JCM800, 4 Coliseum, 5 MP1, 6 Triple, 7 Dual, 8 Plexi
P = [
 ("Glass Clean", "Clean", "Crystal solid-state clean with the bright switch on and a touch of room.", "factory:am-2x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.45,"amp.mid":0.5,"amp.treble":0.62,"amp.master":0.6,"amp.bright":1,
   "reverb.enabled":1,"reverb.type":0,"reverb.mix":0.22,"reverb.decay":1.4}),
 ("Studio Clean Chorus", "Clean", "The classic stereo-chorus clean, with a subtle slap.", "factory:am-2x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.5,"amp.treble":0.55,"amp.master":0.62,
   "chorus.enabled":1,"chorus.rate":0.7,"chorus.depth":0.45,"chorus.mix":0.45,
   "delay.enabled":1,"delay.time":110,"delay.feedback":0.1,"delay.mix":0.12,"reverb.enabled":1,"reverb.mix":0.15}),
 ("Warm Jazz Clean", "Clean", "Round, dark clean for neck pickups and chord melodies.", "factory:am-1x12-open-edge", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.55,"amp.mid":0.6,"amp.treble":0.32,"amp.master":0.65,
   "reverb.enabled":1,"reverb.type":3,"reverb.mix":0.16,"reverb.decay":1.4,"reverb.tone":0.3}),
 ("Edge of Breakup", "Edge of Breakup", "Clean when you pick lightly, hair and grit when you dig in. Roll back the guitar volume to clean up.", "factory:am-1x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.52,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.65}),
 ("Top Boost Chime", "Edge of Breakup", "Jangly class-A chime, just breaking up: ringing chords, spring reverb.", "factory:am-2x12-open-room", None,
  {"amp.model":2,"amp.gain":0.55,"amp.bass":0.4,"amp.mid":0.5,"amp.treble":0.6,"amp.presence":0.5,"amp.master":0.7,"amp.bright":1,
   "reverb.enabled":1,"reverb.type":2,"reverb.mix":0.2,"reverb.decay":1.8}),
 ("Tweed Blues", "Blues", "Fat, loose vintage drive for single-note blues.", "factory:am-1x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.7,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.55,"amp.presence":0.45,"amp.master":0.7,
   "reverb.enabled":1,"reverb.type":2,"reverb.mix":0.2,"reverb.decay":1.6}),
 ("Texas Drive", "Blues", "Screamer pushing a tweed on the edge.", "factory:am-1x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.45,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.master":0.65,
   "drive.enabled":1,"drive.type":0,"drive.drive":0.35,"drive.tone":0.55,"drive.level":5.0,"reverb.enabled":1,"reverb.mix":0.15}),
 ("Brit Crunch", "Classic Rock", "1980s British amp at crunch: chords ring, single notes bite.", "factory:br-4x12-v25-cap", None,
  {"amp.model":3,"amp.gain":0.35,"amp.bass":0.45,"amp.mid":0.6,"amp.treble":0.6,"amp.presence":0.55,"amp.master":0.6}),
 ("Cranked AC30", "Classic Rock", "Everything up on the class-A amp: thick, compressed and singing.", "factory:am-2x12-open-room", None,
  {"amp.model":2,"amp.gain":0.88,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.5,"amp.presence":0.45,"amp.master":0.75,
   "gate.enabled":1,"gate.threshold":-66}),
 ("British Crunch", "British Crunch", "Hot-rodded British amp at a moderate gain: tight, punchy rhythm.", "factory:br-4x12-h75-cap", None,
  {"amp.model":3,"amp.gain":0.45,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.6,"amp.presence":0.5,"amp.master":0.6}),
 ("Boosted Brit", "British Crunch", "Screamer as a clean boost into the British lead amp: the 80s edge.", "factory:br-4x12-v25-edge", None,
  {"amp.model":3,"amp.gain":0.6,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.58,"amp.presence":0.55,"amp.master":0.6,
   "drive.enabled":1,"drive.drive":0.1,"drive.tone":0.6,"drive.level":8.0,"gate.enabled":1,"gate.threshold":-64}),
 ("Hot Rod Lead", "Lead", "Saturated British lead with tape echo and room.", "factory:br-4x12-h75-cap", None,
  {"amp.model":3,"amp.gain":0.8,"amp.bass":0.45,"amp.mid":0.65,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.6,
   "gate.enabled":1,"gate.threshold":-62,"delay.enabled":1,"delay.type":1,"delay.time":420,"delay.feedback":0.3,"delay.mix":0.22,
   "reverb.enabled":1,"reverb.type":0,"reverb.mix":0.18}),
 ("Singing Lead", "Lead", "Smooth, vocal high-gain lead with mid push from the graphic EQ.", "factory:bt-1x12-closed-cap", None,
  {"amp.model":4,"amp.gain":0.62,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.6,"amp.presence":0.45,"amp.depth":0.3,"amp.master":0.55,
   "eq.enabled":1,"eq.b80":0,"eq.b240":-1,"eq.b750":2,"eq.b2200":1.5,"eq.b6600":-2,
   "smooth.enabled":1,"smooth.amount":0.4,
   "gate.enabled":1,"gate.threshold":-64,"gate.release":200,
   "delay.enabled":1,"delay.sync":1,"delay.division":5,"delay.feedback":0.32,"delay.mix":0.2,"reverb.enabled":1,"reverb.mix":0.2}),
 ("Fusion Lead", "Lead", "Articulate, slightly compressed lead with light chorus.", "factory:md-2x12-closed-cap", None,
  {"amp.model":4,"amp.gain":0.48,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.55,
   "chorus.enabled":1,"chorus.rate":0.4,"chorus.depth":0.3,"chorus.mix":0.3,
   "delay.enabled":1,"delay.time":360,"delay.feedback":0.25,"delay.mix":0.18,"reverb.enabled":1,"reverb.mix":0.16}),
 ("Classic V Rhythm", "High Gain", "The famous V-shaped graphic EQ curve on the high-power lead amp.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":4,"amp.gain":0.6,"amp.bass":0.35,"amp.mid":0.55,"amp.treble":0.6,"amp.presence":0.5,"amp.depth":0.5,"amp.master":0.55,
   "eq.enabled":1,"eq.b80":3,"eq.b240":0,"eq.b750":-4,"eq.b2200":1,"eq.b6600":3,"gate.enabled":1,"gate.threshold":-60}),
 ("High Gain Rhythm", "High Gain", "Thick, saturated modern rhythm with a huge low end.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":6,"amp.gain":0.55,"amp.bass":0.5,"amp.mid":0.5,"amp.treble":0.55,"amp.presence":0.5,"amp.depth":0.45,"amp.master":0.55,
   "gate.enabled":1,"gate.threshold":-60}),
 ("Modern Metal", "Modern Metal", "Boosted modern high gain: tight palm mutes, clear chords.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":6,"amp.gain":0.6,"amp.bass":0.45,"amp.mid":0.45,"amp.treble":0.6,"amp.presence":0.55,"amp.depth":0.55,"amp.master":0.55,
   "drive.enabled":1,"drive.drive":0.0,"drive.tone":0.6,"drive.level":6.0,
   "eq.enabled":1,"eq.b80":1,"eq.b240":-2,"eq.b750":0,"eq.b2200":1,"eq.b6600":0,
   "smooth.enabled":1,"smooth.amount":0.3,
   "gate.enabled":1,"gate.threshold":-56,"gate.release":80}),
 ("Plexi Stack", "Classic Rock", "Late-60s 100 W stack, dimed: open, chewy power-valve crunch, bright and toothy.", "factory:br-4x12-v25-cap", None,
  {"amp.model":8,"amp.gain":0.75,"amp.bass":0.45,"amp.mid":0.7,"amp.treble":0.6,"amp.presence":0.55,"amp.master":0.8,"amp.bright":1}),
 ("Dual Grind", "High Gain", "Saggy, raw 100 W Rectifier-style rhythm: chunky chugs that bloom.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":7,"amp.gain":0.6,"amp.bass":0.5,"amp.mid":0.45,"amp.treble":0.55,"amp.presence":0.5,"amp.depth":0.5,"amp.master":0.6,
   "gate.enabled":1,"gate.threshold":-58,"gate.release":80}),
 ("Lexicon Concert Hall", "Studio Reverb", "Glassy clean in a lush, gently chorused concert hall: 2.6 s, 24 ms pre-delay, bass x1.3.", "factory:am-2x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.5,"amp.mid":0.5,"amp.treble":0.58,"amp.master":0.6,"amp.bright":1,
   "reverb.enabled":1,"reverb.type":5,"reverb.lexprog":0,"reverb.mix":0.26,"reverb.decay":2.6,"reverb.tone":0.55,"reverb.predelay":24,
   "reverb.size":0.8,"reverb.diffusion":0.8,"reverb.bassmult":1.3,"reverb.crossover":650,"reverb.spin":0.9,"reverb.wander":0.35,"reverb.lowcut":120}),
 ("Random Hall Lead", "Studio Reverb", "Singing British lead in the classic Random Hall: long, wide, shimmering tail behind 40 ms of pre-delay.", "factory:br-4x12-v25-cap", None,
  {"amp.model":3,"amp.gain":0.72,"amp.bass":0.45,"amp.mid":0.65,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.6,
   "delay.enabled":1,"delay.time":380,"delay.feedback":0.22,"delay.mix":0.14,
   "reverb.enabled":1,"reverb.type":5,"reverb.lexprog":1,"reverb.mix":0.22,"reverb.decay":3.2,"reverb.tone":0.5,"reverb.predelay":40,
   "reverb.size":0.9,"reverb.diffusion":0.75,"reverb.bassmult":1.1,"reverb.crossover":700,"reverb.spin":1.1,"reverb.wander":0.55,"reverb.lowcut":150}),
 ("Rich Plate Chime", "Studio Reverb", "Class-A chime through the bright, dense Rich Plate: instant, 1.6 s, no pre-delay.", "factory:am-2x12-open-room", None,
  {"amp.model":2,"amp.gain":0.5,"amp.bass":0.4,"amp.mid":0.5,"amp.treble":0.6,"amp.presence":0.5,"amp.master":0.65,
   "reverb.enabled":1,"reverb.type":5,"reverb.lexprog":2,"reverb.mix":0.25,"reverb.decay":1.6,"reverb.tone":0.7,"reverb.predelay":0,
   "reverb.size":0.55,"reverb.diffusion":0.85,"reverb.bassmult":1.0,"reverb.crossover":500,"reverb.spin":1.3,"reverb.wander":0.45,"reverb.lowcut":100}),
 ("Bricasti Large Hall", "Studio Reverb", "Wide, natural hall around a clean chorus: 3 s, 18 ms pre-delay, distinct early reflections, no chorusing in the tail.", "factory:am-2x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.5,"amp.treble":0.55,"amp.master":0.6,
   "chorus.enabled":1,"chorus.rate":0.6,"chorus.depth":0.4,"chorus.mix":0.35,
   "reverb.enabled":1,"reverb.type":6,"reverb.m7prog":0,"reverb.mix":0.3,"reverb.decay":3.0,"reverb.tone":0.55,"reverb.predelay":18,
   "reverb.size":0.9,"reverb.diffusion":0.8,"reverb.early":0.45,"reverb.bassmult":1.2,"reverb.crossover":400,"reverb.spin":0.5,"reverb.wander":0.2,"reverb.lowcut":100}),
 ("Bricasti Small Room", "Studio Reverb", "Crunch in a tight, realistic room: mostly early reflections and a 0.7 s tail - sounds like a mic'd amp in a real space.", "factory:br-4x12-v25-cap", None,
  {"amp.model":3,"amp.gain":0.45,"amp.bass":0.45,"amp.mid":0.6,"amp.treble":0.6,"amp.presence":0.55,"amp.master":0.6,
   "reverb.enabled":1,"reverb.type":6,"reverb.m7prog":1,"reverb.mix":0.22,"reverb.decay":0.7,"reverb.tone":0.55,"reverb.predelay":2,
   "reverb.size":0.3,"reverb.diffusion":0.7,"reverb.early":0.75,"reverb.bassmult":1.0,"reverb.crossover":500,"reverb.spin":0.5,"reverb.wander":0.15,"reverb.lowcut":80}),
 ("Thrash Rack", "Modern Metal", "Tight, scooped 80s rack-preamp rhythm.", "factory:md-2x12-closed-cap", None,
  {"amp.model":5,"amp.gain":0.6,"amp.bass":0.5,"amp.mid":0.4,"amp.treble":0.6,"amp.presence":0.55,"amp.depth":0.4,"amp.master":0.5,
   "gate.enabled":1,"gate.threshold":-56,"gate.release":70}),
 ("RAT Grunge", "Classic Rock", "RAT pedal into a clean amp: fuzzy, raw and wide.", "factory:am-2x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.5,"amp.bass":0.5,"amp.mid":0.5,"amp.treble":0.5,"amp.master":0.6,
   "drive.enabled":1,"drive.type":1,"drive.drive":0.6,"drive.tone":0.45,"drive.level":0.0,
   "gate.enabled":1,"gate.threshold":-62}),
 ("Ambient Swells", "Ambient", "Clean pads: long tape echo and a big hall for volume swells.", "factory:am-2x12-open-room", None,
  {"amp.model":1,"amp.gain":0.45,"amp.bass":0.5,"amp.treble":0.5,"amp.master":0.62,
   "chorus.enabled":1,"chorus.rate":0.25,"chorus.depth":0.6,"chorus.mix":0.4,
   "delay.enabled":1,"delay.type":1,"delay.time":520,"delay.feedback":0.55,"delay.mix":0.35,"delay.tone":3000,
   "reverb.enabled":1,"reverb.type":1,"reverb.mix":0.45,"reverb.decay":6.0,"reverb.tone":0.45}),
 ("Dual Cab Crunch", "Classic Rock", "British crunch through a 4x12 and an open 2x12, panned wide.", "factory:br-4x12-v25-cap", "factory:am-2x12-open-cap",
  {"amp.model":3,"amp.gain":0.45,"amp.bass":0.45,"amp.mid":0.6,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.6,
   "cab.dual":1,"cab.a.pan":-0.6,"cab.b.pan":0.6,"cab.b.level":-2}),
]

# Output level per preset (dB), calibrated with `ampforge_render --calibrate`.
LEVELS = {'Plexi Stack': -6.5, 'Lexicon Concert Hall': -1.5, 'Random Hall Lead': -4.0, 'Rich Plate Chime': -5.0, 'Bricasti Large Hall': -1.0, 'Bricasti Small Room': -2.5, 'Dual Grind': -1.0, 'Glass Clean': -1.5, 'Studio Clean Chorus': -1.5, 'Warm Jazz Clean': 1.0, 'Edge of Breakup': -5.5, 'Top Boost Chime': -6.5, 'Tweed Blues': -5.5, 'Texas Drive': -11.0, 'Brit Crunch': -1.0, 'Cranked AC30': -5.0, 'British Crunch': -1.5, 'Boosted Brit': -5.0, 'Hot Rod Lead': -4.0, 'Singing Lead': 0.5, 'Fusion Lead': 0.5, 'Classic V Rhythm': -1.0, 'High Gain Rhythm': -1.0, 'Modern Metal': 0.5, 'Thrash Rack': 1.5, 'RAT Grunge': -6.5, 'Ambient Swells': -2.5, 'Dual Cab Crunch': -5.5}
LEVELS_OLD = {'Glass Clean': -1.5, 'Studio Clean Chorus': -1.5, 'Warm Jazz Clean': 3.0, 'Edge of Breakup': 1.5, 'Touch Sensitive': -7.5, 'Tweed Blues': -1.0, 'Texas Drive': -9.5, 'Plexi Crunch': -0.5, 'Cranked Plexi': -5.0, 'British Crunch': -1.0, 'Boosted Brit': -4.0, 'Hot Rod Lead': -4.0, 'Singing Lead': -4.0, 'Fusion Lead': -1.0, 'Classic V Rhythm': -2.5, 'High Gain Rhythm': -1.5, 'Modern Metal': -3.0, 'Tight Extended Range': -2.0, 'Ambient Swells': -1.5, 'Dual Cab Crunch': -2.5}

def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".json"):
            os.remove(os.path.join(OUT, f))
    ir_names = {}
    manifest = json.load(open(os.path.join(os.path.dirname(__file__), "..", "Resources", "FactoryIRs", "manifest.json")))
    for m in manifest:
        ir_names[m["id"]] = m["name"]
    # Usual valves per model (preamp V1, power) — matches AmpVoicing defaults in AmpModel.cpp.
    valves = {0: (2, 2), 1: (3, 4), 2: (0, 3), 3: (0, 1), 4: (0, 0), 5: (0, 0), 6: (0, 0), 7: (0, 0), 8: (0, 1)}
    for i, (name, cat, desc, ira, irb, params) in enumerate(P):
        params = dict(params)
        model = params["amp.model"]
        params.setdefault("amp.preampValve", valves[model][0])
        params.setdefault("amp.powerValve", valves[model][1])
        # Vintage-style models get no extra low-end resonance unless a preset asks for it.
        if model <= 3:
            params.setdefault("amp.depth", 0.0)
        params["out.level"] = LEVELS.get(name, 0.0)
        doc = {"format": "ampforge.preset", "schemaVersion": 2, "name": name, "category": cat,
               "description": desc, "author": "Z.AMP Factory", "parameters": params,
               "cabinet": {"a": {"irId": ira, "irName": ir_names.get(ira, "")},
                           "b": {"irId": irb or "", "irName": ir_names.get(irb, "") if irb else ""}}}
        slug = "".join(c.lower() if c.isalnum() else "-" for c in name).strip("-")
        with open(os.path.join(OUT, "%02d-%s.json" % (i + 1, slug)), "w") as fh:
            json.dump(doc, fh, indent=2)
    print("wrote %d presets to %s" % (len(P), os.path.normpath(OUT)))

if __name__ == "__main__":
    main()
