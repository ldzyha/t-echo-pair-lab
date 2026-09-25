#pragma once
#include <cassert>
constexpr int EVENT_INPUT = 17;
struct TestPowerFSM {
    unsigned wakes = 0;
    void trigger(int event)
    {
        assert(event == EVENT_INPUT);
        ++wakes;
    }
};
extern TestPowerFSM powerFSM;
