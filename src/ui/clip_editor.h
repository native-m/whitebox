#pragma once

#include "engine/track.h"

namespace wb {

extern bool g_clip_editor_window_open;

void clip_editor_init();
void clip_editor_shutdown();
void clip_editor_set_clip(TrackID track_id, ClipID clip_id);
void clip_editor_unset_clip();
Clip* clip_editor_get_clip();
Track* clip_editor_get_track();
void render_clip_editor();

}  // namespace wb