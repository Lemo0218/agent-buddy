# Agent Buddy — sprite assets (sprite-gen)

The dino character/animation assets are produced with
[**sprite-gen**](https://github.com/aldegad/sprite-gen) — a Codex/Claude skill that
turns one base image + a list of actions into a clean transparent sprite atlas
(`sprite-sheet-alpha.png`) + a runtime `manifest.json` (frame rectangles, fps, loop).

`buddy-dino/` already contains the **prepared run** (no image model needed yet):

```
buddy-dino/
├── sprite-request.json          # the recipe (states/frames/fps/loop)
├── base-source.png              # the dino base image (from buddy.png)
├── prompts/<state>.txt          # image-gen prompt per state
└── references/layout-guides/<state>.png   # frame-slot layout guide per state
```

States: `idle` (loop), `coding` (loop), `talking` (loop, = "needs you"), `success`.

## How to finish the sprites (needs an image-gen model)

sprite-gen's generation step requires `kuma:image-gen`, available in
Codex / Claude Desktop (not in a plain Claude Code CLI). On a machine with it:

```bash
git clone https://github.com/aldegad/sprite-gen
SG=sprite-gen/scripts
RUN=firmware/assets/sprites/buddy-dino

# 1) Generate each row strip from prompts/<state>.txt + layout guide + base image
#    -> save the model output as  $RUN/raw/<state>.png   (cyan #00FFFF background)

# 2) Extract transparent frames, then compose the atlas + manifest
python3 $SG/extract_sprite_row_frames.py --run-dir $RUN
python3 $SG/compose_sprite_atlas.py      --run-dir $RUN
#  -> $RUN/sprite-sheet-alpha.png  +  $RUN/manifest.json
```

## Firmware integration (done once sprites exist)

The ESP32 needs the atlas as a C array (RGB565 + transparency) and the frame
rectangles as constants. A converter + sprite renderer will read
`sprite-sheet-alpha.png` + `manifest.json` and blit frames per state.

> Note on size: the **village** dinos render at ~15–30 px, where fine sprite
> detail isn't visible — primitives stay best there. These sprites are intended
> for a **large/featured** dino (boot splash, a "hero" view, or a future
> single-pet mode), where the detail actually shows.
