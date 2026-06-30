"""
Generate pixel-art buddy-dino sprite strips for sprite-gen pipeline.

Creates raw/<state>.png strips (4 frames × 96px) with magenta chroma key.
States: idle, coding, talking, success
"""

from PIL import Image, ImageDraw
import os, math

CELL = 96
FRAMES = 4
CHROMA = (0, 255, 255)   # cyan — matches image-gen prompt spec (#00FFFF)

# Palette
C_BODY   = (168, 216, 166)   # #A8D8A6 green
C_DARK   = (104, 154, 106)   # #689A6A dark green (spikes, outline)
C_BELLY  = (215, 233, 192)   # #D7E9C0 belly
C_OUTLN  = (20,  33,  15)    # #14210F near-black outline
C_EYE    = (240, 240, 240)   # white sclera
C_PUPIL  = (20,  20,  20)    # pupil
C_MOUTH  = (74,  51,  40)    # #4A3328 brown mouth
C_CHEEK  = (240, 160, 155)   # blush
C_SPRT   = (120, 210, 120)   # sprout green
C_ALERT  = (255, 200, 60)    # alert yellow

OUT_DIR = os.path.join(os.path.dirname(__file__), "buddy-dino", "raw")
os.makedirs(OUT_DIR, exist_ok=True)


def circle(draw, cx, cy, r, fill, outline=None, width=1):
    draw.ellipse([cx-r, cy-r, cx+r, cy+r], fill=fill,
                 outline=outline, width=width)


def dino_frame(draw, ox, oy, state, frame_idx, total_frames):
    """Draw one dino frame at (ox, oy) offset within the strip."""

    # --- base animation parameters ---
    t = frame_idx / total_frames         # 0..1 cycle position
    bob = round(math.sin(t * 2 * math.pi) * 2)   # ±2px body bob

    # state-specific overrides
    arm_l_up = arm_r_up = False
    mouth_open = False
    jumping = False
    jump_h = 0

    if state == "idle":
        bob = round(math.sin(t * 2 * math.pi) * 1.5)

    elif state == "coding":
        # left/right arm alternates tapping
        arm_l_up = frame_idx in (0, 2)
        arm_r_up = frame_idx in (1, 3)
        bob = round(math.sin(t * 4 * math.pi) * 0.8)

    elif state == "talking":
        arm_l_up = frame_idx >= 2
        mouth_open = frame_idx in (1, 3)
        bob = 0

    elif state == "success":
        # 0=crouch, 1=leap, 2=peak, 3=land
        jump_h = [0, -10, -18, -4][frame_idx]
        arm_l_up = arm_r_up = frame_idx in (1, 2)
        jumping = frame_idx in (1, 2)
        bob = 0

    # character anchor: feet at y=86 (safe area bottom = 88, leave 2px gap)
    feet_y = 86 + oy + jump_h

    # body ellipse center
    bw, bh = 22, 28
    body_cx = ox + 48
    body_cy = feet_y - bh

    # head circle
    hr = 20
    head_cx = body_cx
    head_cy = body_cy - bh + 4 + bob

    # --- draw body ---
    # outline first (slightly bigger)
    draw.ellipse([body_cx-bw-1, body_cy-bh-1, body_cx+bw+1, body_cy+bh+1],
                 fill=C_OUTLN)
    draw.ellipse([body_cx-bw, body_cy-bh, body_cx+bw, body_cy+bh],
                 fill=C_BODY)
    # belly
    draw.ellipse([body_cx-12, body_cy-14, body_cx+12, body_cy+18],
                 fill=C_BELLY)

    # --- spikes (3, on back of head/neck) ---
    spike_bases = [(-8, head_cy - hr + 5), (0, head_cy - hr - 2), (8, head_cy - hr + 5)]
    for (sx, sy) in spike_bases:
        pts = [(head_cx+sx-5, sy+8), (head_cx+sx, sy-8), (head_cx+sx+5, sy+8)]
        draw.polygon(pts, fill=C_DARK)
    # outline spikes
    for (sx, sy) in spike_bases:
        pts = [(head_cx+sx-5, sy+8), (head_cx+sx, sy-8), (head_cx+sx+5, sy+8)]
        draw.polygon(pts, outline=C_OUTLN, fill=None)

    # --- head ---
    # outline
    circle(draw, head_cx, head_cy, hr+1, C_OUTLN)
    circle(draw, head_cx, head_cy, hr, C_BODY)

    # --- eyes ---
    eye_lx, eye_rx = head_cx - 7, head_cx + 7
    eye_y = head_cy - 3
    eye_r = 6
    circle(draw, eye_lx, eye_y, eye_r, C_OUTLN)
    circle(draw, eye_rx, eye_y, eye_r, C_OUTLN)
    circle(draw, eye_lx, eye_y, eye_r-1, C_EYE)
    circle(draw, eye_rx, eye_y, eye_r-1, C_EYE)
    # pupils - shift slightly toward center/forward for personality
    circle(draw, eye_lx+1, eye_y+1, 3, C_PUPIL)
    circle(draw, eye_rx-1, eye_y+1, 3, C_PUPIL)
    # eye shine
    circle(draw, eye_lx+2, eye_y-1, 1, C_EYE)
    circle(draw, eye_rx, eye_y-1, 1, C_EYE)

    # blink on idle frame 3
    if state == "idle" and frame_idx == 3:
        draw.rectangle([eye_lx-eye_r+1, eye_y-1, eye_lx+eye_r-1, eye_y+2],
                       fill=C_BODY)
        draw.rectangle([eye_rx-eye_r+1, eye_y-1, eye_rx+eye_r-1, eye_y+2],
                       fill=C_BODY)

    # --- mouth ---
    if mouth_open:
        draw.arc([head_cx-7, head_cy+7, head_cx+7, head_cy+16],
                 start=0, end=180, fill=C_OUTLN, width=2)
        draw.arc([head_cx-5, head_cy+8, head_cx+5, head_cy+14],
                 start=10, end=170, fill=C_MOUTH, width=1)
    else:
        draw.arc([head_cx-6, head_cy+8, head_cx+6, head_cy+15],
                 start=10, end=170, fill=C_OUTLN, width=2)

    # --- tail (compact rounded stub — stays above feet, curves right not down) ---
    # Old tail extended 18 px past body and 6 px BELOW feet (y=92). This one
    # peaks at y=64 (22 px above feet) and extends only 14 px past body edge.
    tx = body_cx + bw - 2   # 68 — anchors at right body edge
    ty = body_cy + 16        # 74 — lower body, well above feet (86)
    tail_pts = [
        (tx,      ty - 5),   # 68, 69  base-top
        (tx + 5,  ty - 10),  # 73, 64  upper curve
        (tx + 12, ty - 7),   # 80, 67  tip-top
        (tx + 14, ty),       # 82, 74  tip-outer
        (tx + 12, ty + 7),   # 80, 81  tip-bottom
        (tx + 5,  ty + 6),   # 73, 80  lower curve
        (tx,      ty + 5),   # 68, 79  base-bottom
    ]
    draw.polygon(tail_pts, fill=C_OUTLN)
    inner_tail = [
        (tx + 1,  ty - 4),
        (tx + 5,  ty - 8),
        (tx + 11, ty - 6),
        (tx + 13, ty),
        (tx + 11, ty + 6),
        (tx + 5,  ty + 5),
        (tx + 1,  ty + 4),
    ]
    draw.polygon(inner_tail, fill=C_BODY)

    # --- legs ---
    leg_y_top = feet_y - 12
    for lx in (body_cx-10, body_cx+4):
        draw.rectangle([lx, leg_y_top, lx+7, feet_y], fill=C_OUTLN)
        draw.rectangle([lx+1, leg_y_top+1, lx+6, feet_y-1], fill=C_DARK)
    # feet
    for lx in (body_cx-12, body_cx+3):
        draw.ellipse([lx, feet_y-4, lx+10, feet_y+3], fill=C_OUTLN)
        draw.ellipse([lx+1, feet_y-3, lx+9, feet_y+2], fill=C_DARK)

    # --- arms (ellipse stubs — rounded flipper-style, not triangle fins) ---
    # Ellipses are 12 × 20 px (outline), centered just outside each body edge.
    # Raised position shifts 12 px up and 2 px outward to clear the head circle.
    # Resting bottom (y=64) aligns exactly with tail top — zero overlap.
    arm_rw, arm_rh = 5, 9   # half-widths for ellipse (outline = +1 each side)

    # left arm
    la_cx = body_cx - bw - 2   # 24
    la_cy = body_cy - 4         # 54
    if arm_l_up:
        la_cx -= 2   # 22 — shift outward so elbow clears head left edge (x=28)
        la_cy -= 12  # 42

    draw.ellipse([la_cx - arm_rw - 1, la_cy - arm_rh - 1,
                  la_cx + arm_rw + 1, la_cy + arm_rh + 1], fill=C_OUTLN)
    draw.ellipse([la_cx - arm_rw, la_cy - arm_rh,
                  la_cx + arm_rw, la_cy + arm_rh], fill=C_DARK)

    # right arm
    ra_cx = body_cx + bw + 2   # 72
    ra_cy = body_cy - 4         # 54
    if arm_r_up:
        ra_cx += 2   # 74 — shift outward so elbow clears head right edge (x=68)
        ra_cy -= 12  # 42

    draw.ellipse([ra_cx - arm_rw - 1, ra_cy - arm_rh - 1,
                  ra_cx + arm_rw + 1, ra_cy + arm_rh + 1], fill=C_OUTLN)
    draw.ellipse([ra_cx - arm_rw, ra_cy - arm_rh,
                  ra_cx + arm_rw, ra_cy + arm_rh], fill=C_DARK)

    # --- success: sparkles at peak ---
    if state == "success" and frame_idx == 2:
        for sx, sy in [(ox+20, oy+20), (ox+72, oy+18), (ox+65, oy+35)]:
            draw.line([sx-4, sy, sx+4, sy], fill=C_ALERT, width=2)
            draw.line([sx, sy-4, sx, sy+4], fill=C_ALERT, width=2)

    # --- coding: small code symbol in front ---
    if state == "coding" and frame_idx in (0, 2):
        draw.text((ox+8, oy+10), "</>" , fill=C_DARK)

    # --- talking: ! bubble ---
    if state == "talking" and frame_idx >= 1:
        bx, by = head_cx + hr - 2, head_cy - hr - 10
        circle(draw, bx, by, 8, C_EYE, C_OUTLN)
        draw.text((bx-3, by-6), "!", fill=C_OUTLN)


def make_strip(state, frames=4, cell=CELL):
    img = Image.new("RGB", (cell * frames, cell), CHROMA)
    draw = ImageDraw.Draw(img)
    for i in range(frames):
        dino_frame(draw, i * cell, 0, state, i, frames)
    out_path = os.path.join(OUT_DIR, f"{state}.png")
    img.save(out_path)
    print(f"  {state}.png → {img.size}")
    return out_path


if __name__ == "__main__":
    print("Generating buddy-dino sprite strips...")
    for state in ("idle", "coding", "talking", "success"):
        make_strip(state)
    print("Done. raw/ strips written.")
