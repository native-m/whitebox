#pragma once

#include "core/common.h"
#include "engine.h"
#include "sample_table.h"
#include <fstream>

namespace wb {

struct ProjectHeader {
    uint32_t magic_numbers;
    uint32_t version;
};

struct ProjectInfo {
    std::string author;
    std::string title;
    std::string description;
    std::string genre;
};

struct ProjectFile {
    std::unordered_map<uint64_t, uint32_t> sample_index_map;
    std::fstream file;

    ProjectFile();
    bool open(const std::string& file, bool file_write);
    void write_project(Engine& engine, SampleTable& sample_table);

    void write_header();
    void write_project_info(const ProjectInfo& project_info);
    void write_track(Track* track);
    void write_clip(Clip* clip);
    void write_string_(const std::string& str);
    void write_text_(const std::string& str);
};

} // namespace wb