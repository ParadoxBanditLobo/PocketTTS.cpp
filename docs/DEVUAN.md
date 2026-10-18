# Devuan Linux setup and testing

Target: Devuan Linux (initially x86-64), desktop/X11, standalone native C++ / raylib GUI.

This is an **unverified integration checkpoint**, not a fully self-contained binary distribution. The original Pocket TTS **Python installation is not required at runtime**, but existing Python model weights are not automatically compatible with this native ONNX backend.

## Build dependencies

The fork's CMake minimum has been lowered to **3.25**, accommodating Devuan 5's standard CMake. Devuan 6 has a newer release. For the raylib frontend, CMake first looks for raylib 5.x; if absent, it fetches and builds upstream raylib 5.5 during setup. The existing C++ core, CLI, and ONNX Runtime remain intact.

From your usual configured **Devuan** APT repositories (do not add Debian repositories to a Devuan machine), install:

\`\`\`sh
sudo apt-get update
sudo apt-get install -y build-essential cmake git curl ca-certificates pkg-config \
  libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev \
  libgl1-mesa-dev libasound2-dev libxext-dev libxfixes-dev libxxf86vm-dev zenity
\`\`\`

Use your distribution's administrator procedure if \`sudo\` is unavailable. Zenity is optional for reference-file selection (drag/drop or paste a path also works) but **needed for Save WAV** in this checkpoint.

\`\`\`sh
cmake -S . -B .build -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON -DBUILD_DESKTOP_TESTS=ON
cmake --build .build --parallel 2
ctest --test-dir .build --output-on-failure
\`\`\`

A successful build produces \`./pocket-tts-desktop\` and \`./pocket-tts\`. Source dependencies and ONNX Runtime are fetched at build time by the existing CMake files.

## Model files

Place these **six** separate files in the repo's \`models/\` directory:

\`\`\`text
models/
  tokenizer.model
  flow_lm_flow_int8.onnx
  flow_lm_main_int8.onnx
  mimi_decoder_int8.onnx
  mimi_encoder.onnx
  text_conditioner.onnx
\`\`\`

The initial test bootstrap offers an explicit opt-in download from:
https://huggingface.co/IgnitiveLabs/PocketTTS-ONNX

This is a third-party, preconverted ONNX repository which lists CC BY 4.0 as the model license, attributes the original model to Kyutai, and includes these filenames. **Important: The initial Devuan bootstrap used IgnitiveLabs ONNX models exported with KevinAHM's exporter. Its bundle has a different state initialization and voice-conditioning contract than PocketTTS.cpp's exporter. Those weights may generate clicks/noise even when inference succeeds. Do NOT redownload the same models to resolve distorted output.** Downloading these files should not be conflated with a successful synthesis test. Review the model card and license and do not commit model weights or personal reference voices to the public repository.

If the preconverted model layout is incompatible, the reliable fallback is to run \`export_onnx.py\` once using a temporary Python development environment and original weights. The resulting **app still needs no Python at runtime**. Do not install the Python dependencies globally simply to use the GUI.

## How to launch

\`\`\`sh
./pocket-tts-desktop
\`\`\`

The desktop program uses **in-process C++ inference**, a persistent model instance, and native audio playback. It does not start a localhost HTTP server or send telemetry intentionally.

Choose WAV, MP3, or FLAC reference audio. M4A is not directly supported by this checkpoint.

## Diagnostics

- If the window does not appear, check that a Linux desktop/X11 session is active and that CMake actually compiled \`pocket-tts-desktop\`.
- If a model file is missing, the GUI status line names the missing artifact.
- If synthesis fails, run the GUI from the terminal to capture the message.
- If ONNX reports tensor/input mismatch, do not immediately rewrite the inference engine: the third-party preconverted model might not match the fork's exporter.
- Do not share Hugging Face tokens, personally identifying local paths, or reference audio unless you intend to.

For initial testing, capture the final 30-50 lines of terminal output if something fails. Never run commands from strangers with elevated privileges without reviewing what they do.


## When the result is clicks or noise

1. Run the same reference sample through your already-working Python Pocket TTS to exclude a bad reference.
2. Run the native CLI from the test repository with the diagnostic command in scripts/audio-smoke-test.sh (see below).
3. If the native CLI WAV is also bad, the problem precedes the raylib GUI. Investigate ONNX model-export compatibility.
4. If the CLI WAV is good, compare the GUI's saved WAV with an external player to isolate raylib playback.

The initial ONNX download from IgnitiveLabs uses the KevinAHM exporter, not the one included in PocketTTS.cpp. Existing upstream issue #12 documents that the included export_onnx.py is no longer compatible with current pocket-tts package configurations (b6369a24.yaml was removed). An updated, verified export will be required before reworking the model setup. A separate upstream issue #13 also reports audible INT8 artifacts with some models. Do not install Python packages globally or discard your working Python setup.

To perform a model-free audio path test, run:

```sh
sh scripts/audio-smoke-test.sh /path/to/your/reference.wav
```

This writes the comparison WAV to a private temporary directory. It does not copy the reference into the Git checkout or upload it anywhere.
