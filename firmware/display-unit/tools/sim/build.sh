#!/bin/sh
# Builds the host screenshot simulator and renders all scenes to tools/sim/out/*.png
# usage: tools/sim/build.sh   (needs clang; Pillow for PNG conversion is optional)
set -e
cd "$(dirname "$0")"
ROOT=../..
LV=$ROOT/managed_components/lvgl__lvgl
OBJ=build
mkdir -p $OBJ out
CFLAGS="-O2 -w -DCONFIG_DASH_DEV_DEMO_BUTTON=1 -DLV_CONF_INCLUDE_SIMPLE -I. -Istubs -I$LV -I$LV/src -I$ROOT/main/ui -I$ROOT/main/assets"

SRCS="$(find $LV/src -name '*.c') $(ls $ROOT/main/ui/*.c) $(ls $ROOT/main/assets/*.c) sim_main.c"
echo "$SRCS" | tr ' ' '\n' | grep . | while read -r f; do
  o=$OBJ/$(echo "$f" | sed 's#[/.]#_#g').o
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ lv_conf.h -nt "$o" ]; then echo "$f $o"; fi
done | xargs -P 8 -n 2 sh -c 'cc '"$CFLAGS"' -c "$1" -o "$2"' _ 2>&1 | grep -v '^$' || true
cc -o $OBJ/sim $OBJ/*.o -lm
rm -f out/*.ppm out/*.png
./$OBJ/sim out
