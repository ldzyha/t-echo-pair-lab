# Time regression checks

Run from the firmware root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc test/custom_audit/test_time.cpp -o ../test-time-r2
../test-time-r2
g++ -std=c++17 -Wall -Wextra -Werror -DPIO_UNIT_TESTING -Itest/custom_audit/test_time_stubs -Isrc -Isrc/mesh test/custom_audit/test_rtc_policy.cpp src/gps/RTC.cpp src/mesh/Throttle.cpp -o ../test-rtc-policy-r2
../test-rtc-policy-r2
g++ -std=c++17 -Wall -Wextra -Werror -DPIO_UNIT_TESTING -DTEST_HARDWARE_RTC -Itest/custom_audit/test_time_stubs -Isrc -Isrc/mesh -I.pio/libdeps/t-echo-plus/ErriezCRC32/src test/custom_audit/test_rtc_restart.cpp src/gps/RTC.cpp src/mesh/Throttle.cpp .pio/libdeps/t-echo-plus/ErriezCRC32/src/ErriezCRC32.c -o ../test-rtc-restart-r2
../test-rtc-restart-r2
```

The first executable tests production time helpers: transport/source qualification, anchor tolerance, elapsed time across counter wrap, calendar conversion against libc, DST edges, timezone offsets, 12/24-hour formatting and unknown time.

The second compiles actual `RTC.cpp` and `Throttle.cpp`, replacing only hardware/config/log dependencies. It reproduces the old GPS-over-NTP priority, proves explicit local correction now succeeds, rejects subsequent GPS dates outside the anchor, permits plausible GPS time, verifies invalid local dates do not replace the anchor, tests runtime timezone application and resets quality/anchor between sessions. It does not set the host clock.

The third compiles the real RTC code with a fake advancing PCF8563 and in-memory filesystem. It covers commissioning, restart from the advancing clock, GPS rejection after restart, CRC corruption, partial alternate-slot writes, low-voltage/stopped-clock/I2C failures and repair by a local client. Ordinary resync does not rewrite flash markers.

These host checks do not test the nRF RTC hardware, nRF newlib timezone implementation, BLE authorization, Admin packet routing or pixels on e-ink. Firmware compilation and live device verification remain required. After a verified local clock write, a CRC-protected marker qualifies the advancing hardware RTC to restore the RAM anchor on restart. The stored marker epoch is only a lower bound; it is never returned as current time. A commissioned RTC with failed integrity/readback requires local time again. Before initial commissioning, ordinary GPS time validation applies.
