#pragma once
struct TestNodeInfoModule {
    unsigned requests = 0;
    void triggerImmediateNodeInfoCheck() { ++requests; }
};
extern TestNodeInfoModule *nodeInfoModule;
