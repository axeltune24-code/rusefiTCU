#pragma once
#include <cmath>
#include <cstddef>

// V25: symmetric Blackman-Harris window without a permanent 4 KiB table.
class KnockWindow {
public:
    explicit KnockWindow(size_t count)
        : step(cosf(6.28318530718f / (count > 1 ? count - 1 : 1))),
          previous(step) {}
    float next() {
        const float cos2 = 2 * cosine * cosine - 1;
        const float cos3 = cosine * (4 * cosine * cosine - 3);
        const float value = 0.35875f - 0.48829f * cosine + 0.14128f * cos2 - 0.01168f * cos3;
        const float following = 2 * step * cosine - previous;
        previous = cosine;
        cosine = following;
        return value;
    }
private:
    float step;
    float previous;
    float cosine = 1;
};
