#include "configuration.h"
#if defined(TTGO_T_ECHO_PLUS)
#include "MeshService.h"
#include "NodeDB.h"
#include "QuickHeart.h"
#include "Router.h"
#include "graphics/Screen.h"
#include "mesh/PeerStatus.h"
#if HAS_SCREEN
#include "MessageStore.h"
#endif

namespace QuickHeart
{
static void unavailable()
{
    LOG_WARN("Quick heart: paired contact or public key unavailable");
#if HAS_SCREEN
    if (screen) {
        graphics::BannerOverlayOptions options;
        options.message = "Heart: contact unavailable";
        options.durationMs = 2000;
        screen->showOverlayBanner(options);
    }
#endif
}

void send()
{
    if (!nodeDB || !router || !service) {
        unavailable();
        return;
    }
    const auto self = nodeDB->getNodeNum();
    const auto target = targetFor(self, PeerStatus::NODE_A, PeerStatus::NODE_B);
    const auto *peer = target ? nodeDB->getMeshNode(target) : nullptr;
    if (!peer || peer->is_ignored || !peer->has_user) {
        unavailable();
        return;
    }
    auto *packet = router->allocForSending();
    if (!packet)
        return;
    if (!prepare(*packet, self, target, peer->user.public_key.bytes, peer->user.public_key.size)) {
        packetPool.release(packet);
        unavailable();
        return;
    }
#if HAS_SCREEN
    messageStore.addFromPacket(*packet);
#endif
    service->sendToPhone(packetPool.allocCopy(*packet));
    LOG_INFO("Quick heart requested id=%08x", packet->id);
    service->sendToMesh(packet, RX_SRC_LOCAL, false);
}
} // namespace QuickHeart
#endif
