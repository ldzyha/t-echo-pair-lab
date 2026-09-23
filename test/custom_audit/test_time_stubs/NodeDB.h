#pragma once
struct TestConfig {
    struct Device {
        char tzdef[64] = {};
    } device;
};
extern TestConfig config;
