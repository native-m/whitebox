#pragma once

#include "stdpch.h"
#include "core/math.h"

namespace wb
{
    struct FileSize
    {
        double value;
        const char* unit = "B";

        FileSize() : value(0) { }

        FileSize(uint64_t size)
        {
            if (size >= 0 && size < 1000000) {
                value = (double)size / 1000.0;
                unit = "KB";
            }
            else if (size >= 1000000 && size < 1000000000) {
                value = (double)size / 1000000.0;
                unit = "MB";
            }
            else if (size >= 1000000000 && size < 1000000000000) {
                value = (double)size / 1000000000.0;
                unit = "GB";
            }
            else {
                value = (double)size / 1000000000000.0;
                unit = "TB";
            }
        }
    };

    struct ContentBrowserItem
    {
        enum Type
        {
            Directory,
            File,
        };

        Type type;
        ContentBrowserItem* parent;
        std::u8string name;
        FileSize size;
        std::optional<std::vector<ContentBrowserItem>> dir_items;
        std::optional<std::vector<ContentBrowserItem>> file_items;
        bool root_dir;
        bool open;

        std::filesystem::path get_file_path(const std::filesystem::path& root) const
        {
            std::filesystem::path ret;
            const ContentBrowserItem* item = this;
            while (item != nullptr) {
                ret = (item != this) ?
                    std::filesystem::path(item->name) / ret :
                    std::filesystem::path(item->name);
                item = item->parent;
            }
            return root.parent_path() / ret;
        }
    };

    struct ContentBrowserDir
    {
        std::filesystem::path path;
        ContentBrowserItem item;
    };

    struct ContentBrowserFilePayload
    {
        const std::filesystem::path* root_dir;
        const ContentBrowserItem* item;
    };

    struct GUIContentBrowser
    {
        using DirectorySet = std::unordered_set<std::filesystem::path>;
        using DirectoryRefItem = std::pair<DirectorySet::iterator, ContentBrowserItem>;
        bool docked = false;
        DirectorySet directory_set;
        std::vector<DirectoryRefItem> directories;

        GUIContentBrowser();
        void add_directory(const std::filesystem::path& path);
        void sort_directory();
        void glob_path(const std::filesystem::path& path, ContentBrowserItem& item);
        void render_item(const std::filesystem::path& root_dir, ContentBrowserItem& item);
        void render();
    };

    extern GUIContentBrowser g_gui_content_browser;
}