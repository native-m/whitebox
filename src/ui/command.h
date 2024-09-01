#pragma once

#include "core/common.h"
#include "engine/assets_table.h"
#include "engine/clip.h"
#include <string>

namespace wb {

struct ClipHistory {
    uint32_t clip_id {};
    uint32_t track_id {};
    ClipType type {};
    std::string name {};
    ImColor color {};
    bool active {};
    double min_time {};
    double max_time {};
    double start_offset {};

    union {
        AudioClip audio;
        MidiClip midi;
    };

    ClipHistory();
    ClipHistory(const Clip& clip, uint32_t track_id);
};

struct ClipMoveCmd {
    uint32_t track_id;
    uint32_t target_track_id;
    uint32_t clip_id;
    double relative_pos;

    void execute();
    void undo();
};

struct ClipShiftCmd {
    uint32_t track_id;
    uint32_t clip_id;
    double relative_pos;
    double last_beat_duration;

    void execute();
    void undo();
};

struct ClipResizeCmd {
    uint32_t track_id;
    uint32_t clip_id;
    bool left_side;
    double relative_pos;
    double min_length;
    double last_beat_duration;

    void execute();
    void undo();
};

struct ClipDuplicateCmd {
    uint32_t track_id;
    uint32_t target_track_id;

    void execute();
    void undo();
};

struct ClipDeleteCmd {
    uint32_t track_id;
    uint32_t clip_id;
    Clip clip_state;
    double last_beat_duration;

    ClipDeleteCmd(uint32_t track_id, uint32_t clip_id, const Clip& clip,
                  double last_beat_duration) :
        track_id(track_id),
        clip_id(clip_id),
        clip_state(clip),
        last_beat_duration(last_beat_duration) {}

    ClipDeleteCmd(ClipDeleteCmd&& other) :
        track_id(std::exchange(other.track_id, {})),
        clip_id(std::exchange(other.clip_id, {})),
        clip_state(std::move(other.clip_state)) {}

    ClipDeleteCmd& operator=(ClipDeleteCmd&& other) {
        track_id = std::exchange(other.track_id, {});
        clip_id = std::exchange(other.clip_id, {});
        clip_state = std::move(other.clip_state);
        return *this;
    }

    void execute();
    void undo();
};

struct ClipDeleteRegionCmd {
    uint32_t first_track_id;
    uint32_t last_track_id;
    double min_time;
    double max_time;
    Vector<ClipHistory> clips;

    void execute();
    void undo();
};
} // namespace wb