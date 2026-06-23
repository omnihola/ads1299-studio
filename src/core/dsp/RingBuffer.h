#pragma once
#include <vector>
#include <mutex>
#include <cstdint>
namespace studio {
template<class T> class RingBuffer {
public:
    explicit RingBuffer(size_t cap) : buf_(cap + 1), cap_(cap + 1) {}
    bool push(const T& v) {
        std::lock_guard<std::mutex> lk(m_);
        size_t next = (head_ + 1) % cap_;
        if (next == tail_) { ++dropped_; return false; }
        buf_[head_] = v; head_ = next; return true;
    }
    bool pop(T& out) {
        std::lock_guard<std::mutex> lk(m_);
        if (tail_ == head_) return false;
        out = buf_[tail_]; tail_ = (tail_ + 1) % cap_; return true;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return (head_ + cap_ - tail_) % cap_;
    }
    size_t capacity() const { return cap_ - 1; }
    uint64_t dropped() const { std::lock_guard<std::mutex> lk(m_); return dropped_; }
private:
    std::vector<T> buf_;
    size_t cap_, head_ = 0, tail_ = 0;
    uint64_t dropped_ = 0;
    mutable std::mutex m_;
};
}
