#pragma once
#include <cstring>
#include <string>
#include <vector>
class OLEDDisplay
{
  public:
    struct Glyph {
        int x, width, height;
    };
    std::vector<Glyph> glyphs;
    int getStringWidth(const char *text, size_t len, bool = false) const
    {
        int width = 0;
        for (size_t i = 0; i < len; ++i)
            width += text[i] == ' ' ? 3 : 6;
        return width;
    }
    int getStringWidth(const char *text) const { return getStringWidth(text, std::strlen(text)); }
    void drawString(int, int, const char *text)
    {
        // Unsupported Unicode whitespace must never reach the font renderer.
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
            if (*p >= 0x80)
                unexpectedUnicode = true;
    }
    void drawXbm(int x, int, int width, int height, const unsigned char *) { glyphs.push_back({x, width, height}); }
    bool unexpectedUnicode = false;
};
