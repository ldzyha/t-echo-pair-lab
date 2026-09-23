# Delivery queue host checks

Run from the firmware repository root. These tests do not use USB, radio hardware, or real flash.

## Pure codec and event gate

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc test/custom_audit/test_delivery_queue.cpp -o /tmp/test-delivery-queue
/tmp/test-delivery-queue
```

Checks production frame parsing, receipt correlation, relevant RX filtering, duplicate-event coalescing, no retry trigger from elapsed time, CRC including snapshot generation, truncated/corrupt alternate snapshots.

## Real encoded payload limit

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Isrc -Isrc/mesh/generated -I.pio/libdeps/t-echo-plus/Nanopb test/custom_audit/test_delivery_payload.cpp src/mesh/generated/meshtastic/mesh.pb.cpp .pio/libdeps/t-echo-plus/Nanopb/pb_encode.c .pio/libdeps/t-echo-plus/Nanopb/pb_common.c -Wl,--gc-sections -o /tmp/test-delivery-payload
/tmp/test-delivery-payload
```

Uses actual generated protobuf and Nanopb. Current maximum is192 UTF-8 bytes for simple text and187 with maximal reply_id; additional metadata can lower it. No truncation.

## Actual module state machine

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Itest/custom_audit/test_delivery_stubs -Isrc -Isrc/mesh/generated -I.pio/libdeps/t-echo-plus/Nanopb test/custom_audit/test_delivery_module.cpp src/mesh/generated/meshtastic/mesh.pb.cpp .pio/libdeps/t-echo-plus/Nanopb/pb_encode.c .pio/libdeps/t-echo-plus/Nanopb/pb_decode.c .pio/libdeps/t-echo-plus/Nanopb/pb_common.c -Wl,--gc-sections -o /tmp/test-delivery-module
/tmp/test-delivery-module
```

Directly includes production DeliveryQueueModule.cpp, without a duplicate implementation of queue logic. Only platform dependencies are replaced: in-memory filesystem, radio/client sinks, node keys, scheduler, locks. Checks:

- Readback-validated persistence precedes queue acceptance and first transmission.
- Only FIFO head sends; polling scheduler alone never retries.
- Relevant target RX triggers another attempt with a new physical ID.
- Invalid logical receipt cannot resolve pending delivery; matching receipt advances FIFO.
- Durable receipt is emitted after persistent incoming payload/watermark commit.
- Logical duplicate produces another receipt without another flash write or display.
- Sender restart restores pending state but waits for fresh peer RX.
- Receiver restart replays stable original IDs as INTERNAL, without radio sends.
- Inbox deletion survives restart while retaining duplicate suppression.
- Failed outgoing persistence produces explicit error and no transmission; failed incoming persistence emits no receipt or delivery.
- RX callback never writes flash; client/radio callbacks run outside SPI lock.

This harness does not model RF propagation, LoRa queues, real filesystem power-loss semantics, Android/iOS database consumption, FreeRTOS scheduling, or SPI driver behavior. Those remain hardware/integration checks.
