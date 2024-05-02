#include "catch_amalgamated.hpp"
#include "core/audio_buffer.h"
#include <random>
#include <vector>

TEST_CASE("AudioBuffer construct") {
    wb::AudioBuffer<float> buffer(128, 2);
    REQUIRE(buffer.n_samples == 128);
    REQUIRE(buffer.n_channels == 2);

    for (uint32_t i = 0; i < buffer.n_channels; i++) {
        REQUIRE(buffer.get_read_pointer(i) != nullptr);
    }
}

TEST_CASE("AudioBuffer resize buffer") {
    SECTION("resize buffer with clearing") {
        wb::AudioBuffer<float> buffer(128, 2);
        buffer.resize(256, true);
        REQUIRE(buffer.n_samples == 256);
    }

    std::random_device rd;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    SECTION("expand buffer without clearing") {
        std::vector<float> random_samples(256);
        wb::AudioBuffer<float> buffer(256, 2);

        // Store random samples
        for (auto& sample : random_samples) {
            sample = dist(rd);
        }

        // Load random samples into audio channel buffer
        for (uint32_t c = 0; c < buffer.n_channels; c++) {
            float* channel = buffer.get_write_pointer(c);
            for (uint32_t i = 0; i < buffer.n_samples; i++) {
                channel[i] = random_samples[i];
            }
        }

        // Resize the buffer without clearing the old data
        buffer.resize(512);
        REQUIRE(buffer.n_samples == 512);

        // Compare the samples
        for (uint32_t c = 0; c < buffer.n_channels; c++) {
            int result = std::memcmp(buffer.get_read_pointer(c), random_samples.data(),
                                     random_samples.size() * sizeof(float));
            REQUIRE(result == 0);
        }
    }
}