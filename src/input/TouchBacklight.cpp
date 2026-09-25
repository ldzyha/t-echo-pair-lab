#include "TouchBacklight.h"

#if defined(TTGO_T_ECHO_PLUS)
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "main.h"

TouchBacklight *TouchBacklight::instance = nullptr;

TouchBacklight::TouchBacklight() : concurrency::OSThread("TouchLight")
{
    instance = this;
    pinMode(BUTTON_PIN_TOUCH, INPUT_PULLDOWN_SENSE);
    wasPressed = digitalRead(BUTTON_PIN_TOUCH) == HIGH;
    int irq = attachInterrupt(BUTTON_PIN_TOUCH, onInterrupt, RISING);
    LOG_INFO("TouchLight T1: pin=%u idle=%u irq=%d", BUTTON_PIN_TOUCH, wasPressed, irq);
}

void TouchBacklight::onInterrupt()
{
    // Retain short sensor pulses even if e-ink work delays the main loop.
    instance->edgeCount++;
    instance->setIntervalFromNow(0);
    runASAP = true;
    BaseType_t higherWake = 0;
    concurrency::mainDelay.interruptFromISR(&higherWake);
}

int32_t TouchBacklight::runOnce()
{
    const uint32_t edges = edgeCount;
    const bool pressed = digitalRead(BUTTON_PIN_TOUCH) == HIGH;
    const bool touched = edges != consumedEdges || (pressed && !wasPressed);
    consumedEdges = edges;
    wasPressed = pressed;

    if (touched) {
        powerFSM.trigger(EVENT_INPUT);
        digitalWrite(PIN_EINK_EN, HIGH);
        lightStartedMs = millis();
        timedLight = true;
        LOG_INFO("TouchLight T1: touch edges=%u input=%u light=%u", edges, pressed, digitalRead(PIN_EINK_EN));
    }

    if (timedLight && !Throttle::isWithinTimespanMs(lightStartedMs, LIGHT_MS)) {
        // Respect the explicit persistent Backlight setting in the screen menu.
        if (uiconfig.screen_brightness != 1)
            digitalWrite(PIN_EINK_EN, LOW);
        timedLight = false;
        LOG_INFO("TouchLight T1: expired input=%u light=%u", pressed, digitalRead(PIN_EINK_EN));
    }
    return 100;
}
#endif
