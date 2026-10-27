#!/usr/bin/env python3
"""Generates ZackAMP factory presets (original settings, schema v1).

Each preset only lists parameters that differ from the parameter defaults; everything else
is filled in with defaults so the files stay readable. Output levels ("out.level") are
calibrated with Tools/Render.cpp (`ampforge_render --calibrate`) so presets play at a
similar loudness; the calibrated values are stored in LEVELS below.
"""
import json, os, sys

OUT = os.path.join(os.path.dirname(__file__), "..", "Resources", "FactoryPresets")

# Model indices: 0 Lacquer Clean, 1 Lacquer Tweed, 2 Albion Plexi, 3 Albion Lead, 4 Cascade Lead, 5 Cascade Heavy
P = [
 ("Glass Clean", "Clean", "Sparkling American clean with the bright switch on and a touch of room.", "factory:am-2x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.38,"amp.bass":0.45,"amp.mid":0.5,"amp.treble":0.62,"amp.master":0.6,"amp.bright":1,
   "reverb.enabled":1,"reverb.mix":0.22,"reverb.decay":2.2}),
 ("Studio Clean Chorus", "Clean", "Polished clean rhythm with stereo chorus and a subtle slap.", "factory:am-2x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.4,"amp.bass":0.5,"amp.treble":0.55,"amp.master":0.62,
   "chorus.enabled":1,"chorus.rate":0.7,"chorus.depth":0.45,"chorus.mix":0.45,
   "delay.enabled":1,"delay.time":110,"delay.feedback":0.1,"delay.mix":0.12,"reverb.enabled":1,"reverb.mix":0.15}),
 ("Warm Jazz Clean", "Clean", "Round, dark clean for neck pickups and chord melodies.", "factory:am-1x12-open-edge", None,
  {"amp.model":0,"amp.gain":0.4,"amp.bass":0.55,"amp.mid":0.6,"amp.treble":0.32,"amp.master":0.65,
   "reverb.enabled":1,"reverb.mix":0.16,"reverb.decay":1.4,"reverb.tone":0.3}),
 ("Edge of Breakup", "Edge of Breakup", "Clean when you pick lightly, hair and grit when you dig in. Roll back the guitar volume to clean up.", "factory:am-1x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.52,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.65}),
 ("Touch Sensitive", "Edge of Breakup", "Cranked American clean amp with power-amp bloom. Very dynamic.", "factory:am-2x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.72,"amp.bass":0.4,"amp.mid":0.55,"amp.treble":0.55,"amp.master":0.8,"amp.bright":1,
   "reverb.enabled":1,"reverb.mix":0.18}),
 ("Tweed Blues", "Blues", "Fat, loose vintage drive for single-note blues.", "factory:am-1x12-open-cap", None,
  {"amp.model":1,"amp.gain":0.7,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.55,"amp.presence":0.45,"amp.master":0.7,
   "reverb.enabled":1,"reverb.mix":0.2,"reverb.decay":1.6}),
 ("Texas Drive", "Blues", "Mid-hump overdrive pedal into a clean amp on the edge.", "factory:am-2x12-open-cap", None,
  {"amp.model":0,"amp.gain":0.55,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.master":0.65,
   "drive.enabled":1,"drive.drive":0.35,"drive.tone":0.55,"drive.level":5.0,"reverb.enabled":1,"reverb.mix":0.15}),
 ("Plexi Crunch", "Classic Rock", "Vintage British crunch: chords ring, single notes bite.", "factory:br-4x12-v25-cap", None,
  {"amp.model":2,"amp.gain":0.66,"amp.bass":0.45,"amp.mid":0.6,"amp.treble":0.6,"amp.presence":0.55,"amp.master":0.6,"amp.bright":1}),
 ("Cranked Plexi", "Classic Rock", "Everything on the stack turned up: thick, compressed and singing.", "factory:br-4x12-v25-cap", None,
  {"amp.model":2,"amp.gain":0.92,"amp.bass":0.4,"amp.mid":0.65,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.75,"amp.bright":1,
   "gate.enabled":1,"gate.threshold":-66}),
 ("British Crunch", "British Crunch", "Hot-rodded British amp at a moderate gain: tight, punchy rhythm.", "factory:br-4x12-h75-cap", None,
  {"amp.model":3,"amp.gain":0.42,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.6,"amp.presence":0.5,"amp.master":0.6}),
 ("Boosted Brit", "British Crunch", "Overdrive used as a boost into a Plexi-style amp for an 80s edge.", "factory:br-4x12-v25-edge", None,
  {"amp.model":2,"amp.gain":0.7,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.58,"amp.presence":0.55,"amp.master":0.6,
   "drive.enabled":1,"drive.drive":0.1,"drive.tone":0.6,"drive.level":8.0,"gate.enabled":1,"gate.threshold":-64}),
 ("Hot Rod Lead", "Lead", "Saturated British lead with delay and room.", "factory:br-4x12-h75-cap", None,
  {"amp.model":3,"amp.gain":0.78,"amp.bass":0.45,"amp.mid":0.65,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.6,
   "gate.enabled":1,"gate.threshold":-62,"delay.enabled":1,"delay.time":420,"delay.feedback":0.3,"delay.mix":0.22,
   "reverb.enabled":1,"reverb.mix":0.18}),
 ("Singing Lead", "Lead", "Smooth, vocal high-gain lead with mid push from the graphic EQ.", "factory:bt-1x12-closed-cap", None,
  {"amp.model":4,"amp.gain":0.62,"amp.bass":0.4,"amp.mid":0.6,"amp.treble":0.6,"amp.presence":0.45,"amp.depth":0.3,"amp.master":0.55,
   "eq.enabled":1,"eq.b80":0,"eq.b240":-1,"eq.b750":2,"eq.b2200":1.5,"eq.b6600":-2,
   "gate.enabled":1,"gate.threshold":-64,"gate.release":200,
   "delay.enabled":1,"delay.sync":1,"delay.division":5,"delay.feedback":0.32,"delay.mix":0.2,"reverb.enabled":1,"reverb.mix":0.2}),
 ("Fusion Lead", "Lead", "Articulate, slightly compressed lead with light chorus.", "factory:md-2x12-closed-cap", None,
  {"amp.model":4,"amp.gain":0.48,"amp.bass":0.45,"amp.mid":0.55,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.55,
   "chorus.enabled":1,"chorus.rate":0.4,"chorus.depth":0.3,"chorus.mix":0.3,
   "delay.enabled":1,"delay.time":360,"delay.feedback":0.25,"delay.mix":0.18,"reverb.enabled":1,"reverb.mix":0.16}),
 ("Classic V Rhythm", "High Gain", "The famous V-shaped graphic EQ curve on a mid-focused high-gain amp.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":4,"amp.gain":0.6,"amp.bass":0.35,"amp.mid":0.55,"amp.treble":0.6,"amp.presence":0.5,"amp.depth":0.5,"amp.master":0.55,
   "eq.enabled":1,"eq.b80":3,"eq.b240":0,"eq.b750":-4,"eq.b2200":1,"eq.b6600":3,"gate.enabled":1,"gate.threshold":-60}),
 ("High Gain Rhythm", "High Gain", "Thick, defined high-gain rhythm with a firm low end.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":5,"amp.gain":0.6,"amp.bass":0.5,"amp.mid":0.5,"amp.treble":0.55,"amp.presence":0.5,"amp.depth":0.45,"amp.master":0.55,
   "gate.enabled":1,"gate.threshold":-60}),
 ("Modern Metal", "Modern Metal", "Boosted modern high gain: tight palm mutes, clear chords.", "factory:md-4x12-heavy-cap", None,
  {"amp.model":5,"amp.gain":0.68,"amp.bass":0.5,"amp.mid":0.45,"amp.treble":0.6,"amp.presence":0.55,"amp.depth":0.6,"amp.master":0.55,
   "drive.enabled":1,"drive.drive":0.0,"drive.tone":0.6,"drive.level":6.0,
   "eq.enabled":1,"eq.b80":1,"eq.b240":-2,"eq.b750":0,"eq.b2200":1,"eq.b6600":0,
   "gate.enabled":1,"gate.threshold":-56,"gate.release":80}),
 ("Tight Extended Range", "Modern Metal", "Very tight low end for drop tunings; graphic EQ trims the boom.", "factory:md-2x12-closed-cap", None,
  {"amp.model":5,"amp.gain":0.55,"amp.bass":0.4,"amp.mid":0.55,"amp.treble":0.6,"amp.presence":0.55,"amp.depth":0.4,"amp.master":0.5,
   "drive.enabled":1,"drive.drive":0.05,"drive.tone":0.65,"drive.level":7.0,
   "eq.enabled":1,"eq.b80":-2,"eq.b240":-1,"eq.b750":2,"eq.b2200":1,"eq.b6600":-1,
   "gate.enabled":1,"gate.threshold":-54,"gate.release":60}),
 ("Ambient Swells", "Ambient", "Clean pads: long delay and lush reverb for volume swells.", "factory:am-2x12-open-room", None,
  {"amp.model":0,"amp.gain":0.4,"amp.bass":0.5,"amp.treble":0.5,"amp.master":0.62,
   "chorus.enabled":1,"chorus.rate":0.25,"chorus.depth":0.6,"chorus.mix":0.4,
   "delay.enabled":1,"delay.time":520,"delay.feedback":0.55,"delay.mix":0.35,"delay.tone":3000,
   "reverb.enabled":1,"reverb.mix":0.45,"reverb.decay":6.0,"reverb.tone":0.45}),
 ("Dual Cab Crunch", "Classic Rock", "British crunch through a 4x12 and an open 2x12, panned wide.", "factory:br-4x12-v25-cap", "factory:am-2x12-open-cap",
  {"amp.model":2,"amp.gain":0.62,"amp.bass":0.45,"amp.mid":0.6,"amp.treble":0.55,"amp.presence":0.5,"amp.master":0.6,
   "cab.dual":1,"cab.a.pan":-0.6,"cab.b.pan":0.6,"cab.b.level":-2}),
]

# Output level per preset (dB), calibrated with `ampforge_render --calibrate`.
LEVELS = {'Glass Clean': -1.5, 'Studio Clean Chorus': -1.5, 'Warm Jazz Clean': 3.0, 'Edge of Breakup': 1.5, 'Touch Sensitive': -7.5, 'Tweed Blues': -1.0, 'Texas Drive': -9.5, 'Plexi Crunch': -0.5, 'Cranked Plexi': -5.0, 'British Crunch': -1.0, 'Boosted Brit': -4.0, 'Hot Rod Lead': -4.0, 'Singing Lead': -4.0, 'Fusion Lead': -1.0, 'Classic V Rhythm': -2.5, 'High Gain Rhythm': -1.5, 'Modern Metal': -3.0, 'Tight Extended Range': -2.0, 'Ambient Swells': -1.5, 'Dual Cab Crunch': -2.5}

def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".json"):
            os.remove(os.path.join(OUT, f))
    ir_names = {}
    manifest = json.load(open(os.path.join(os.path.dirname(__file__), "..", "Resources", "FactoryIRs", "manifest.json")))
    for m in manifest:
        ir_names[m["id"]] = m["name"]
    for i, (name, cat, desc, ira, irb, params) in enumerate(P):
        params = dict(params)
        params["out.level"] = LEVELS.get(name, 0.0)
        doc = {"format": "ampforge.preset", "schemaVersion": 1, "name": name, "category": cat,
               "description": desc, "author": "ZackAMP Factory", "parameters": params,
               "cabinet": {"a": {"irId": ira, "irName": ir_names.get(ira, "")},
                           "b": {"irId": irb or "", "irName": ir_names.get(irb, "") if irb else ""}}}
        slug = "".join(c.lower() if c.isalnum() else "-" for c in name).strip("-")
        with open(os.path.join(OUT, "%02d-%s.json" % (i + 1, slug)), "w") as fh:
            json.dump(doc, fh, indent=2)
    print("wrote %d presets to %s" % (len(P), os.path.normpath(OUT)))

if __name__ == "__main__":
    main()
