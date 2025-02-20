#include "controls.h"
#include "dialogs.h"
#include "engine/export_prop.h"
#include <imgui.h>

namespace wb {
static ExportAudioProperties export_prop {};

static void bitrate_selectable(const char* str, uint32_t bitrate, uint32_t* value) {
    if (ImGui::Selectable(str, *value == bitrate))
        *value = bitrate;
    if (*value == bitrate)
        ImGui::SetItemDefaultFocus();
}

static void bitrate_combo_box(const char* str, uint32_t* bitrate, bool vorbis = false) {
    const char* preview;
    ImFormatStringToTempBuffer(&preview, nullptr, "%d kbps", *bitrate);
    if (ImGui::BeginCombo(str, preview)) {
        bitrate_selectable("32 kbps", 32, bitrate);
        bitrate_selectable("40 kbps", 40, bitrate);
        bitrate_selectable("48 kbps", 48, bitrate);
        bitrate_selectable("56 kbps", 56, bitrate);
        bitrate_selectable("64 kbps", 64, bitrate);
        bitrate_selectable("80 kbps", 80, bitrate);
        bitrate_selectable("96 kbps", 96, bitrate);
        bitrate_selectable("112 kbps", 112, bitrate);
        bitrate_selectable("128 kbps", 128, bitrate);
        bitrate_selectable("160 kbps", 160, bitrate);
        bitrate_selectable("192 kbps", 192, bitrate);
        bitrate_selectable("224 kbps", 224, bitrate);
        bitrate_selectable("256 kbps", 256, bitrate);
        bitrate_selectable("320 kbps", 320, bitrate);
        if (vorbis) {
            bitrate_selectable("450 kbps", 450, bitrate);
            bitrate_selectable("500 kbps", 500, bitrate);
        }
        ImGui::EndCombo();
    }
}

void export_audio() {
    ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Export audio", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Checkbox("WAV", &export_prop.enable_wav);
        ImGui::SameLine();
        ImGui::Checkbox("AIFF", &export_prop.enable_aiff);
        ImGui::SameLine();
        ImGui::Checkbox("MP3", &export_prop.enable_mp3);
        ImGui::SameLine();
        ImGui::Checkbox("Ogg Vorbis", &export_prop.enable_vorbis);
        ImGui::SameLine();
        ImGui::Checkbox("FLAC", &export_prop.enable_flac);

        ImGui::Checkbox("Export project info to file metadata", &export_prop.mp3_create_id3_tag);

        ImGui::BeginDisabled(!export_prop.enable_wav);
        {
            ImGui::SeparatorText("WAV");
            if (ImGui::RadioButton("16-bit int##wav", export_prop.wav_bit_depth == AudioFormat::I16))
                export_prop.wav_bit_depth = AudioFormat::I16;
            ImGui::SameLine();
            if (ImGui::RadioButton("24-bit int##wav", export_prop.wav_bit_depth == AudioFormat::I24))
                export_prop.wav_bit_depth = AudioFormat::I24;
            ImGui::SameLine();
            if (ImGui::RadioButton("32-bit float##wav", export_prop.wav_bit_depth == AudioFormat::F32))
                export_prop.wav_bit_depth = AudioFormat::F32;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!export_prop.enable_aiff);
        {
            ImGui::SeparatorText("AIFF");
            if (ImGui::RadioButton("16-bit int##aiff", export_prop.aiff_bit_depth == AudioFormat::I16))
                export_prop.aiff_bit_depth = AudioFormat::I16;
            ImGui::SameLine();
            if (ImGui::RadioButton("24-bit int##aiff", export_prop.aiff_bit_depth == AudioFormat::I24))
                export_prop.aiff_bit_depth = AudioFormat::I24;
            ImGui::SameLine();
            if (ImGui::RadioButton("32-bit float##aiff", export_prop.aiff_bit_depth == AudioFormat::F32))
                export_prop.aiff_bit_depth = AudioFormat::F32;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!export_prop.enable_mp3);
        {
            ImGui::SeparatorText("MP3");
            if (ImGui::RadioButton("CBR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::CBR))
                export_prop.mp3_bitrate_mode = ExportBitrateMode::CBR;
            controls::tooltip("Constant bitrate");

            ImGui::SameLine();
            if (ImGui::RadioButton("ABR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::ABR))
                export_prop.mp3_bitrate_mode = ExportBitrateMode::ABR;
            controls::tooltip("Average bitrate");

            ImGui::SameLine();
            if (ImGui::RadioButton("VBR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::VBR))
                export_prop.mp3_bitrate_mode = ExportBitrateMode::VBR;
            controls::tooltip("Variable bitrate");

            switch (export_prop.mp3_bitrate_mode) {
                case ExportBitrateMode::CBR:
                    bitrate_combo_box("Bitrate##mp3", &export_prop.mp3_bitrate);
                    break;
                case ExportBitrateMode::ABR:
                    bitrate_combo_box("Target bitrate##mp3", &export_prop.mp3_bitrate);
                    bitrate_combo_box("Min. bitrate##mp3", &export_prop.mp3_min_bitrate);
                    bitrate_combo_box("Max. bitrate##mp3", &export_prop.mp3_max_bitrate);
                    break;
                case ExportBitrateMode::VBR:
                    ImGui::SliderFloat("Quality##mp3", &export_prop.mp3_vbr_quality, 0.0f, 100.0f, "%.3f",
                                       ImGuiSliderFlags_AlwaysClamp);
                    bitrate_combo_box("Min. bitrate##mp3", &export_prop.mp3_min_bitrate);
                    bitrate_combo_box("Max. bitrate##mp3", &export_prop.mp3_max_bitrate);
                    break;
            }
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!export_prop.enable_vorbis);
        {
            ImGui::SeparatorText("Ogg Vorbis");
            if (ImGui::RadioButton("CBR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::CBR))
                export_prop.vorbis_bitrate_mode = ExportBitrateMode::CBR;
            controls::tooltip("Constant bitrate");

            ImGui::SameLine();
            if (ImGui::RadioButton("ABR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::ABR))
                export_prop.vorbis_bitrate_mode = ExportBitrateMode::ABR;
            controls::tooltip("Average bitrate");

            ImGui::SameLine();
            if (ImGui::RadioButton("VBR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::VBR))
                export_prop.vorbis_bitrate_mode = ExportBitrateMode::VBR;
            controls::tooltip("Variable bitrate");

            switch (export_prop.vorbis_bitrate_mode) {
                case ExportBitrateMode::CBR:
                    bitrate_combo_box("Bitrate##vorbis", &export_prop.vorbis_bitrate, true);
                    break;
                case ExportBitrateMode::ABR:
                    bitrate_combo_box("Target bitrate##vorbis", &export_prop.vorbis_bitrate, true);
                    bitrate_combo_box("Min. bitrate##vorbis", &export_prop.vorbis_min_bitrate, true);
                    bitrate_combo_box("Max. bitrate##vorbis", &export_prop.vorbis_max_bitrate, true);
                    break;
                case ExportBitrateMode::VBR:
                    ImGui::SliderFloat("Quality##vorbis", &export_prop.vorbis_vbr_quality, 0.0f, 100.0f, "%.3f",
                                       ImGuiSliderFlags_AlwaysClamp);
                    break;
            }
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!export_prop.enable_flac);
        {
            ImGui::SeparatorText("FLAC properties");
            if (ImGui::RadioButton("16-bit##flac", export_prop.flac_bit_depth == AudioFormat::I16))
                export_prop.flac_bit_depth = AudioFormat::I16;
            ImGui::SameLine();
            if (ImGui::RadioButton("24-bit##flac", export_prop.flac_bit_depth == AudioFormat::I24))
                export_prop.flac_bit_depth = AudioFormat::I24;
            ImGui::SliderInt("Compression level", &export_prop.flac_compression_level, 0, 8, "%d",
                             ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        if (ImGui::Button("Start")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace wb