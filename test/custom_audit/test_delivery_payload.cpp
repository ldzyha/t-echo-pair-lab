#include "mesh/DeliveryQueueCodec.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <pb_encode.h>
using namespace DeliveryQueueCodec;
size_t encoded(const meshtastic_Data &data, uint8_t *out, size_t cap)
{
    auto stream = pb_ostream_from_buffer(out, cap);
    return pb_encode(&stream, meshtastic_Data_fields, &data) ? stream.bytes_written : 0;
}
bool fits(const meshtastic_Data &original)
{
    uint8_t source[meshtastic_Data_size], bytes[meshtastic_Data_size];
    size_t n = encoded(original, source, sizeof(source));
    meshtastic_Data wrapper = meshtastic_Data_init_default;
    wrapper.portnum = meshtastic_PortNum_PRIVATE_APP;
    wrapper.has_bitfield = true;
    wrapper.payload.size = encode(wrapper.payload.bytes, sizeof(wrapper.payload.bytes), {DATA, 1, 1, 1, source, n});
    if (!wrapper.payload.size)
        return false;
    n = encoded(wrapper, bytes, sizeof(bytes));
    return n && n + 16 + 12 <= 255;
}
int main()
{
    meshtastic_Data original = meshtastic_Data_init_default;
    original.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t plainMax = 0;
    for (size_t n = 1; n <= sizeof(original.payload.bytes); ++n) {
        original.payload.size = n;
        memset(original.payload.bytes, 'x', n);
        if (fits(original))
            plainMax = n;
    }
    original.payload.size = plainMax;
    assert(fits(original));
    original.payload.size = plainMax + 1;
    assert(!fits(original));
    original.payload.size = plainMax;
    original.reply_id = UINT32_MAX;
    assert(!fits(original));
    size_t replyMax = 0;
    for (size_t n = 1; n <= plainMax; ++n) {
        original.payload.size = n;
        if (fits(original))
            replyMax = n;
    }
    assert(replyMax < plainMax);
    printf("Exact protobuf+PKI+radio limit: plain UTF-8 bytes=%zu, "
           "reply-to-max-ID bytes=%zu\n",
           plainMax, replyMax);
}
