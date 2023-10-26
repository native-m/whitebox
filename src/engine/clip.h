#pragma once

#include "../types.h"
#include "../stdpch.h"
#include "sample_table.h"
#include <imgui.h>

namespace wb
{
    struct Clip
    {
        std::string name;
        ImColor color;
        double min_time = 0.0;
        double max_time = 0.0;
        Clip* prev{};
        Clip* next{};

        inline void push_front(Clip* other_node)
        {
            Clip* prev_node = prev;
            if (prev_node) prev_node->next = other_node;
            other_node->prev = prev_node;
            other_node->next = this;
            prev = other_node;
        }

        inline void push_back(Clip* other_node)
        {
            Clip* next_node = next;
            if (next_node) next_node->prev = other_node;
            other_node->next = next_node;
            other_node->prev = this;
            next = other_node;
        }

        inline void detach()
        {
            prev->next = next;
            next->prev = prev;
            prev = {};
            next = {};
        }

        virtual ~Clip() { }
    };

    struct AudioClip : public Clip
    {
        SampleAsset asset;

        AudioClip(double min_time, double max_time)
        {
            this->min_time = min_time;
            this->max_time = max_time;
        }

        ~AudioClip() {}

        Sample* get_sample_instance() { return &asset.ref->sample_instance; }
    };

    struct MIDIClip : public Clip
    {
        ~MIDIClip() {}
    };
}
