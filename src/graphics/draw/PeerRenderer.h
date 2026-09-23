#pragma once

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics::PeerRenderer
{
void drawPeerFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
}
