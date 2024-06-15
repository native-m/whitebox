#include "catch_amalgamated.hpp"
#include "engine/audio_io.h"

TEST_CASE("PulseAudioDeviceList get devices") {
    wb::AudioIO* audio_io = wb::create_audio_io_pulseaudio();

    auto output_devices = audio_io->get_output_device_count();
    auto input_devices = audio_io->get_input_device_count();

    REQUIRE(output_devices > 0);
    REQUIRE(input_devices > 0);

    auto output_device = audio_io->get_output_device_properties(0);
    auto input_device = audio_io->get_input_device_properties(0);

    REQUIRE(output_device.name != "");
    REQUIRE(input_device.name != "");

    delete audio_io;
}