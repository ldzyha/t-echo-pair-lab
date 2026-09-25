#include "FakeMessageStore.h"
#include "MessageStore.h"
#include <cstdio>
uint32_t fakeMillis = 100;
concurrency::Lock fakeLock;
concurrency::Lock *spiLock = &fakeLock;
FakeFS fakeFS;
NodeDB fakeNodeDB;
NodeDB *nodeDB = &fakeNodeDB;
std::vector<uint32_t> forgotten;
namespace DeliveryQueue
{
void forgetInbox(uint32_t id)
{
    forgotten.push_back(id);
}
} // namespace DeliveryQueue
static meshtastic_MeshPacket packet(uint32_t id, const char *text)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
    p.from = 0x11223344;
    p.to = nodeDB->getNodeNum();
    p.id = id;
    p.decoded.payload.size = strlen(text);
    memcpy(p.decoded.payload.bytes, text, strlen(text));
    return p;
}
static void expect(uint32_t id, const char *text)
{
    assert(messageStore.getMessages().size() == 1);
    const auto &last = messageStore.getMessages().back();
    assert(last.packetId == id);
    assert(!strcmp(MessageStore::getText(last), text));
}
int main()
{
    static_assert(MAX_MESSAGES_SAVED == 1);
    messageStore.clearAllMessages();
    const std::string path = "/Messages_default.msgs2";
    std::vector<uint8_t> legacy{20};
    for (uint32_t id = 1; id <= 20; ++id) {
        const auto text = std::string("Ї Є Ґ ") + std::to_string(id);
        messageStore.addFromPacket(packet(id, text.c_str()));
        messageStore.saveToFlash();
        const auto &one = fakeFS.files[path];
        legacy.insert(legacy.end(), one.begin() + 1, one.end());
    }
    const size_t recordSize = fakeFS.files[path].size() - 1;
    fakeFS.files[path] = legacy;
    messageStore.loadFromFlash();
    expect(20, "Ї Є Ґ 20");
    assert(fakeFS.files[path].size() == 1 + recordSize && fakeFS.files[path][0] == 1);
    messageStore.loadFromFlash();
    expect(20, "Ї Є Ґ 20");
    assert(forgotten.size() == 1); // Eviction does not cancel delivery or forget its newest inbox.
    messageStore.addFromPacket(packet(21, "нове ❤️"));
    messageStore.addFromPacket(packet(21, "duplicate cannot overwrite"));
    expect(21, "нове ❤️");
    assert(messageStore.unreadCount() == 1);
    messageStore.markMessagesRead();
    assert(messageStore.unreadCount() == 0);
    messageStore.saveToFlash();
    messageStore.loadFromFlash();
    expect(21, "нове ❤️");

    // A partial final record must not overwrite the last complete text slot.
    legacy.resize(legacy.size() - 2);
    fakeFS.files[path] = legacy;
    messageStore.loadFromFlash();
    expect(19, "Ї Є Ґ 19");

    // Old .msgs records lack packet IDs; retain their newest text too.
    std::vector<uint8_t> v1{19};
    for (size_t i = 0; i < 19; ++i)
        v1.insert(v1.end(), legacy.begin() + 1 + i * recordSize, legacy.begin() + 1 + (i + 1) * recordSize - 4);
    fakeFS.files.erase(path);
    fakeFS.files["/Messages_default.msgs"] = v1;
    messageStore.loadFromFlash();
    expect(0, "Ї Є Ґ 19");
    assert(fakeFS.files[path][0] == 1);
    puts("actual MessageStore: newest-only migration, UTF-8 replacement, partial records, read state: OK");
}
