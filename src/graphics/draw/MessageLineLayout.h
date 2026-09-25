#pragma once
#include "graphics/EmoteRenderer.h"
#include <algorithm>

namespace graphics
{
namespace MessageLineLayout
{
// Keep only the current/next measurements; a full metrics vector can exhaust the nRF52 heap.
inline std::vector<int> calculate(const std::vector<std::string> &lines, const std::vector<bool> &isHeaderVec, int fontHeight,
                                  int blockGap, const Emote *emotes = graphics::emotes, int emoteCount = graphics::numEmotes)
{
    // Tunables for layout control
    constexpr int HEADER_UNDERLINE_GAP = 0; // space between underline and first body line
    constexpr int HEADER_UNDERLINE_PIX = 1; // underline thickness (1px row drawn)
    constexpr int BODY_LINE_LEADING = -4;   // default vertical leading for normal body lines
    constexpr int EMOTE_PADDING_ABOVE = 4;  // space above emote line (added to line above)
    constexpr int EMOTE_PADDING_BELOW = 3;  // space below emote line (added to emote line)

    std::vector<int> rowHeights;
    rowHeights.reserve(lines.size());
    auto current = lines.empty() ? EmoteRenderer::LineMetrics{}
                                 : EmoteRenderer::analyzeLine(nullptr, lines.front(), fontHeight, emotes, emoteCount);

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        const int baseHeight = fontHeight;
        int lineHeight = baseHeight;

        const auto next = idx + 1 < lines.size()
                              ? EmoteRenderer::analyzeLine(nullptr, lines[idx + 1], fontHeight, emotes, emoteCount)
                              : EmoteRenderer::LineMetrics{};
        const int tallestEmote = current.tallestHeight;
        const bool hasEmote = current.hasEmote;
        const bool nextHasEmote = next.hasEmote;

        if (isHeaderVec[idx]) {
            // Header line spacing
            lineHeight = baseHeight + HEADER_UNDERLINE_PIX + HEADER_UNDERLINE_GAP;
        } else {
            // Base spacing for normal lines
            int desiredBody = baseHeight + BODY_LINE_LEADING;

            if (hasEmote) {
                // Emote line: add overshoot + bottom padding
                int overshoot = std::max(0, tallestEmote - baseHeight);
                lineHeight = desiredBody + overshoot + EMOTE_PADDING_BELOW;
            } else {
                // Regular line: no emote → standard spacing
                lineHeight = desiredBody;

                // If next line has an emote → add top padding *here*
                if (nextHasEmote) {
                    lineHeight += EMOTE_PADDING_ABOVE;
                }
            }

            // Add block gap if next is a header
            if (idx + 1 < lines.size() && isHeaderVec[idx + 1]) {
                lineHeight += blockGap;
            }
        }

        rowHeights.push_back(lineHeight);
        current = next;
    }

    return rowHeights;
}
} // namespace MessageLineLayout
} // namespace graphics
