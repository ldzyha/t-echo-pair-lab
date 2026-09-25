#pragma once

#include "configuration.h"

#if defined(TTGO_T_ECHO_PLUS)
#include "concurrency/OSThread.h"

class TouchBacklight : public concurrency::OSThread
{
  public:
    TouchBacklight();
    static constexpr uint32_t LIGHT_MS = 30000;

  protected:
    int32_t runOnce() override;

  private:
    static TouchBacklight *instance;
    static void onInterrupt();
    volatile uint32_t edgeCount = 0;
    uint32_t consumedEdges = 0;
    uint32_t lightStartedMs = 0;
    bool wasPressed = false;
    bool timedLight = false;
};
#endif
