#pragma once

//#include "core/debug.h"
#include "core/queue.h"
#include "core/vector.h"

namespace wb {

struct ParamValuePoint {
    double value;
    uint32_t sample_offset;
};

struct ParamChange {
    uint32_t id;
    uint32_t sample_offset;
    double value;
};

struct ParamValueQueue {
    uint32_t id = 0xFFFFFFFF;
    Vector<ParamValuePoint> points;

    inline void clear() { points.resize_fast(0); }

    inline uint32_t add_point(uint32_t sample_offset, double value) {
        size_t dest_idx = points.size();
        for (uint32_t i = 0; i < points.size(); i++) {
            auto& point = points[i];
            if (point.sample_offset == sample_offset) {
                point.value = value;
                return i;
            }
            if (point.sample_offset > sample_offset) {
                dest_idx = i;
                break;
            }
        }
        if (dest_idx == points.size()) {
            points.emplace_back(value, sample_offset);
        } else {
            points.emplace_at(dest_idx, value, sample_offset);
        }
        return dest_idx;
    }

    inline void push_point(uint32_t sample_offset, double value) { points.emplace_back(value, sample_offset); }
};

struct ParamChanges {
    Vector<uint32_t> param_ids;
    Vector<ParamValueQueue> queues;
    uint32_t changes_count = 0;

    inline void clear_changes() { changes_count = 0; }

    inline void set_max_params(uint32_t max_params) {
        queues.resize(max_params);

        while (param_ids.size() < max_params) {
            param_ids.emplace_back(0xFFFFFFFF);
        }

        if (param_ids.size() > max_params) {
            param_ids.resize(max_params);
        }

        if (changes_count > max_params) {
            changes_count = max_params;
        }
    }

    inline ParamValueQueue* add_param_change(uint32_t id, int32_t& index) {
        ParamValueQueue* ret = nullptr;

        if (param_ids[id] != 0xFFFFFFFF) {
            index = param_ids[id];
            ret = &queues[index];
        }

        if (queues.size() == changes_count) {
            ret = &queues.emplace_back();
        } else {
            ret = &queues[changes_count];
            ret->clear();
        }

        if (param_ids.size() <= id) {
            param_ids.resize(id + 1);
        }

        ret->id = id;
        param_ids[id] = changes_count;
        index = changes_count;
        changes_count++;
        return ret;
    }

    void transfer_changes_from(ConcurrentRingBuffer<ParamChange>& source) {
        ParamChange p;
        while (source.pop(p)) {
            int32_t index;
            ParamValueQueue* queue = add_param_change(p.id, index);
            queue->add_point(p.sample_offset, p.value);
        }
    }
};

} // namespace wb