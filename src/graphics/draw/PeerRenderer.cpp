#include "configuration.h"
#if HAS_SCREEN && defined(TTGO_T_ECHO_PLUS)
#include "GPS.h"
#include "NodeDB.h"
#include "PeerRenderer.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "mesh/DeliveryQueue.h"
#include "mesh/PeerStatus.h"

namespace graphics::PeerRenderer
{
static void formatAge(char *out, size_t length, uint32_t seconds)
{
    if (seconds == UINT32_MAX)
        snprintf(out, length, "?");
    else if (seconds < 60)
        snprintf(out, length, "%lus", (unsigned long)seconds);
    else if (seconds < 3600)
        snprintf(out, length, "%lum", (unsigned long)(seconds / 60));
    else if (seconds < 86400)
        snprintf(out, length, "%luh", (unsigned long)(seconds / 3600));
    else
        snprintf(out, length, "%lud", (unsigned long)(seconds / 86400));
}

static void drawLine(OLEDDisplay *display, int16_t center, int16_t y, const char *line, const uint8_t *font = FONT_SMALL)
{
    display->setFont(font);
    String fitted(line);
    const uint16_t width = display->getWidth() - 8;
    if (display->getStringWidth(fitted) > width)
        display->setFont(FONT_SMALL_LOCAL);
    while (fitted.length() && display->getStringWidth(fitted) > width)
        fitted.remove(fitted.length() - 1);
    display->drawString(center, y, fitted);
}

void drawPeerFrame(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    display->clear();
    drawCommonHeader(display, x, y, "Peer", true, true);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const int16_t center = x + display->getWidth() / 2;
    const auto *peer = PeerStatus::getPeer();
    display->setFont(FONT_MEDIUM);
    drawLine(display, center, y + 25, peer && peer->has_user ? peer->user.short_name : "No peer", FONT_MEDIUM);
    display->setFont(FONT_SMALL);
    if (!peer) {
        drawLine(display, center, y + 60, "Favorite peer not found");
        drawCommonFooter(display, x, y);
        return;
    }

    char age[16], line[48];
    const uint32_t heardAge = PeerStatus::rxAgeSeconds();
    formatAge(age, sizeof(age), heardAge);
    if (heardAge == UINT32_MAX)
        snprintf(line, sizeof(line), "RX: none since boot");
    else
        snprintf(line, sizeof(line), "%s: %s ago", heardAge < PeerStatus::SILENT_AFTER_SECONDS ? "RX" : "No RX", age);
    drawLine(display, center, y + 54, line);

    formatAge(age, sizeof(age), PeerStatus::txAgeSeconds());
    if (DeliveryQueue::pendingCount())
        snprintf(line, sizeof(line), "%s: %u", DeliveryQueue::statusLabel(), (unsigned)DeliveryQueue::pendingCount());
    else if (strcmp(DeliveryQueue::statusLabel(), "Delivered") == 0)
        snprintf(line, sizeof(line), "DM: radio ACK");
    else if (PeerStatus::deliveryState() == PeerStatus::DeliveryState::NONE)
        snprintf(line, sizeof(line), "%s", PeerStatus::deliveryLabel());
    else if (PeerStatus::deliveryError())
        snprintf(line, sizeof(line), "%s E%lu", PeerStatus::deliveryLabel(), (unsigned long)PeerStatus::deliveryError());
    else
        snprintf(line, sizeof(line), "%s %s", PeerStatus::deliveryLabel(), age);
    drawLine(display, center, y + 75, line);

    const uint32_t positionAge = peer->has_position ? PeerStatus::ageSeconds(peer->position.time) : UINT32_MAX;
    char positionRxAge[16];
    formatAge(age, sizeof(age), positionAge);
    formatAge(positionRxAge, sizeof(positionRxAge), PeerStatus::positionRxAgeSeconds());
    snprintf(line, sizeof(line), "Pos RX:%s Rpt:%s", positionRxAge, age);
    drawLine(display, center, y + 96, line);

    const auto *self = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const bool havePeerPosition = nodeDB->hasValidPosition(peer);
    const bool haveOwnFix =
        self && nodeDB->hasValidPosition(self) && gps && gps->hasLock() && nodeDB->hasLocalPositionSinceBoot();
    if (!havePeerPosition || !haveOwnFix) {
        drawLine(display, center, y + 115,
                 gps && gps->isTimeRejected() ? "GPS time invalid"
                 : !havePeerPosition          ? "Peer position unknown"
                                              : "Need own GPS fix");
        drawLine(display, center, y + 139, "ACK confirms radio only");
        drawCommonFooter(display, x, y);
        return;
    }

    const double ownLat = self->position.latitude_i * 1e-7;
    const double ownLon = self->position.longitude_i * 1e-7;
    const double peerLat = peer->position.latitude_i * 1e-7;
    const double peerLon = peer->position.longitude_i * 1e-7;
    const float distance = GeoCoord::latLongToMeter(ownLat, ownLon, peerLat, peerLon);
    int bearing = (int)lround(GeoCoord::toDegrees(GeoCoord::bearing(ownLat, ownLon, peerLat, peerLon)));
    bearing = (bearing + 360) % 360;
    if (distance < 1000)
        snprintf(line, sizeof(line), "Last pos: %.0f m", distance);
    else
        snprintf(line, sizeof(line), "Last pos: %.1f km", distance / 1000.0f);
    display->setFont(FONT_MEDIUM);
    drawLine(display, center, y + 117, line, FONT_MEDIUM);
    display->setFont(FONT_SMALL);
    if (distance < 1)
        snprintf(line, sizeof(line), "Bearing unavailable");
    else
        snprintf(line, sizeof(line), "%d deg %s from north", bearing, GeoCoord::degreesToBearing(bearing));
    drawLine(display, center, y + 147, line);
    drawLine(display, center, y + 171, positionAge < 5 * 60 ? "GPS approximate; ACK != read" : "Old report; ACK != read",
             FONT_SMALL_LOCAL);
    drawCommonFooter(display, x, y);
}
} // namespace graphics::PeerRenderer
#endif
