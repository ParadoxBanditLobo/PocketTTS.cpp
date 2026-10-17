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

This is a third-party, preconverted ONNX repository which lists CC BY 4.0 as the model license, attributes the original model to Kyutai, and includes these filenames. **Compatibility with this exact C++ implementation has not yet been verified by audio testing.** Downloading these files should not be conflated with a successful synthesis test. Review the model card and license and do not commit model weights or personal reference voices to the public repository.

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
