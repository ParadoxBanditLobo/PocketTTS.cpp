# Pocket TTS Desktop — Checkpoint A

This is an optional Linux raylib desktop frontend for the existing native PocketTTS.cpp CLI. It follows the original browser demonstration's core workflow: type text, choose a reference recording, generate audio, play it, and save a WAV.

**Scope:** This checkpoint is a functional first-pass GUI, not yet a packaged binary release. The model inference engine and original CLI have not been changed. Voice generation currently launches the CLI asynchronously for each request; keeping the model resident and improving first-audio latency are next steps.

## Building on Linux

The upstream requirements remain CMake 3.28+, a C++17 compiler, ONNX Runtime, SentencePiece, and model files. Install raylib **5.0 or newer** development files to enable the additional GUI target (the CLI still builds if raylib is absent). A GUI file dialog uses **zenity if installed**; it is optional for selecting references because file drag-and-drop and pasting an absolute path also work. Saving WAV currently requires zenity.

From the project root:

```sh
cmake -B .build -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
cmake --build .build -j$(nproc)
./pocket-tts-desktop
```

The two executables are built beside the repository's CMakeLists.txt because of upstream output settings. The GUI expects the CLI executable \`pocket-tts\` beside \`pocket-tts-desktop\`, and expects preconverted ONNX models in \`models/\`, including \`tokenizer.model\`.

### Models and Python

**Python is not used by the desktop application or the existing C++ inference engine.** Upstream's \`export_onnx.py\` currently requires Python **once during model conversion** if you do not already have compatible, properly licensed ONNX files. This milestone does not distribute or automatically download weights. See the upstream README's "Export ONNX Models" section. Model/voice terms must be checked before redistributing them.

### How to use

1. Enter text in the upper editor.
2. Drag a WAV, MP3, or FLAC sample onto the window, select **Browse**, or paste the file's absolute path in the reference field. The original engine supports WAV/MP3/FLAC.
3. Press **Generate Audio** (or Ctrl+Enter). The GUI launches the existing CLI locally without a web server.
4. Use **Play** to listen; **Save WAV** opens a file dialog if zenity is available.

Generated audio is written to a private temporary folder and removed when the app exits. Cached reference embeddings and KV states are stored under \`$XDG_CACHE_HOME/pockettts-desktop/\` or \`~/.cache/pockettts-desktop/\`. Different reference paths use isolated subdirectories to avoid identical-filename collisions.

### Current limitations

- Models are loaded again for each generation. This is intentionally an integration checkpoint; a persistent native backend should follow.
- No progress percentage or waveform yet; the GUI indicates whether generation is running.
- Text editing is append/backspace, Ctrl+V, and Ctrl+A to clear; it is not yet a complete text editor with cursor movement/selection. raylib's default font may not draw every Unicode character.
- The original engine supports MP3/FLAC/WAV inputs. M4A conversion has **not** been added.
- **Save WAV requires optional zenity** in this checkpoint. No separate native file-dialog library is linked.
- On synthesis errors, the GUI reports a generic message. The underlying CLI can be run in a terminal for detailed diagnostics.
- This checkpoint has only been source-level syntax-checked in a sandbox without raylib or model files. End-to-end GUI, voice quality, and audio playback need testing on a Linux machine with the required dependencies and models.

### Privacy and network audit (first pass)

- The new desktop source makes **no HTTP requests**; it invokes the local CLI with explicit file paths and runs raylib audio playback. Its synthesis path does not start the existing HTTP server.
- The original C++ code still contains an optional \`--server\` HTTP feature. The server binds to all interfaces in the upstream source if explicitly enabled; do not expose it on an untrusted network. It is **not started by this GUI**.
- The upstream build downloads pinned runtime libraries and retrieves source dependencies via CMake. The **model export** step may access Hugging Face. These are setup/build activities, distinct from synthesis.
- No active telemetry sender was identified in the inspected core/GUI paths, but this is not a comprehensive dependency security audit.
- Do not commit personal voice recordings or secrets. Existing \`.gitignore\` excludes \`voices/\`, model weights, and build files. Saved WAV files outside ignored directories still need care in a public repository.

### Next checkpoints

1. Real Linux/audio acceptance test with reference recordings and INT8 models.
2. Integrate the persistent C API to avoid loading the models each time.
3. Improve file dialogs and text editor; add clearer model/error diagnostics.
4. Distributable unpack-and-run package after successful real-device tests.
5. Keep the inference API usable independently by ZenoVox without importing the raylib UI.

Code credits: VolgaGerm/PocketTTS.cpp (MIT), Kyutai Pocket TTS (MIT code; independent model terms), raylib (zlib/libpng).
