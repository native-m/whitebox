#pragma once

namespace wb {
template <typename T>
struct TrackedResource {
    TrackedResource<T>* prev_ = nullptr;
    TrackedResource<T>* next_ = nullptr;

    void push_tracked_resource(TrackedResource<T>* item) noexcept {
        item->next_ = next_;
        item->prev_ = this;
        if (next_)
            next_->prev_ = item;
        next_ = item;
    }

    TrackedResource<T>* pop_tracked_resource() noexcept {
        auto ret = next_;

        if (ret == nullptr)
            return nullptr;

        next_ = next_->next_;
        if (next_)
            next_->prev_ = ret->prev_;
        ret->prev_ = nullptr;
        ret->next_ = nullptr;

        return ret;
    }

    void remove_tracked_resource() noexcept {
        prev_->next_ = next_;
        if (next_)
            next_->prev_ = prev_;
        prev_ = nullptr;
        next_ = nullptr;
    }
};
} // namespace wb