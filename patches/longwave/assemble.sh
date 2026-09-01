#!/usr/bin/env bash
# longwave -- render the five movements and assemble the hour.
#
# The movements overlap by 60 seconds. Each one's tail has already ramped its
# clock to the NEXT movement's tempo, so the two grids agree before they ever
# sound together: the crossfade is a metric modulation, not a beat-match.
#
# Offsets are integer bar counts of the INCOMING tempo, which is why movement
# V starts at 3180.952 s and not 3180 -- 3180 is 834.75 bars at 63 BPM, and
# landing a coda three quarters of a bar early is audible.
#
#   I    0.000 s      14:00 slot   126 -> 168 BPM in its tail
#   II   840.000 s    13:00        168 -> 84   (a halving, no ramp needed)
#   III  1620.000 s    9:00         84 -> 126
#   IV   2160.000 s   17:00        126 -> 63   (a ritardando)
#   V    3180.952 s    7:00        ends open; the fade is applied here
#
# Usage: patches/longwave/assemble.sh [outfile]
set -euo pipefail

cd "$(dirname "$0")/../.."
BIN=./build/ratsnest
OUT="${1:-longwave.wav}"
WORK="${LONGWAVE_WORK:-$(mktemp -d)}"

# file                     seconds  offset      arr_clk bpm
MOVEMENTS=(
  "1-dorian.patch           900      0.000       504"
  "2-phrygian.patch         840      840.000     672"
  "3-aeolian.patch          600      1620.000    336"
  "4-lydian.patch           1080     2160.000    504"
  "5-coda.patch             420      3180.952    252"
)

# --- guard: a time-compressed probe divisor must never reach the assembly ---
# Probing the form means dividing the arrangement clock by 10-15x (see
# docs/patch-craft.md). That is a one-character edit and it silently ruins an
# hour, so refuse to assemble unless every movement is back to 128 (8 bars)
# and running its committed arrangement tempo.
for m in "${MOVEMENTS[@]}"; do
  read -r file secs offset bpm <<<"$m"
  path="patches/longwave/$file"
  [[ -f "$path" ]] || { echo "missing: $path" >&2; exit 1; }
  grep -qE '^blk +=+ div \{ by: 128 \}' "$path" \
    || { echo "GUARD: $file arrangement divisor is not 128 -- probe left in?" >&2; exit 1; }
  grep -qE "^arr_clk += clock \{ bpm: $bpm \}" "$path" \
    || { echo "GUARD: $file arrangement clock is not $bpm BPM" >&2; exit 1; }
done
echo "guard: all five movements at their committed grid"

# --- render (all five in parallel; they are independent processes) ---
# Set LONGWAVE_SKIP_RENDER=1 with LONGWAVE_WORK pointing at existing renders
# to re-assemble without re-rendering the hour.
if [[ "${LONGWAVE_SKIP_RENDER:-0}" != "1" ]]; then
  for m in "${MOVEMENTS[@]}"; do
    read -r file secs offset bpm <<<"$m"
    stem="${file%.patch}"
    echo "rendering $stem (${secs}s)"
    "$BIN" render "patches/longwave/$file" -d "${secs}s" -o "$WORK/$stem.wav" &
  done
  wait
fi
for m in "${MOVEMENTS[@]}"; do
  read -r file secs offset bpm <<<"$m"
  [[ -f "$WORK/${file%.patch}.wav" ]] || { echo "missing render: ${file%.patch}.wav" >&2; exit 1; }
done

# --- assemble: 60 s equal-power crossfades at bar-aligned offsets ---
# Movement I gets no fade-in (the hour's 12 s opening fade is applied at the
# end); every other movement fades in over its first 60 s, and every movement
# but the last fades out over its final 60 s.
args=(); filters=(); labels=""
i=0
for m in "${MOVEMENTS[@]}"; do
  read -r file secs offset bpm <<<"$m"
  stem="${file%.patch}"
  args+=(-i "$WORK/$stem.wav")
  chain="[$i:a]"
  [[ $i -gt 0 ]] && chain+="afade=t=in:st=0:d=60:curve=qsin,"
  [[ $i -lt $(( ${#MOVEMENTS[@]} - 1 )) ]] \
    && chain+="afade=t=out:st=$(echo "$secs - 60" | bc):d=60:curve=qsin,"
  # adelay is in milliseconds -- truncating to whole seconds here would
  # throw away exactly the sub-second bar alignment computed above.
  ms=$(echo "($offset * 1000) / 1" | bc)
  chain+="adelay=${ms}|${ms}:all=1[m$i]"
  filters+=("$chain")
  labels+="[m$i]"
  i=$((i+1))
done
filters+=("${labels}amix=inputs=${#MOVEMENTS[@]}:normalize=0:dropout_transition=0[mixed]")
filters+=("[mixed]afade=t=in:st=0:d=12,afade=t=out:st=3570:d=30[final]")

ffmpeg -hide_banner -loglevel warning -y "${args[@]}" \
  -filter_complex "$(IFS=';'; echo "${filters[*]}")" \
  -map "[final]" -c:a pcm_f32le "$OUT"

echo "assembled -> $OUT"
ffprobe -hide_banner -show_entries format=duration -of default=nk=1:nw=1 "$OUT"
