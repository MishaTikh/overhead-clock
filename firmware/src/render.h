// Text -> 32x8 column bitmaps (bit 0 = top row). Platform independent.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace ov {

// Proportional columns for the 5x7 font (rows 0-6).
std::vector<uint8_t> text_columns(const std::string& text);

// Clock face: big font, centered, e.g. "9:45" or "21:45".
void clock_frame(int hour24, int minute, bool colon_on, bool h24, uint8_t* out);

class Marquee {
public:
    // speed: pixels per second, 0 = the user's scroll-speed setting
    void set_text(const std::string& text, double now, float speed = 0);
    // seconds to show the start, then scroll the whole text past `cycles` times
    double seconds_for(int cycles) const;
    // Fills 32 column bytes for time `now`.
    void frame(double now, uint8_t* out) const;
    const std::string& text() const { return text_; }
private:
    std::string text_;
    std::vector<uint8_t> cols_;
    double t0_ = 0;
    float speed_ = 0;
};

}  // namespace ov
