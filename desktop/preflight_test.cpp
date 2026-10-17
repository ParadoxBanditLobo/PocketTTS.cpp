// Dependency-free smoke tests for offline input/model preflight.
#include "preflight.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

int main() {
    namespace fs = std::filesystem;
    using namespace ptt_desktop;

    assert(is_audio_path("voice.WAV"));
    assert(is_audio_path("voice.mp3"));
    assert(is_audio_path("voice.FlAc"));
    assert(!is_audio_path("voice.m4a"));
    assert(!is_audio_path("voice.txt"));

    const auto root = fs::temp_directory_path() / ("pockettts-preflight-" + std::to_string(getpid()));
    fs::create_directories(root / "models");
    const auto model_dir = root / "models";
    const auto sample = root / "voice.wav";

    assert(validate_models(model_dir).find("tokenizer.model") != std::string::npos);
    assert(!validate_reference(sample).empty());

    std::ofstream(sample) << "sample";
    assert(validate_reference(sample).empty());
    std::ofstream(sample, std::ios::trunc).close();
    assert(validate_reference(sample).find("empty") != std::string::npos);

    const std::vector<std::string> files{
        "tokenizer.model", "flow_lm_flow_int8.onnx", "flow_lm_main_int8.onnx",
        "mimi_decoder_int8.onnx", "mimi_encoder.onnx", "text_conditioner.onnx"
    };
    for (const auto& name : files) {
        assert(!validate_models(model_dir).empty());
        std::ofstream(model_dir / name) << "stub";
    }
    assert(validate_models(model_dir).empty());
    std::ofstream(model_dir / "mimi_decoder_int8.onnx", std::ios::trunc).close();
    assert(validate_models(model_dir).find("mimi_decoder_int8.onnx") != std::string::npos);

    fs::remove_all(root);
    std::cout << "PASS: audio validation, missing/empty INT8 artifacts, preflight\n";
}
