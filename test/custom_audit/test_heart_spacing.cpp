#include "graphics/EmoteRenderer.h"
#include "input/QuickHeart.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>

int main()
{
    using namespace graphics::EmoteRenderer;
    const auto *space = findEmoteByLabel(u8"\u2003");
    assert(space && space->height == 1 && space->width == 13);
    for (int i = 0; i < 2; ++i)
        assert(space->bitmap[i] == 0);
    // Wrapping measures growing prefixes; an unfinished UTF-8 character must stay
    // in bounds.
    for (unsigned clicks = 2; clicks <= 5; ++clicks) {
        const char *text = QuickHeart::textFor(clicks);
        for (size_t length = 1; length <= strlen(text); ++length) {
            auto prefix = std::make_unique<char[]>(length + 1);
            memcpy(prefix.get(), text, length);
            prefix[length] = 0;
            OLEDDisplay display;
            size_t complete = 0;
            while (complete < length) {
                const size_t bytes = utf8CharLen(static_cast<uint8_t>(text[complete]));
                if (bytes > length - complete)
                    break;
                complete += bytes;
            }
            const std::string fullCharacters(text, complete);
            assert(analyzeLine(&display, prefix.get(), 12).width == analyzeLine(&display, fullCharacters, 12).width);
            drawStringWithEmotes(&display, 0, 0, prefix.get(), 12);
            char truncated[240];
            truncateToWidth(&display, prefix.get(), truncated, sizeof(truncated), 80, "...");
        }
    }
    int previousWidth = 0;
    for (unsigned clicks = 3; clicks <= 5; ++clicks) {
        std::istringstream body(QuickHeart::textFor(clicks));
        std::vector<float> centers;
        int maximum = 0, count = 0;
        for (std::string row; std::getline(body, row);) {
            OLEDDisplay display;
            const auto metrics = analyzeLine(&display, row, 12);
            drawStringWithEmotes(&display, 0, 0, row, 12);
            assert(!display.unexpectedUnicode && metrics.tallestHeight == 16);
            int first = -1, last = -1, lastRight = 0;
            for (const auto &g : display.glyphs) {
                lastRight = g.x + g.width + 1;
                if (g.height == 16) {
                    if (first < 0)
                        first = g.x;
                    last = g.x;
                }
            }
            assert(first >= 0 && metrics.width == lastRight);
            centers.push_back((first + last + 16) / 2.f);
            maximum = std::max(maximum, metrics.width);
            ++count;
        }
        assert(count == int(2 * clicks - 3));
        assert(maximum > previousWidth && maximum <= 160);
        previousWidth = maximum;
        for (float center : centers)
            assert(std::abs(center - maximum / 2.f) <= 4.f);
    }
    std::puts("Actual EmoteRenderer: wide blank glyph, aligned contour rows, "
              "matching measure/draw and e-ink width passed");
}
