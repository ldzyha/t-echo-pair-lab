# Environment regression checks

Run from the firmware repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Isrc/mesh \
  -Itest/custom_audit/test_environment_stubs \
  test/custom_audit/test_environment.cpp src/mesh/Throttle.cpp \
  -o /tmp/meshtastic-test-environment
/tmp/meshtastic-test-environment
```

The tests compile the production `LocalEnvironmentCache`, `BME280Reading` and
`Throttle.cpp`. Only the hardware sensor interface, time source and protobuf-shaped
data containers are replaced by test fixtures. This runs without radios, Bluetooth,
phone queues or transmitting any packet.

Covered: 15-second local sampling; unchanged measurement timestamp on reuse;
45-second stale limit; immediate invalidation after a failed read; bounded retry;
millis rollover and changed wall-clock time; atomic BME field updates; failed
conversion and invalid float/range rejection; valid boundary values; zero default
temperature correction and optional correction arithmetic.

These are logic tests, not a full EnvironmentTelemetry scheduler or physical I2C
test. The firmware build and both radios still require separate verification.

At runtime each real attempt logs `Local environment read: ms=... time=...` plus
the measured values, or a failure with `current values invalidated`. Local sampling
starts independently of the mesh startup delay. RF replies use the one current
valid sample, so receiving a request does not run a blocking sensor conversion.
No sensor history is written to flash. The transmitted protobuf `time` remains
the measurement time, even if a phone or mesh packet is emitted later.

## BME initialization sequencing

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  test/custom_audit/test_environment_setup.cpp -o /tmp/meshtastic-test-environment-setup
/tmp/meshtastic-test-environment-setup
```

This additional test compiles the production `BME280Setup.h` against a register
fixture which ignores configuration writes during an active mode transition. It
covers the former immediate-write race, sleep-before-configuration, checked X1
readback, bounded busy polling, read/write failures, and mismatched configuration.
Runtime evidence is `BME280 verified forced X1: id=60 hum=01 ctrl=24 cfg=a0` plus
the per-measurement `BME280 read completed` duration. The vendor library's earlier
`begin()` initialization is outside this helper's bounded polling contract.
