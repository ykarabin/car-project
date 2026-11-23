#!/bin/sh
# Generates LVGL fonts (Montserrat + Ukrainian Cyrillic) into main/assets.
# Sizes are the 800x480 design sizes scaled by 1.25 for the 1024x600 panel.
set -e
cd "$(dirname "$0")/.."
F=tools/fonts
OUT=main/assets
FA=tools/fonts/FontAwesome5.woff

TEXT="0x20-0x7E,0xB0,0xB7,0x2014,0x2019,0x2212,0x2248,0x404,0x406,0x407,0x410-0x44F,0x454,0x456,0x457,0x490,0x491"
SYM="0xF55A"   # LV_SYMBOL_BACKSPACE

conv() { # name ttf size range [extra args]
  name=$1; ttf=$2; size=$3; range=$4; shift 4
  npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size "$size" --format lvgl \
    --lv-include lvgl.h --lv-font-name "$name" \
    --font "$F/$ttf" -r "$range" "$@" -o "$OUT/$name.c"
  echo "$name: $(wc -c < "$OUT/$name.c") bytes of C"
}

conv font_r16 Montserrat-Regular.ttf 16 "$TEXT"
conv font_r18 Montserrat-Regular.ttf 18 "$TEXT"
conv font_r20 Montserrat-Regular.ttf 20 "$TEXT"
conv font_r22 Montserrat-Regular.ttf 22 "$TEXT"
conv font_r25 Montserrat-Regular.ttf 25 "$TEXT"
conv font_s28 Montserrat-SemiBold.ttf 28 "$TEXT"
conv font_s32 Montserrat-SemiBold.ttf 32 "$TEXT"
conv font_s40 Montserrat-SemiBold.ttf 40 "$TEXT" --font "$FA" -r "$SYM"
conv font_s50 Montserrat-SemiBold.ttf 50 "$TEXT"
conv font_s90 Montserrat-SemiBold.ttf 90 "0x25,0x30-0x39"
conv font_s138 Montserrat-SemiBold.ttf 138 "0x30-0x39,0x2014"
# theme fonts (digits and the em dash shown when the link is lost)
conv font_s238 Montserrat-SemiBold.ttf 238 "0x30-0x39,0x2014"   # Sport: huge speed
conv font_s212 Montserrat-SemiBold.ttf 212 "0x30-0x39,0x2014"   # Kids: big speed
conv font_s80  Montserrat-SemiBold.ttf 80  "0x30-0x39,0x2014"   # Analog: digital speed
conv font_s70  Montserrat-SemiBold.ttf 70  "0x25,0x2C,0x30-0x39,0x2014"   # Sport/Analog: range with decimal comma
