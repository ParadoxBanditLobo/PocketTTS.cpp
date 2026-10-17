// PocketTTS Desktop: optional, local-only raylib frontend for native inference.
// Copyright (c) 2026 PocketTTS Desktop contributors. MIT (see repository LICENSE).
#include "raylib.h"
#include "preflight.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

// Existing native inference C API; the GUI links it as a static engine.
extern "C" {
void* ptt_create(const char*, const char*, const char*, const char*, float, int, int);
const char* ptt_last_error();
void ptt_destroy(void*);
void* ptt_stream_start(void*, const char*, const char*);
int ptt_stream_read(void*, float**, int*);
const char* ptt_stream_error(void*);
void ptt_stream_end(void*);
void ptt_free_audio(float*);
}

namespace fs = std::filesystem;

namespace {
constexpr int WIDTH = 850;
constexpr int HEIGHT = 690;
const Color BG = {246, 247, 250, 255};
const Color TEXT = {35, 43, 57, 255};
const Color MUTED = {94, 105, 120, 255};
const Color BLUE = {45, 97, 224, 255};
const Color BORDER = {216, 222, 232, 255};
const Color WHITE = {255, 255, 255, 255};

struct Job {
    std::atomic<bool> busy{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel{false};
    std::thread thread;
    std::mutex mutex;
    std::string failure;
    void *engine = nullptr; // only the worker and post-join cleanup touch this
};

std::string strip_line_ending(std::string input) {
    while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) input.pop_back();
    return input;
}

std::string chooser(bool save) {
    // Constant shell command; no untrusted text is interpolated. Optional dependency.
    const char *cmd = save
        ? "zenity --file-selection --save --confirm-overwrite --title='Save generated WAV' --filename='pocket-tts-output.wav' 2>/dev/null"
        : "zenity --file-selection --title='Choose reference audio' --file-filter='Audio | *.wav *.mp3 *.flac' 2>/dev/null";
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return {};
    char buf[4096] = {};
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    const int exit_code = pclose(pipe);
    return exit_code == 0 ? strip_line_ending(result) : std::string{};
}

void delete_last_codepoint(std::string &s) {
    if (s.empty()) return;
    size_t pos = s.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80) --pos;
    s.erase(pos);
}

void append_codepoint(std::string &s, int cp, size_t limit) {
    if (cp < 32 && cp != '\n') return;
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return;
    char encoded[5] = {};
    int n = 0;
    if (cp < 0x80) encoded[n++] = char(cp);
    else if (cp < 0x800) { encoded[n++] = char(0xc0 | (cp >> 6)); encoded[n++] = char(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { encoded[n++] = char(0xe0 | (cp >> 12)); encoded[n++] = char(0x80 | ((cp >> 6) & 0x3f)); encoded[n++] = char(0x80 | (cp & 0x3f)); }
    else { encoded[n++] = char(0xf0 | (cp >> 18)); encoded[n++] = char(0x80 | ((cp >> 12) & 0x3f)); encoded[n++] = char(0x80 | ((cp >> 6) & 0x3f)); encoded[n++] = char(0x80 | (cp & 0x3f)); }
    if (s.size() + size_t(n) <= limit) s.append(encoded, size_t(n));
}

void edit_field(std::string &value, bool multiline, size_t limit) {
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_A)) value.clear();
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            for (const char *p = clip; *p && value.size() < limit; ++p) {
                if (multiline || (*p != '\n' && *p != '\r')) value += *p;
            }
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) delete_last_codepoint(value);
    if (multiline && IsKeyPressed(KEY_ENTER) && !ctrl && value.size() < limit) value += '\n';
    int cp = 0;
    while ((cp = GetCharPressed()) != 0) append_codepoint(value, cp, limit);
}

bool button(Rectangle r, const char *title, bool enabled, bool primary = false) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.13f, 8, !enabled ? BORDER : primary ? (hover ? Color{29, 76, 197, 255} : BLUE) : (hover ? Color{230, 236, 248, 255} : WHITE));
    if (!primary) DrawRectangleRoundedLinesEx(r, 0.13f, 8, 1.0f, BORDER);
    int fs = 18;
    DrawText(title, int(r.x + (r.width - MeasureText(title, fs))/2), int(r.y + (r.height - fs)/2), fs, primary ? WHITE : TEXT);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

std::string tail_to_fit(const std::string &s, int max_width, int size) {
    if (MeasureText(s.c_str(), size) <= max_width) return s;
    size_t begin = 0;
    while (begin < s.size() && MeasureText(("..." + s.substr(begin)).c_str(), size) > max_width) ++begin;
    return "..." + s.substr(begin);
}

// Renders the latest visible lines without allocating textures or extra GUI libraries.
void draw_wrapped(const std::string &text, Rectangle rect, int font_size) {
    const int line_height = font_size + 7;
    const int max_lines = std::max(1, int(rect.height) / line_height);
    std::vector<std::string> lines(1);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') { lines.emplace_back(); continue; }
        // Keep UTF-8 sequences intact; wrapping at byte boundary is avoided by
        // adding one codepoint at a time.
        size_t n = 1;
        const unsigned char lead = static_cast<unsigned char>(c);
        if ((lead & 0xe0) == 0xc0) n = 2;
        else if ((lead & 0xf0) == 0xe0) n = 3;
        else if ((lead & 0xf8) == 0xf0) n = 4;
        n = std::min(n, text.size() - i);
        const std::string part = text.substr(i, n);
        if (!lines.back().empty() && MeasureText((lines.back() + part).c_str(), font_size) > rect.width) lines.emplace_back();
        lines.back() += part;
        i += n - 1;
    }
    const int from = std::max(0, int(lines.size()) - max_lines);
    BeginScissorMode(int(rect.x), int(rect.y), int(rect.width), int(rect.height));
    for (int i = from; i < int(lines.size()); ++i)
        DrawText(lines[i].c_str(), int(rect.x), int(rect.y) + (i - from)*line_height, font_size, TEXT);
    EndScissorMode();
}

fs::path executable_dir(const char *argv0) {
    std::vector<char> buf(4096);
    ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n > 0) return fs::path(std::string(buf.data(), size_t(n))).parent_path();
    return fs::absolute(argv0).parent_path();
}

void write_u16(std::ofstream &file, uint16_t v) {
    const char b[2] = {char(v & 255), char((v >> 8) & 255)};
    file.write(b, 2);
}

void write_u32(std::ofstream &file, uint32_t v) {
    const char b[4] = {char(v & 255), char((v >> 8) & 255),
                       char((v >> 16) & 255), char((v >> 24) & 255)};
    file.write(b, 4);
}

bool write_wav(const fs::path &path, const std::vector<float> &audio) {
    // 16-bit mono WAV for broad playback compatibility. Pocket TTS runs at 24 kHz.
    constexpr uint32_t sample_rate = 24000;
    constexpr uint32_t max_samples = (std::numeric_limits<uint32_t>::max() - 36) / 2;
    if (audio.empty() || audio.size() > max_samples) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write("RIFF", 4); write_u32(file, uint32_t(36 + 2 * audio.size()));
    file.write("WAVEfmt ", 8); write_u32(file, 16);
    write_u16(file, 1); write_u16(file, 1);
    write_u32(file, sample_rate); write_u32(file, sample_rate * 2);
    write_u16(file, 2); write_u16(file, 16);
    file.write("data", 4); write_u32(file, uint32_t(audio.size() * 2));
    for (float sample : audio) {
        const float safe = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
        const auto quantized = int16_t(std::lrint(safe * 32767.0f));
        write_u16(file, static_cast<uint16_t>(quantized));
    }
    file.flush();
    return file.good();
}

void generate(Job &job, fs::path models, fs::path cache, fs::path voice,
              fs::path output, std::string text) {
    if (job.thread.joinable()) job.thread.join();
    job.busy = true;
    job.finished = false;
    job.cancel = false;
    job.failure.clear();
    job.thread = std::thread([&job, models, cache, voice, output, text = std::move(text)] {
        std::string failure;
        const fs::path temp = fs::path(output.string() + ".part.wav");
        std::error_code ec;
        fs::remove(temp, ec);
        try {
            // Initialize ONNX sessions only once. Voice models are reused.
            if (!job.engine) {
                const std::string model_path = models.string();
                const std::string tokenizer_path = (models / "tokenizer.model").string();
                const std::string voice_cache = cache.string();
                job.engine = ptt_create(model_path.c_str(), voice_cache.c_str(),
                    tokenizer_path.c_str(), "int8", 0.7f, 1, 0);
                if (!job.engine) {
                    const char *detail = ptt_last_error();
                    throw std::runtime_error(detail && *detail ? detail : "Failed to load Pocket TTS model files.");
                }
            }
            const std::string reference = voice.string();
            void *stream = ptt_stream_start(job.engine, text.c_str(), reference.c_str());
            if (!stream) throw std::runtime_error("Unable to start speech generation.");
            std::vector<float> samples;
            samples.reserve(24000 * 5);
            bool failed = false;
            while (true) {
                float *chunk = nullptr;
                int count = 0;
                const int result = ptt_stream_read(stream, &chunk, &count);
                if (result <= 0) {
                    if (result < 0) {
                        const char *detail = ptt_stream_error(stream);
                        failure = detail && *detail ? detail : "Speech generation failed.";
                        failed = true;
                    }
                    break;
                }
                if (count <= 0 || chunk == nullptr || samples.size() + size_t(count) > 24000u * 300u) {
                    ptt_free_audio(chunk);
                    failure = "Invalid or oversized audio output.";
                    failed = true;
                    break;
                }
                samples.insert(samples.end(), chunk, chunk + count);
                ptt_free_audio(chunk);
                if (job.cancel) { failed = true; failure = "Generation cancelled."; break; }
            }
            ptt_stream_end(stream);
            if (!failed) {
                if (!write_wav(temp, samples)) throw std::runtime_error("Could not write the generated WAV.");
                fs::rename(temp, output, ec);
                if (ec) throw std::runtime_error("Unable to finalize the generated WAV.");
            }
        } catch (const std::exception &e) {
            failure = e.what();
        }
        if (!failure.empty()) fs::remove(temp, ec);
        {
            std::lock_guard<std::mutex> lock(job.mutex);
            job.failure = failure;
        }
        job.busy = false;
        job.finished = true;
    });
}

}

int main(int argc, char **argv) {
    (void)argc;
    const fs::path base = executable_dir(argv[0]);
    const fs::path models = base / "models";
    fs::path cache;
    const char *xdg_cache = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (xdg_cache && *xdg_cache) cache = fs::path(xdg_cache) / "pockettts-desktop";
    else if (home && *home) cache = fs::path(home) / ".cache/pockettts-desktop";
    else cache = fs::temp_directory_path() / "pockettts-desktop";
    std::error_code ec;
    fs::create_directories(cache, ec);
    fs::permissions(cache, fs::perms::owner_all, fs::perm_options::replace, ec);

    fs::path tmp = fs::temp_directory_path() / "pockettts-desktop-XXXXXX";
    std::string tmp_pattern = tmp.string();
    std::vector<char> mutable_pattern(tmp_pattern.begin(), tmp_pattern.end());
    mutable_pattern.push_back('\0');
    if (!mkdtemp(mutable_pattern.data())) {
        fprintf(stderr, "Cannot create private temporary directory\n");
        return 1;
    }
    const fs::path session_dir(mutable_pattern.data());
    const fs::path output = session_dir / "generated.wav";

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIDTH, HEIGHT, "Pocket TTS Desktop");
    SetWindowMinSize(740, 630);
    InitAudioDevice();
    SetTargetFPS(60);

    std::string text = "Hello. This is my Pocket TTS voice.";
    std::string voice;
    std::string status = "Choose a reference recording to begin. Model loads on first use.";
    bool edit_text = true;
    bool edit_voice = false;
    bool has_audio = false;
    bool playing = false;
    Sound sound{};
    Job job;
    while (!WindowShouldClose()) {
        const int w = GetScreenWidth();
        const int h = GetScreenHeight();
        const float x = 32, ww = float(w) - 64;
        const Rectangle text_box = {x, 92, ww, 190};
        const Rectangle voice_box = {x, 338, ww - 160, 46};
        const Rectangle generate_button = {x, 422, ww, 48};
        const Rectangle play_button = {x, 537, 145, 44};
        const Rectangle save_button = {x + 162, 537, 165, 44};

        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count && dropped.paths[0] && ptt_desktop::is_audio_path(dropped.paths[0])) {
                voice = dropped.paths[0];
                status = "Reference audio selected.";
            }
            UnloadDroppedFiles(dropped);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 point = GetMousePosition();
            edit_text = CheckCollisionPointRec(point, text_box);
            edit_voice = CheckCollisionPointRec(point, voice_box);
        }
        if (edit_text) edit_field(text, true, 3000);
        else if (edit_voice) edit_field(voice, false, 2048);

        if (job.finished.exchange(false)) {
            if (job.thread.joinable()) job.thread.join();
            std::string error;
            { std::lock_guard<std::mutex> lock(job.mutex); error = job.failure; }
            if (!error.empty()) status = error;
            else {
                if (has_audio) { StopSound(sound); UnloadSound(sound); has_audio = false; }
                sound = LoadSound(output.string().c_str());
                has_audio = IsSoundReady(sound);
                status = has_audio ? "Generated audio is ready. Press Play or Save WAV." : "WAV generated, but playback could not load it.";
            }
        }
        playing = has_audio && IsSoundPlaying(sound);
        const bool can_generate = !job.busy && !text.empty() && !voice.empty();
        const bool can_save = has_audio && !job.busy;

        BeginDrawing();
        ClearBackground(BG);
        DrawText("Pocket TTS", int(x), 23, 29, TEXT);
        DrawText("Standalone offline voice cloning", int(x), 59, 16, MUTED);
        DrawText("Text", int(x), 72, 16, TEXT);
        DrawRectangleRounded(text_box, .045f, 8, WHITE);
        DrawRectangleRoundedLinesEx(text_box, .045f, 8, 1.2f, edit_text ? BLUE : BORDER);
        draw_wrapped(text, {x+15, 107, ww-30, 160}, 19);
        DrawText("Reference audio   (WAV / MP3 / FLAC)", int(x), 307, 17, TEXT);
        DrawRectangleRounded(voice_box, .13f, 8, WHITE);
        DrawRectangleRoundedLinesEx(voice_box, .13f, 8, 1.2f, edit_voice ? BLUE : BORDER);
        DrawText(voice.empty() ? "Drag an audio file here or select Browse..." : tail_to_fit(voice, int(voice_box.width)-20, 17).c_str(),
                 int(x+12), 350, 17, voice.empty() ? MUTED : TEXT);
        if (button({x+ww-148, 338, 148, 46}, "Browse...", !job.busy)) {
            const auto chosen = chooser(false);
            if (!chosen.empty()) { voice = chosen; status = "Reference audio selected."; }
            else status = "File picker unavailable or cancelled. Drag a file here or paste its path.";
        }
        if (button(generate_button, job.busy ? "Generating..." : "Generate Audio", can_generate, true)) {
            const auto missing = ptt_desktop::validate_models(models);
            const auto invalid = ptt_desktop::validate_reference(voice);
            if (!missing.empty()) status = missing;
            else if (!invalid.empty()) status = invalid;
            else {
                if (has_audio) { StopSound(sound); UnloadSound(sound); has_audio = false; }
                status = "Generating speech locally...";
                generate(job, models, cache, fs::absolute(voice), output, text);
            }
        }
        DrawText(tail_to_fit(status, int(ww), 16).c_str(), int(x), 491, 16, job.busy ? BLUE : MUTED);
        DrawText("Generated audio", int(x), 516, 18, TEXT);
        if (button(play_button, playing ? "Stop" : "Play", has_audio)) {
            if (playing) StopSound(sound); else PlaySound(sound);
        }
        if (button(save_button, "Save WAV...", can_save)) {
            std::string path = chooser(true);
            if (path.empty()) status = "Save cancelled. Install zenity to enable the native Save dialog.";
            else {
                fs::copy_file(output, path, fs::copy_options::overwrite_existing, ec);
                status = ec ? "Unable to write the chosen WAV file." : "WAV saved successfully.";
            }
        }
        DrawText("Direct native synthesis. No Python runtime or HTTP server.", int(x), h - 39, 15, MUTED);
        EndDrawing();
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_ENTER) && can_generate) {
            const auto missing = ptt_desktop::validate_models(models);
            const auto invalid = ptt_desktop::validate_reference(voice);
            if (!missing.empty()) status = missing;
            else if (!invalid.empty()) status = invalid;
            else {
                if (has_audio) { StopSound(sound); UnloadSound(sound); has_audio = false; }
                status = "Generating speech locally...";
                generate(job, models, cache, fs::absolute(voice), output, text);
            }
        }
    }

    if (job.busy) job.cancel = true;
    if (job.thread.joinable()) job.thread.join();
    if (job.engine) ptt_destroy(job.engine);
    if (has_audio) UnloadSound(sound);
    CloseAudioDevice();
    CloseWindow();
    fs::remove_all(session_dir, ec);
    return 0;
}
