#include "NodeDB.h"
#include "PowerFSM.h"
#include "input/TouchBacklight.h"
#include <cassert>
#include <cstdio>

static uint32_t clockMs = 1000;
static int input = LOW, output = LOW;
static void (*interruptHandler)() = nullptr;
bool runASAP = false;
TestUIConfig uiconfig;
TestPowerFSM powerFSM;
concurrency::TestDelay concurrency::mainDelay;

uint32_t millis()
{
    return clockMs;
}
void pinMode(int pin, int mode)
{
    assert(pin == BUTTON_PIN_TOUCH && mode == INPUT_PULLDOWN_SENSE);
}
int digitalRead(int pin)
{
    assert(pin == BUTTON_PIN_TOUCH || pin == PIN_EINK_EN);
    return pin == BUTTON_PIN_TOUCH ? input : output;
}
void digitalWrite(int pin, int value)
{
    assert(pin == PIN_EINK_EN);
    output = value;
}
int attachInterrupt(uint32_t pin, void (*callback)(), uint32_t mode)
{
    assert(pin == BUTTON_PIN_TOUCH && mode == RISING);
    interruptHandler = callback;
    return 1;
}
class TestTouch : public TouchBacklight
{
  public:
    using TouchBacklight::runOnce;
};
static void pulse()
{
    input = HIGH;
    interruptHandler();
    input = LOW;
}

int main()
{
    TestTouch touch;
    touch.runOnce();
    assert(output == LOW && powerFSM.wakes == 0);

    pulse(); // Entire pulse finishes before the main thread gets scheduled.
    assert(output == LOW && powerFSM.wakes == 0);
    assert(runASAP && concurrency::mainDelay.wakes == 1);
    clockMs += 500;
    touch.runOnce();
    assert(output == HIGH && powerFSM.wakes == 1);
    clockMs += TouchBacklight::LIGHT_MS - 1;
    touch.runOnce();
    assert(output == HIGH);
    ++clockMs;
    touch.runOnce();
    assert(output == LOW);

    pulse();
    touch.runOnce();
    clockMs += 20000;
    pulse();
    touch.runOnce();
    clockMs += 20000;
    touch.runOnce();
    assert(output == HIGH); // A new touch extends the reading period.
    clockMs += 10000;
    touch.runOnce();
    assert(output == LOW);

    input = HIGH; // Poll fallback also works without an interrupt.
    touch.runOnce();
    assert(output == HIGH);
    const auto wakes = powerFSM.wakes;
    clockMs += 31000;
    touch.runOnce();
    assert(output == LOW && powerFSM.wakes == wakes); // Stuck/held pad cannot latch light on.
    input = LOW;
    touch.runOnce();

    clockMs = UINT32_MAX - 5000;
    pulse();
    touch.runOnce();
    clockMs += 29999;
    touch.runOnce();
    assert(output == HIGH);
    ++clockMs;
    touch.runOnce();
    assert(output == LOW); // Millisecond rollover.

    pulse();
    touch.runOnce();
    uiconfig.screen_brightness = 1;
    clockMs += 30000;
    touch.runOnce();
    assert(output == HIGH); // Explicit menu setting is respected.
    uiconfig.screen_brightness = 153;
    output = LOW; // Menu switched it off again.
    touch.runOnce();
    assert(output == LOW);

    input = HIGH;
    TestTouch initiallyHigh;
    initiallyHigh.runOnce();
    assert(output == LOW); // No fake touch at startup.
    input = LOW;
    initiallyHigh.runOnce();
    pulse();
    initiallyHigh.runOnce();
    assert(output == HIGH);
    puts("PASS touch backlight: short pulse, hold, retrigger, expiry, rollover, manual setting, startup");
}
