#include <imgui.h>
#include <imgui_stdlib.h>

#include "../codec/codec.h"
#include "config.h"
#include "controls.h"
#include "core/bit_manipulation.h"
#include "core/deferred_job.h"
#include "dialogs.h"
#include "engine/engine.h"
#include "export_prop.h"

namespace wb {

static const char* export_error_msg_no_filename = "Filename must be specified";
static const char* export_error_msg_no_location = "Location must be specified";

static ExportAudioProperties export_prop{};
static bool is_rendering = false;
static bool request_stop = false;
static float progress;
static const char* export_error_msg;

static void bitrate_selectable(const char* str, uint32_t bitrate, uint32_t* value) {
  if (ImGui::Selectable(str, *value == bitrate))
    *value = bitrate;
  if (*value == bitrate)
    ImGui::SetItemDefaultFocus();
}

static void bitrate_combo_box(const char* str, uint32_t* bitrate, bool vorbis = false) {
  char preview[10]{};
  ImFormatString(preview, sizeof(preview), "%d kbps", *bitrate);
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

static void export_job_runner(DeferredJobContext* ctx) {
  using namespace std::chrono_literals;
  ExportAudioProperties* props = (ExportAudioProperties*)ctx->userdata0;
  int num_formats = std::popcount(props->format_flags);
  double song_length = g_engine.get_song_length(0.0);
  wm_enable_taskbar_progress_indicator(true);

  if (song_length > 0.0) {
    if (num_formats == 1) {
#if 0
      dsp::AudioEncoder* encoder = nullptr;
      std::string filepath = props->location + '/' + props->filename;

      switch (props->format_flags) {
        case ExportAudioProperties::WAV:
          encoder = new dsp::AudioSFEncoder(dsp::AudioSFEncoder::WAV, props->wav_bit_depth);
          filepath += ".wav";
          break;
        case ExportAudioProperties::AIFF:
          encoder = new dsp::AudioSFEncoder(dsp::AudioSFEncoder::AIFF, props->aiff_bit_depth);
          filepath += ".aiff";
          break;
        case ExportAudioProperties::MP3: break;
        case ExportAudioProperties::Vorbis: break;
        case ExportAudioProperties::FLAC: break;
        default: break;
      }

      if (!encoder->open(filepath, 2)) {
        // TODO(native-m): Close this file.
        delete encoder;
        goto exit_export;
      }
#endif
      for (int i = 0; i < 10; i++) {
        if (ctx->request_stop)
          break;
        std::this_thread::sleep_for(1s);
        progress += 0.1f;
        wm_set_taskbar_progress_value(progress);
        Log::debug("Progress: {}", progress * 100.0f);
      }

    } else if (num_formats > 1) {
      // TODO(native-m): Implement multi-format export
    }
  }

exit_export:
  wm_enable_taskbar_progress_indicator(false);
  progress = 0.0f;
  is_rendering = false;
  request_stop = false;
  delete props;
}

void export_audio_dialog() {
  ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(
          "Export audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::CheckboxFlags("WAV", &export_prop.format_flags, ExportAudioProperties::WAV);
    controls::item_tooltip("Export to WAV");
    ImGui::SameLine();
    ImGui::CheckboxFlags("AIFF", &export_prop.format_flags, ExportAudioProperties::AIFF);
    controls::item_tooltip("Export to AIFF");
    ImGui::SameLine();
    ImGui::CheckboxFlags("MP3", &export_prop.format_flags, ExportAudioProperties::MP3);
    controls::item_tooltip("Export to MP3");
    ImGui::SameLine();
    ImGui::CheckboxFlags("Ogg Vorbis", &export_prop.format_flags, ExportAudioProperties::Vorbis);
    controls::item_tooltip("Export to Ogg Vorbis");
    ImGui::SameLine();
    ImGui::CheckboxFlags("FLAC", &export_prop.format_flags, ExportAudioProperties::FLAC);
    controls::item_tooltip("Export to FLAC");

    ImGui::Checkbox("Export project info to file metadata", &export_prop.export_metadata);
    ImGui::InputText("Filename", &export_prop.filename);
    ImGui::InputText("##location", &export_prop.location, ImGuiInputTextFlags_ElideLeft);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::Button("Pick directory");

    ImGui::BeginDisabled(!has_bit(export_prop.format_flags, ExportAudioProperties::WAV));
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

    ImGui::BeginDisabled(!has_bit(export_prop.format_flags, ExportAudioProperties::AIFF));
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

    ImGui::BeginDisabled(!has_bit(export_prop.format_flags, ExportAudioProperties::MP3));
    {
      ImGui::SeparatorText("MP3");
      if (ImGui::RadioButton("CBR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::CBR))
        export_prop.mp3_bitrate_mode = ExportBitrateMode::CBR;
      controls::item_tooltip("Constant bitrate");

      ImGui::SameLine();
      if (ImGui::RadioButton("ABR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::ABR))
        export_prop.mp3_bitrate_mode = ExportBitrateMode::ABR;
      controls::item_tooltip("Average bitrate");

      ImGui::SameLine();
      if (ImGui::RadioButton("VBR##mp3", export_prop.mp3_bitrate_mode == ExportBitrateMode::VBR))
        export_prop.mp3_bitrate_mode = ExportBitrateMode::VBR;
      controls::item_tooltip("Variable bitrate");

      switch (export_prop.mp3_bitrate_mode) {
        case ExportBitrateMode::CBR: bitrate_combo_box("Bitrate##mp3", &export_prop.mp3_bitrate); break;
        case ExportBitrateMode::ABR:
          bitrate_combo_box("Target bitrate##mp3", &export_prop.mp3_bitrate);
          bitrate_combo_box("Min. bitrate##mp3", &export_prop.mp3_min_bitrate);
          bitrate_combo_box("Max. bitrate##mp3", &export_prop.mp3_max_bitrate);
          break;
        case ExportBitrateMode::VBR:
          ImGui::SliderFloat(
              "Quality##mp3", &export_prop.mp3_vbr_quality, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          bitrate_combo_box("Min. bitrate##mp3", &export_prop.mp3_min_bitrate);
          bitrate_combo_box("Max. bitrate##mp3", &export_prop.mp3_max_bitrate);
          break;
        default: break;
      }
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!has_bit(export_prop.format_flags, ExportAudioProperties::Vorbis));
    {
      ImGui::SeparatorText("Ogg Vorbis");
      if (ImGui::RadioButton("CBR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::CBR))
        export_prop.vorbis_bitrate_mode = ExportBitrateMode::CBR;
      controls::item_tooltip("Constant bitrate");

      ImGui::SameLine();
      if (ImGui::RadioButton("ABR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::ABR))
        export_prop.vorbis_bitrate_mode = ExportBitrateMode::ABR;
      controls::item_tooltip("Average bitrate");

      ImGui::SameLine();
      if (ImGui::RadioButton("VBR##vorbis", export_prop.vorbis_bitrate_mode == ExportBitrateMode::VBR))
        export_prop.vorbis_bitrate_mode = ExportBitrateMode::VBR;
      controls::item_tooltip("Variable bitrate");

      switch (export_prop.vorbis_bitrate_mode) {
        case ExportBitrateMode::CBR: bitrate_combo_box("Bitrate##vorbis", &export_prop.vorbis_bitrate, true); break;
        case ExportBitrateMode::ABR:
          bitrate_combo_box("Target bitrate##vorbis", &export_prop.vorbis_bitrate, true);
          bitrate_combo_box("Min. bitrate##vorbis", &export_prop.vorbis_min_bitrate, true);
          bitrate_combo_box("Max. bitrate##vorbis", &export_prop.vorbis_max_bitrate, true);
          break;
        case ExportBitrateMode::VBR:
          ImGui::SliderFloat(
              "Quality##vorbis", &export_prop.vorbis_vbr_quality, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          break;
        default: break;
      }
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!has_bit(export_prop.format_flags, ExportAudioProperties::FLAC));
    {
      ImGui::SeparatorText("FLAC");
      if (ImGui::RadioButton("16-bit##flac", export_prop.flac_bit_depth == AudioFormat::I16))
        export_prop.flac_bit_depth = AudioFormat::I16;
      ImGui::SameLine();
      if (ImGui::RadioButton("24-bit##flac", export_prop.flac_bit_depth == AudioFormat::I24))
        export_prop.flac_bit_depth = AudioFormat::I24;
      ImGui::SliderInt("Compression level", &export_prop.flac_compression_level, 0, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
      controls::item_tooltip(
          "0-4: Faster compression speed, large file size.\n"
          "5-8: Slower compression speed, small file size.\n");
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), nullptr);

    static std::optional<DeferredJobHandle> export_job;
    bool show_empty_song_msg = false;
    bool disable_render = std::popcount(export_prop.format_flags) == 0 && !is_rendering;

    ImGui::BeginDisabled(request_stop || disable_render);
    if (!is_rendering) {
      if (ImGui::Button("Start")) {
        if (export_prop.filename.empty()) {
          export_error_msg = export_error_msg_no_filename;
        } else if (export_prop.location.empty()) {
          export_error_msg = export_error_msg_no_location;
        } else if (g_engine.get_song_length(0.0) > 0.0) {
          auto props = new ExportAudioProperties(export_prop);  // Copy current properties
          is_rendering = true;
          wait_for_all_deferred_job();
          export_job = enqueue_deferred_job(&export_job_runner, props);
        } else {
          ImGui::OpenPopup("Empty song##empty_song_dlg");
        }
      }
    } else {
      if (ImGui::Button(request_stop ? "Aborting..." : "Abort")) {
        if (export_job.has_value()) {
          request_stop = true;
          stop_deferred_job(export_job.value());
        }
        export_job.reset();
      }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      is_rendering = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (export_error_msg)
      ImGui::Text(export_error_msg);

    confirm_dialog("Empty song##empty_song_dlg", "Cannot render empty song.", ConfirmDialog::Ok);
    confirm_dialog("Filename empty##filename_empty_dlg", "Filename must be filled.", ConfirmDialog::Ok);

    ImGui::EndPopup();
  }
}

}  // namespace wb