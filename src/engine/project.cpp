#include "project.h"
#include <algorithm>
#include <cstdio>

namespace wb {

ProjectFile::ProjectFile() {
}

bool ProjectFile::open(const std::string& filepath, bool file_write) {
    int mode = file_write ? std::ios::binary | std::ios::out | std::ios::trunc
                          : std::ios::binary | std::ios::in;
    file.open(filepath, mode);
    return file.is_open();
}

void ProjectFile::write_project(Engine& engine, SampleTable& sample_table) {
    size_t sample_count = sample_table.samples.size();
    size_t track_count = engine.tracks.size();

    write_header();
    write_project_info({});

    uint32_t idx = 0;
    file.write((char*)&sample_count, sizeof(size_t));
    for (auto& sample : sample_table.samples) {
        write_text_(sample.second.sample_instance.path.string());
        sample_index_map.emplace(sample.second.hash, idx);
        idx++;
    }

    file.write((char*)&track_count, sizeof(size_t));
    for (auto track : engine.tracks) {
        write_track(track);
    }
}

void ProjectFile::write_header() {
    ProjectHeader project_header {
        .magic_numbers = 'RPBW',
        .version = 1,
    };
    file.write((char*)&project_header, sizeof(ProjectHeader));
}

void ProjectFile::write_project_info(const ProjectInfo& project_info) {
    write_string_(project_info.author);
    write_text_(project_info.title);
    write_text_(project_info.description);
    write_string_(project_info.genre);
}

void ProjectFile::write_track(Track* track) {
    uint32_t color = track->color;
    float volume = track->ui_parameter.get_float(TrackParameter_Volume);
    float pan = track->ui_parameter.get_float(TrackParameter_Pan);
    bool mute = (bool)track->ui_parameter.get_uint(TrackParameter_Mute);
    size_t clip_count = track->clips.size();
    write_string_(track->name);
    file.write((char*)&color, sizeof(uint32_t));
    file.write((char*)&track->height, sizeof(float));
    file.write((char*)&volume, sizeof(float));
    file.write((char*)&pan, sizeof(float));
    file.write((char*)&mute, 1);

    file.write((char*)&clip_count, sizeof(size_t));
    for (auto clip : track->clips) {
        write_clip(clip);
    }
}

void ProjectFile::write_clip(Clip* clip) {
    uint32_t color = clip->color;
    file.write((char*)&clip->type, sizeof(ClipType));
    write_string_(clip->name);
    file.write((char*)&color, sizeof(uint32_t));
    file.write((char*)&clip->min_time, sizeof(double));
    file.write((char*)&clip->max_time, sizeof(double));
    file.write((char*)&clip->relative_start_time, sizeof(double));

    switch (clip->type) {
        case ClipType::Audio: {
            uint32_t sample_idx = sample_index_map[clip->audio.asset->hash];
            file.write((char*)&sample_idx, sizeof(uint32_t));
            file.write((char*)&clip->audio.start_sample_pos, sizeof(double));
            break;
        }
        case ClipType::Unknown:
        case ClipType::Midi:
            break;
    }
}

void ProjectFile::write_string_(const std::string& str) {
    uint8_t len = (uint8_t)std::min(str.size(), size_t(256u));
    file.write((char*)&len, 1);
    if (len) {
        file.write(str.c_str(), len);
    }
}

void ProjectFile::write_text_(const std::string& str) {
    uint16_t len = (uint16_t)std::min(str.size(), size_t(UINT16_MAX));
    file.write((char*)&len, 2);
    if (len) {
        file.write(str.c_str(), len);
    }
}

} // namespace wb