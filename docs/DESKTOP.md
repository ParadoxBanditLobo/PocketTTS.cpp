# Pocket TTS Desktop — Checkpoint A

This is an optional Linux raylib desktop frontend linked directly to PocketTTS.cpp's native C API. It follows the original browser demonstration's core workflow: type text, choose a reference recording, generate audio, play it, and save a WAV.

**Scope:** The GUI now uses the native streaming C API in-process. Models are loaded once on the first generation, then reused for subsequent phrases. The original CLI remains independently usable. This is still a development checkpoint rather than a tested binary release.

## Building on Linux

The upstream requirements remain CMake 3.28+, a C++17 compiler, ONNX Runtime, SentencePiece, and model files. Install raylib **5.0 or newer** development files to enable the additional GUI target (the CLI still builds if raylib is absent). A GUI file dialog uses **zenity if installed**; it is optional for selecting references because file drag-and-drop and pasting an absolute path also work. Saving WAV currently requires zenity.

From the project root:

```sh
cmake -B .build -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
cmake --build .build -j$(nproc)
./pocket-tts-desktop
```

The original CLI and new GUI executables are built beside the repository's CMakeLists.txt. The GUI **does not launch or depend on the CLI at runtime**. It loads the native engine directly and expects preconverted ONNX models in \`models/\`, including \`tokenizer.model\`. The CMake GUI target copies a matching ONNX Runtime shared library beside the GUI.

### Models and Python

**Python is not used by the desktop application or the existing C++ inference engine.** Upstream's \`export_onnx.py\` currently requires Python **once during model conversion** if you do not already have compatible, properly licensed ONNX files. This milestone does not distribute or automatically download weights. See the upstream README's "Export ONNX Models" section. Model/voice terms must be checked before redistributing them.

### How to use

1. Enter text in the upper editor.
2. Drag a WAV, MP3, or FLAC sample onto the window, select **Browse**, or paste the file's absolute path in the reference field. The original engine supports WAV/MP3/FLAC.
3. Press **Generate Audio** (or Ctrl+Enter). The GUI uses in-process ONNX synthesis without a CLI subprocess or web server. The first generation initializes the model.
4. Use **Play** to listen; **Save WAV** opens a file dialog if zenity is available.

Generated audio is written to a private temporary folder and removed when the app exits. Cached reference embeddings and KV states are stored under \`$XDG_CACHE_HOME/pockettts-desktop/\` or \`~/.cache/pockettts-desktop/\`. The upstream cache key has been corrected in this fork to include a stable hash of the reference path, preventing identical-filename collisions. This changes cache filenames once.

### Current limitations

- The engine is reused after first initialization. Replacing an audio file **in place** may require restarting the GUI to invalidate its in-memory voice representation.
- No progress percentage or waveform yet; the GUI indicates whether generation is running.
- Text editing is append/backspace, Ctrl+V, and Ctrl+A to clear; it is not yet a complete text editor with cursor movement/selection. raylib's default font may not draw every Unicode character.
- The original engine supports MP3/FLAC/WAV inputs. M4A conversion has **not** been added.
- **Save WAV requires optional zenity** in this checkpoint. No separate native file-dialog library is linked.
- Model initialization and generation errors are reported in the GUI status line where possible. Very long errors are truncated visually; terminal logs may have additional details.
- The desktop C++ source has passed a compiler syntax check using a raylib API stub. A standalone test also verified output WAV headers/PCM format. **Full raylib+ONNX integration, voice quality, and playback have not yet been tested** with model files.

### Privacy and network audit (first pass)

- The desktop source makes **no HTTP requests**; it links the native engine directly and plays audio via raylib. The desktop path never launches the existing HTTP server.
- The original C++ code still contains an optional \`--server\` HTTP feature. The server binds to all interfaces in the upstream source if explicitly enabled; do not expose it on an untrusted network. It is **not started by this GUI**.
- The upstream build downloads pinned runtime libraries and retrieves source dependencies via CMake. The **model export** step may access Hugging Face. These are setup/build activities, distinct from synthesis.
- No active telemetry sender was identified in the inspected core/GUI paths, but this is not a comprehensive dependency security audit.
- Do not commit personal voice recordings or secrets. Existing \`.gitignore\` excludes \`voices/\`, model weights, and build files. Saved WAV files outside ignored directories still need care in a public repository.

### Next checkpoints

1. Real Linux/audio acceptance test with reference recordings and INT8 models.
2. Validate persistent model reuse and repeated voice/reference switching with real recordings.
3. Improve the text editor and file dialog fallbacks; add optional visible generation timing.
4. Distributable unpack-and-run package after successful real-device tests.
5. Keep the inference API usable independently by ZenoVox without importing the raylib UI.

Code credits: VolgaGerm/PocketTTS.cpp (MIT), Kyutai Pocket TTS (MIT code; independent model terms), raylib (zlib/libpng).


## Offline preflight (third checkpoint)

The GUI now checks for all six required INT8 model/tokenizer files and rejects missing
or empty reference recordings **before** starting the ONNX runtime. It reports the
first missing model filename directly in the status line.

To run basic model-free validation tests:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic desktop/preflight_test.cpp -o /tmp/pockettts-preflight-test
/tmp/pockettts-preflight-test
```

Alternatively, configure CMake with `-DBUILD_DESKTOP_TESTS=ON` and run
`ctest --test-dir .build --output-on-failure`. The preflight test itself
does not need a model download or raylib. Full integration and audio fidelity
tests are still pending on Linux with real model files.
