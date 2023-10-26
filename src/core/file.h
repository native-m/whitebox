#pragma once

#include "../stdpch.h"

namespace wb
{
    static std::optional<std::vector<std::byte>> load_binary_file(const std::string& path)
    {
        std::vector<std::byte> contents;
        try {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open())
                return {};
            auto size = ifs.seekg(0, std::ios::end).tellg();
            contents.resize(size);
            ifs.seekg(0, std::ios::beg);
            ifs.read((char*)contents.data(), size);
            ifs.close();
        }
        catch (...) {
            return {};
        }
        return contents;
    }
}