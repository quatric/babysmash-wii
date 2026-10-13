#!/usr/bin/env bash
# Converts the original BabySmash sounds + word list into the raw blobs linked into the DOL.
# Needs ffmpeg and a checkout of https://github.com/shanselman/babysmash (arg 1).
set -e
SRC="${1:?usage: prep_data.sh /path/to/babysmash}/Shared/Resources"
OUT="$(cd "$(dirname "$0")/.." && pwd)/data"
mkdir -p "$OUT"
for f in EditedJackPlaysBabySmash babygigl2 babylaugh ccgiggle falling giggle laughingmice rising scooby2 smallbumblebee; do
    ffmpeg -loglevel error -y -i "$SRC/Sounds/$f.wav" -ac 1 -ar 22050 -f s16be -acodec pcm_s16be "$OUT/$f.bin"
done
# newline separated, upper-case, 2..15 letters, NUL terminated
tr -d '\r' < "$SRC/Words.txt" | sed 's/^\xef\xbb\xbf//' | tr 'a-z' 'A-Z' | grep -E '^[A-Z]{2,15}$' | sort -u > "$OUT/words.bin"
printf '\0' >> "$OUT/words.bin"
