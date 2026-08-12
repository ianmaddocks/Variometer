#pragma once

#include <stddef.h>

namespace variometer {

template <typename T, size_t N>
class RingBuffer {
public:
    RingBuffer() = default;

    void clear() {
        size_ = 0;
        head_ = 0;
        tail_ = 0;
    }

    void push(const T& value) {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (size_ < N) {
            ++size_;
        } else {
            tail_ = (tail_ + 1) % N;
        }
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    T& at(size_t index) {
        return data_[(tail_ + index) % N];
    }

    const T& at(size_t index) const {
        return data_[(tail_ + index) % N];
    }

private:
    T data_[N]{};
    size_t size_ = 0;
    size_t head_ = 0;
    size_t tail_ = 0;
};

}  // namespace variometer
