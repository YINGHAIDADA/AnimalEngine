#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace animal {

// Generational handle: a stale handle (resource destroyed, slot reused) never resolves.
template <typename Tag>
struct Handle {
    uint32_t index = 0;
    uint32_t generation = 0; // 0 means invalid

    constexpr bool valid() const { return generation != 0; }
    constexpr explicit operator bool() const { return valid(); }
    constexpr uint64_t key() const { return (uint64_t{generation} << 32) | index; }
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

// Pointers returned by get() are invalidated by insert().
template <typename Tag, typename T>
class HandlePool {
public:
    using HandleType = Handle<Tag>;

    HandleType insert(T value) {
        uint32_t index = 0;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            index = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++alive_;
        return {index, slot.generation};
    }

    T* get(HandleType handle) {
        if (!handle.valid() || handle.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[handle.index];
        if (slot.generation != handle.generation || !slot.value) {
            return nullptr;
        }
        return &*slot.value;
    }

    const T* get(HandleType handle) const { return const_cast<HandlePool*>(this)->get(handle); }

    std::optional<T> take(HandleType handle) {
        if (!get(handle)) {
            return std::nullopt;
        }
        Slot& slot = slots_[handle.index];
        std::optional<T> out(std::move(*slot.value));
        slot.value.reset();
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        freeList_.push_back(handle.index);
        --alive_;
        return out;
    }

    template <typename F>
    void forEach(F&& fn) {
        for (Slot& slot : slots_) {
            if (slot.value) {
                fn(*slot.value);
            }
        }
    }

    size_t size() const { return alive_; }

private:
    struct Slot {
        std::optional<T> value;
        uint32_t generation = 1;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
    size_t alive_ = 0;
};

} // namespace animal
