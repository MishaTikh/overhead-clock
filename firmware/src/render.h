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
    // still: show centered without scrolling (for text that fits the screen)
    void set_text(const std::string& text, double now, float speed = 0, bool still = false);
    // seconds to show the start, then scroll the whole text past `cycles` times
    double seconds_for(int cycles) const;
    // Fills 32 column bytes for time `now`.
    void frame(double now, uint8_t* out) const;
    const std::string& text() const { return text_; }
    void restart(double t0) { t0_ = t0; }   // start the hold-then-scroll again from time t0
private:
    std::string text_;
    std::vector<uint8_t> cols_;
    double t0_ = 0;
    float speed_ = 0;
    bool still_ = false;
};

// A short show on the sign: scrolling steps and still pages, played in order, that the
// buttons can also step through. Used to show the settings address and password.
struct SignStep {
    std::string text;
    double secs;            // how long it plays on its own
    bool still;             // centered, not scrolling
    float speed;            // scroll speed, 0 = the setting
    int page, pages;        // progress marks on the bottom row (page of pages), 0 = none
    SignStep(const std::string& t, double s, bool st = false, float sp = 0, int pg = 0, int pgs = 0)
        : text(t), secs(s), still(st), speed(sp), page(pg), pages(pgs) {}
};

class SignSequence {
public:
    // repeat_from: after the last step, jump back to this step `repeats` more times
    void start(const std::vector<SignStep>& steps, double now, int repeat_from = -1, int repeats = 0);
    void next(double now);   // button: step forward (stops the automatic advance)
    void prev(double now);   // button: step back
    void stop() { steps_.clear(); }
    bool active(double now);
    // Fills 32 column bytes; false once finished.
    bool frame(double now, uint8_t* out);
    double total_seconds() const;
private:
    void enter(size_t i, double now);
    std::vector<SignStep> steps_;
    size_t idx_ = 0;
    double step_start_ = 0, last_input_ = 0;
    bool manual_ = false;
    int repeat_from_ = -1, repeats_left_ = 0;
    Marquee m_;
};

}  // namespace ov
