#!/usr/bin/env python3
# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
# Numbers a recipe's cables on the rack's own back-panel render:
#   python3 docs/cookbook/make_diagram.py <recipe> <rack-shots dir> <out.png>
# The rack UI probe (ctest rack_ui, JidaiRackProbe <dir>) writes starter_NN_<name>_back.png for every starter rack and
# starter_NN_<name>_back_jacks.json with the pixel position of every patched jack. Each numbered step of the recipe gets
# a badge next to the jack it plugs into. Needs Pillow and the DejaVu fonts.
import glob
import json
import sys

from PIL import Image, ImageDraw, ImageFont

# Recipe steps, in the cookbook's order: (step, from jack, to jack). Jack ids as the rack names them.
RECIPES = {
    "edm_starter": [
        (1, "SHOGUN#1/CLOCK:RST OUT", "BUSHIDO#1/INPUTS:RESET"),
        (2, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/INT:IN"),
        (3, "RONIN#1/INT:OUT", "RONIN#1/VCO:V/OCT"),
        (4, "RONIN#1/VCO:SAW", "RONIN#1/VCF:IN"),
        (5, "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 1:TRIG"),
        (6, "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 2:TRIG"),
        (7, "RONIN#1/EG 2:OUT +", "RONIN#1/VCF:CUTOFF"),
        (8, "BUSHIDO#1/OUTPUTS:CV C", "RONIN#1/VCF:CUTOFF"),
        (9, "RONIN#1/VCF:OUT", "ORIGAMI#1/IN:IN L"),
        (10, "ORIGAMI#1/OUT:OUT L", "RONIN#1/VCA 1:IN"),
        (11, "SHOGUN#1/BD1:ENV", "RONIN#1/MIX:IN 1"),
        (12, "RONIN#1/MIX:OUT", "RONIN#1/VCA 1:ENV"),
        (13, "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L"),
        (14, "RONIN#1/HOST:OUT R", "RACK#1/MAIN:OUT R"),
        (15, "SHOGUN#1/MIX:L", "RACK#1/MAIN:OUT L"),
        (16, "SHOGUN#1/MIX:R", "RACK#1/MAIN:OUT R"),
    ],
}

GOLD, INK = (201, 162, 39), (22, 22, 26)


def main():
    recipe, shots, out = sys.argv[1], sys.argv[2], sys.argv[3]
    png = glob.glob(f"{shots}/starter_[0-9][0-9]_{recipe}_back.png")
    if len(png) != 1:
        sys.exit(f"no back render for {recipe} in {shots}")
    jacks = json.load(open(png[0][:-4] + "_jacks.json"))
    img = Image.open(png[0]).convert("RGB")
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype("DejaVuSans-Bold.ttf", 17)
    used = {}
    for step, a, b in RECIPES[recipe]:
        for j in (a, b):
            if j not in jacks:
                sys.exit(f"step {step}: {j} is not patched in the render")
        x, y, r = jacks[b]
        k = used.get(b, 0)
        used[b] = k + 1
        cx, cy, br = x - r - 16, y - k * 30, 14       # left of the jack (tags sit on its right); a second cable stacks above
        draw.ellipse((cx - br, cy - br, cx + br, cy + br), fill=GOLD, outline=INK, width=2)
        text = str(step)
        w = draw.textlength(text, font=font)
        draw.text((cx - w / 2, cy - 11), text, fill=INK, font=font)
    # Drop the empty rack space under the last device.
    bottom = max(v[1] for v in jacks.values()) + 70
    img = img.crop((0, 0, img.width, min(img.height, int(bottom))))
    img.save(out, optimize=True)
    print(out, img.size)


if __name__ == "__main__":
    main()
