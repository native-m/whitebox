#pragma once

#include "../types.h"
#include "sample_table.h"
#include <imgui.h>
#include <string>

namespace wb
{
    struct ClipNode
    {
        ClipNode* prev{};
        ClipNode* next{};
        
        virtual ~ClipNode() {}

        inline void push_front(ClipNode* other_node)
        {
            ClipNode* prev_node = prev;
            if (prev_node) prev_node->next = other_node;
            other_node->prev = prev_node;
            other_node->next = this;
            prev = other_node;
        }
        
        inline void push_back(ClipNode* other_node)
        {
            ClipNode* next_node = next;
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
    };

    struct Clip : public ClipNode
    {
        std::string name;
        ImColor color;
        double min_time = 0.0;
        double max_time = 0.0;
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
    };

    struct MIDIClip : public Clip
    {
        ~MIDIClip() {}
    };
}
