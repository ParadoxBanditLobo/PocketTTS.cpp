#!/bin/sh
# Offline diagnostic: native synthesis without the raylib frontend.
set -eu
if [ "$#" -ne 1 ]; then
  echo "Usage: sh scripts/audio-smoke-test.sh /full/path/to/voice.wav" >&2
  exit 2
fi
REFERENCE="$1"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ ! -x "$ROOT/pocket-tts" ]; then
  echo "CLI executable missing; build the application first." >&2
  exit 1
fi
if [ ! -f "$REFERENCE" ]; then
  echo "Reference recording not found." >&2
  exit 1
fi
OUTDIR=$(mktemp -d "${TMPDIR:-/tmp}/pockettts-smoke.XXXXXXXX")
chmod 700 "$OUTDIR"
OUT="$OUTDIR/native-test.wav"
echo "Generating with the same ONNX models, using the native CLI..."
cd "$ROOT"
./pocket-tts --no-cache --models-dir "$ROOT/models" --tokenizer "$ROOT/models/tokenizer.model" --verbose "Hello. This is a longer sentence to determine whether Pocket TTS can produce intelligible speech." "$REFERENCE" "$OUT"
echo "Native test output: $OUT"
echo "Play this WAV in a separate audio player. If it also clicks, investigate ONNX model/inference compatibility."
