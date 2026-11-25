#!/bin/bash
# audio_level.sh <seconds> [sink]: records a PulseAudio/PipeWire sink's monitor (the default sink without one) and
# prints its level, to check a native build isn't silent. To hear nothing and record the game alone, give it a sink
# of its own:
#   pactl load-module module-null-sink sink_name=xbe_test
#   PIPEWIRE_NODE=xbe_test ./ROTKXbox ...     (SDL_AUDIO_DRIVER=pulseaudio: PULSE_SINK=xbe_test)
#   sdk/smoke/audio_level.sh 5 xbe_test
# Needs parec and python3.
set -e
SECS=${1:-5}
SINK=${2:-$(pactl get-default-sink)}
OUT=$(mktemp --suffix=.raw)
trap 'rm -f "$OUT"' EXIT
echo "recording $SINK.monitor for $SECS s"
timeout -k 2 "$SECS" parec -d "$SINK.monitor" --format=float32le --rate=48000 --channels=2 --raw >"$OUT" 2>/dev/null || true
python3 - "$OUT" <<'EOF'
import array, math, sys
a = array.array("f")
with open(sys.argv[1], "rb") as f:
    data = f.read()
a.frombytes(data[: len(data) // 4 * 4])
n = len(a)
if n == 0:
    print("no samples recorded"); sys.exit(1)
db = lambda v: 20 * math.log10(v) if v > 0 else float("-inf")
rms = math.sqrt(sum(x * x for x in a) / n)
peak = max(abs(x) for x in a)
# 100 ms blocks with sound in them (above -60 dBFS), and the longest silent run (a gap in the sound)
blk = 9600
levels = [math.sqrt(sum(x * x for x in a[i:i + blk]) / blk) for i in range(0, n - blk + 1, blk)]
loud = sum(1 for v in levels if v > 0.001)
gap = run = 0
for v in levels:
    run = run + 1 if v <= 0.001 else 0
    gap = max(gap, run)
print(f"{n // 2} frames ({n / 96000:.1f} s): rms {db(rms):.1f} dBFS, peak {db(peak):.1f} dBFS, "
      f"{loud}/{len(levels)} blocks of 100 ms above -60 dBFS, longest quiet run {gap * 100} ms")
EOF
