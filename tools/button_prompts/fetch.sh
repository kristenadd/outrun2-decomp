#!/bin/sh
# Downloads the controller button prompt art (OR2006Sprites, see README.md
# "Controller button prompts") into a game folder:
#
#   sh tools/button_prompts/fetch.sh <game folder> [xbox] [playstation] [switch]
#
# With no style named, all three are fetched. Files go to
#   <game folder>/textures/load/<Xbox|PlayStation|Switch>/<sheet folder>/<HASH>_<w>x<h>.dds
# shrunk to the game's own sheet size (about 10 MB per style instead of
# 160 MB; the game shrinks full-size files the same way when it loads them).
#
# For iOS / iPadOS, give an empty folder, then copy its "textures" folder
# into the app's files (Files app or Finder file sharing), next to the game
# files.
set -eu

# The OR2006Sprites commit the files are taken from (envido32's fork, the
# "Release" set; the hashes below are those of the game's original sheets).
REPO=envido32/OR2006Sprites
COMMIT=83331c64112eb3ad068e8ba7f5741aa5ccdd6cfb
SHEETS="
spr_sprani_congrats_cvt_Exst/455717B2_512x512
spr_sprani_etc_cvt_Exst/1F77CB88_128x128
spr_sprani_fruity_cvt_Exst/FA7BBB13_1024x512
spr_sprani_game_cvt_Exst/39229D64_1024x1024
spr_sprani_sumo_fe_cvt_Exst/ACF61D7C_1024x512
spr_sprani_sumo_fe_cvt_Exst/31C58963_512x256
"

if [ $# -lt 1 ]; then
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
game=$1; shift
[ -d "$game" ] || { echo "No such folder: $game" >&2; exit 1; }
styles=${*:-xbox playstation switch}

work=$(mktemp -d "${TMPDIR:-/tmp}/or2-prompts.XXXXXX")
trap 'rm -rf "$work"' EXIT
here=$(cd "$(dirname "$0")" && pwd)
shrink=""
if cc -O2 -o "$work/shrink_dds" "$here/shrink_dds.c" 2>/dev/null; then shrink="$work/shrink_dds"
else echo "No C compiler: keeping the full-size files (about 160 MB per style)." >&2; fi

for style in $styles; do
    case $(echo "$style" | tr '[:upper:]' '[:lower:]') in
        xbox) folder=Xbox ;;
        playstation|ps) folder=PlayStation ;;
        switch|nintendo) folder=Switch ;;
        *) echo "Unknown style: $style (xbox, playstation, switch)" >&2; exit 1 ;;
    esac
    echo "$folder:"
    for sheet in $SHEETS; do
        dir=${sheet%/*}; name=${sheet##*/}
        size=${name#*_}; w=${size%x*}; h=${size#*x}
        url="https://raw.githubusercontent.com/$REPO/$COMMIT/Release/$folder/$dir/$name.dds"
        dest="$game/textures/load/$folder/$dir/$name.dds"
        mkdir -p "$(dirname "$dest")"
        printf '  %s ... ' "$dir/$name.dds"
        curl -fsSL -o "$work/full.dds" "$url"
        if [ -n "$shrink" ] && "$shrink" "$work/full.dds" "$work/small.dds" "$w" "$h"; then
            mv "$work/small.dds" "$dest"
        else
            mv "$work/full.dds" "$dest"
        fi
        rm -f "$work/full.dds"
        echo ok
    done
done
echo "Done: $game/textures/load"
