// Offline input/model validation shared by the raylib frontend and its tests.
// Copyright (c) 2026 PocketTTS Desktop contributors. MIT (see LICENSE).
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace ptt_desktop {
namespace fs = std::filesystem;

inline bool is_audio_path(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    return extension == ".wav" || extension == ".mp3" || extension == ".flac";
}

inline std::string validate_reference(const fs::path& path) {
    if (path.empty()) return "Choose a WAV, MP3 or FLAC reference recording.";
    if (!is_audio_path(path)) return "Reference file must be WAV, MP3 or FLAC.";
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error)
        return "Reference audio file was not found.";
    const auto bytes = fs::file_size(path, error);
    if (error || bytes == 0) return "Reference audio file is empty or unreadable.";
    return {};
}

// Lists the first required INT8 ONNX artifact that is missing or empty.
// This avoids initializing a costly inference engine before detecting setup
// mistakes. Model weights are never read or downloaded by this function.
inline std::string validate_models(const fs::path& models) {
    constexpr std::array<const char*, 6> files = {{
        "tokenizer.model",
        "flow_lm_flow_int8.onnx",
        "flow_lm_main_int8.onnx",
        "mimi_decoder_int8.onnx",
        "mimi_encoder.onnx",
        "text_conditioner.onnx"
    }};
    for (const char* file : files) {
        const auto path = models / file;
        std::error_code error;
        if (!fs::is_regular_file(path, error) || error)
            return std::string("Missing required model file: ") + file;
        const auto size = fs::file_size(path, error);
        if (error || size == 0)
            return std::string("Empty or unreadable model file: ") + file;
    }
    return {};
}

} // namespace ptt_desktop
