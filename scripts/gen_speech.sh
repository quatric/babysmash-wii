#!/usr/bin/env bash
# Spoken letters / numbers / colours / shapes (and optionally words) for the original's "Speech"
# sound mode. The Wii has no text-to-speech, so the clips are synthesised here, on your computer,
# and baked into the app:
#
#   ./scripts/gen_speech.sh            -> data/speech.bin  (linked into boot.dol by `make`)
#   ./scripts/gen_speech.sh --words    -> speech/words/*.bin for the ~1800 dictionary words,
#                                         copy `speech/` to apps/babysmash/speech on the SD card
#
# ENGINE=espeak-ng  (default) espeak-ng (GPL; `brew install espeak-ng` / `apt install espeak-ng`).
#                   Robotic but freely redistributable, so the result can be shipped.
# ENGINE=say        macOS `say`, voice $VOICE (default Samantha). Nicer, but Apple's voices are
#                   not licensed for redistribution: keep that result private.
# Needs ffmpeg and python3.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${ENGINE:-espeak-ng}"
VOICE="${VOICE:-Samantha}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

render() {   # out.bin  text
    case "$ENGINE" in
        say)       say -v "$VOICE" -o "$WORK/t.aiff" "$2" </dev/null; SRC="$WORK/t.aiff" ;;
        espeak-ng) espeak-ng -v "${VOICE_ESPEAK:-en-us}" -s 140 -g 2 -w "$WORK/t.wav" "$2"; SRC="$WORK/t.wav" ;;
        *) echo "unknown ENGINE $ENGINE" >&2; exit 1 ;;
    esac
    ffmpeg -nostdin -loglevel error -y -i "$SRC" -af "silenceremove=start_periods=1:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_threshold=-50dB,areverse" \
        -ac 1 -ar 22050 -f s16be -acodec pcm_s16be "$1"
}

if [ "$1" = "--words" ]; then
    OUT="$ROOT/speech/words"
    mkdir -p "$OUT"
    tr -d '\000' < "$ROOT/data/words.bin" | while read -r w; do
        [ -n "$w" ] || continue
        f="$OUT/$(echo "$w" | tr 'A-Z' 'a-z').bin"
        [ -f "$f" ] || render "$f" "$(echo "$w" | tr 'A-Z' 'a-z')"
    done
    echo "wrote $(ls "$OUT" | wc -l | tr -d ' ') word clips to $OUT"
    exit 0
fi

CLIPS="$WORK/clips"
mkdir -p "$CLIPS"
for c in {a..z}; do render "$CLIPS/$c.bin" "$c."; done
n=0; for w in zero one two three four five six seven eight nine; do render "$CLIPS/$n.bin" "$w"; n=$((n+1)); done
for w in red blue yellow green purple pink orange tan gray circle oval rectangle hexagon trapezoid star square triangle heart; do
    render "$CLIPS/$w.bin" "$w"
done

python3 - "$CLIPS" "$ROOT/data/speech.bin" <<'PY'
import os, struct, sys
src, out = sys.argv[1], sys.argv[2]
names = sorted(f[:-4] for f in os.listdir(src) if f.endswith('.bin'))
head = 4 + 24 * len(names)
body, entries = b'', b''
for n in names:
    d = open(os.path.join(src, n + '.bin'), 'rb').read()
    d += b'\0' * (-len(d) % 4)
    entries += n.encode().ljust(16, b'\0') + struct.pack('>II', head + len(body), len(d))
    body += d
open(out, 'wb').write(struct.pack('>I', len(names)) + entries + body)
print(f"packed {len(names)} clips, {os.path.getsize(out)//1024} KiB -> {out}")
PY
echo "now run: make"
