#pragma once

namespace wb {
template <typename T>
struct InplaceList {
    InplaceList<T>* prev_ = nullptr;
    InplaceList<T>* next_ = nullptr;

    void replace_next_item(InplaceList<T>* item) noexcept {
        item->prev_ = this;
        next_ = item;
    }

    void push_item(InplaceList<T>* item) noexcept {
        item->next_ = next_;
        item->prev_ = this;
        if (next_)
            next_->prev_ = item;
        next_ = item;
    }

    InplaceList<T>* pop_next_item() noexcept {
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

    void remove_from_list() noexcept {
        prev_->next_ = next_;
        if (next_)
            next_->prev_ = prev_;
        prev_ = nullptr;
        next_ = nullptr;
    }

    void pluck_from_list() noexcept {
        prev_->next_ = nullptr;
        prev_ = nullptr;
    }

    T* next() { return static_cast<T*>(next_); }
    T* prev() { return static_cast<T*>(prev_); }
    const T* next() const { return static_cast<const T*>(next_); }
    const T* prev() const { return static_cast<const T*>(prev_); }
};
} // namespace wb