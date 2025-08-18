#pragma once

#include "core/common.h"
#include "dsp/sample.h"
#include "gfx/waveform_visual.h"

namespace wb {

struct AudioAsset {
  uint64_t hash;
  Sample sample;
  WaveformVisual* waveform_visual;
  uint32_t ref_count;
};

}  // namespace wb