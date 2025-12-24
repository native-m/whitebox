#pragma once

namespace wb {

void timeline_init();
void timeline_shutdown();
void timeline_redraw_window();
void timeline_add_track();
void timeline_set_scroll_position(double start, double end);
void timeline_update_song_length();
double timeline_get_start_scroll();
double timeline_get_end_scroll();
void render_timeline();

}  // namespace wb