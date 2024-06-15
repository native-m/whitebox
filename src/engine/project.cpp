#include "project.h"
#include "engine/track.h"
#include <algorithm>
#include <cstdio>

namespace wb {

ProjectFile::ProjectFile() {
}

bool ProjectFile::open(const std::string& filepath, bool file_write) {
    std::ios_base::openmode mode = file_write ? std::ios::binary | std::ios::out | std::ios::trunc
                          : std::ios::binary | std::ios::in;
    file.open(filepath, mode);
    file_location = filepath;
    return file.is_open();
}

bool ProjectFile::read_project(Engine& engine, SampleTable& sample_table) {
    if (std::filesystem::file_size(file_location) < sizeof(ProjectHeader))
        return false;
    if (!read_header())
        return false;
    read_project_info();

    size_t sample_count;
    file.read((char*)&sample_count, sizeof(size_t));
    sample_asset.reserve(sample_count);
    for (size_t i = 0; i < sample_count; i++) {
        std::string sample_file;
        read_text_(sample_file);
        auto sample = sample_table.load_sample_from_file(sample_file);
        sample->ref_count = 0; // Begin with 0 to prevent the memory leak
        sample_asset.push_back(sample);
    }

    size_t track_count;
    file.read((char*)&track_count, sizeof(size_t));
    engine.tracks.reserve(track_count);
    for (size_t i = 0; i < track_count; i++) {
        Track* track = read_track();
        engine.tracks.push_back(track);
    }

    return true;
}

bool ProjectFile::read_header() {
    ProjectHeader header;
    file.read((char*)&header, sizeof(ProjectHeader));
    if (header.magic_numbers != 'RPBW')
        return false;
    return true;
}

void ProjectFile::read_project_info() {
    // TODO: We skip these field for now.
    ProjectInfo project_info;
    read_string_(project_info.author);
    read_text_(project_info.title);
    read_text_(project_info.description);
    read_string_(project_info.genre);
}

Track* ProjectFile::read_track() {
    std::string name;
    uint32_t color;
    float height;
    float volume;
    float pan;
    bool mute;

    read_string_(name);
    file.read((char*)&color, sizeof(uint32_t));
    file.read((char*)&height, sizeof(float));
    file.read((char*)&volume, sizeof(float));
    file.read((char*)&pan, sizeof(float));
    file.read((char*)&mute, 1);

    Track* track = new Track(name, color, height, true,
                             {
                                 .volume = volume,
                                 .pan = pan,
                                 .mute = false,
                             });
    // track->ui_parameter.set(TrackParameter_Volume, volume);
    // track->ui_parameter.set(TrackParameter_Pan, pan);
    // track->ui_parameter.set(TrackParameter_Mute, (uint32_t)mute);

    size_t clip_count;
    file.read((char*)&clip_count, sizeof(size_t));
    track->clips.reserve(clip_count);
    for (size_t i = 0; i < clip_count; i++) {
        Clip* clip = (Clip*)track->clip_allocator.allocate();
        new (clip) Clip();
        read_clip(clip);
        track->clips.push_back(clip);
    }

    track->update(nullptr, 0.0);

    return track;
}

void ProjectFile::read_clip(Clip* clip) {
    uint32_t color;
    file.read((char*)&clip->type, sizeof(ClipType));
    read_string_(clip->name);
    file.read((char*)&color, sizeof(uint32_t));
    file.read((char*)&clip->min_time, sizeof(double));
    file.read((char*)&clip->max_time, sizeof(double));
    file.read((char*)&clip->relative_start_time, sizeof(double));
    clip->color = ImColor(color);

    switch (clip->type) {
        case ClipType::Audio: {
            uint32_t sample_idx;
            file.read((char*)&sample_idx, sizeof(uint32_t));
            file.read((char*)&clip->audio.start_sample_pos, sizeof(double));
            clip->audio.asset = sample_asset[sample_idx];
            clip->audio.asset->add_ref();
            break;
        }
        case ClipType::Unknown:
        case ClipType::Midi:
            break;
    }
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
    float volume = track->ui_parameter_state.volume;
    float pan = track->ui_parameter_state.pan;
    bool mute = (bool)track->ui_parameter_state.mute;
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

void ProjectFile::read_string_(std::string& str) {
    uint8_t len;
    file.read((char*)&len, 1);
    if (len) {
        str.resize(len);
        file.read(str.data(), len);
    }
}

void ProjectFile::read_text_(std::string& str) {
    uint16_t len;
    file.read((char*)&len, 2);
    if (len) {
        str.resize(len);
        file.read(str.data(), len);
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