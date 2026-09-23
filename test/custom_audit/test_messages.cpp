#include "MessageTextUtils.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>

int main()
{
    using namespace MessageTextUtils;
    assert(completeUtf8Prefix("", 0) == 0);
    const std::string text = u8"Її Єє Ґґ Іі ’ 🌍";
    assert(completeUtf8Prefix(text.data(), text.size()) == text.size());
    const std::string end = std::string(218, 'a') + u8"ї";
    assert(completeUtf8Prefix(end.data(), 219) == 218);
    assert(completeUtf8Prefix(end.data(), 220) == 220);
    const std::string emoji = std::string(216, 'a') + u8"🌍";
    for (size_t cut = 217; cut < 220; ++cut)
        assert(completeUtf8Prefix(emoji.data(), cut) == 216);
    assert(completeUtf8Prefix(emoji.data(), 220) == 220);
    const uint32_t self = 0x1234, radioA = 0x11223344, other = 0x7777;
    assert(matchesThread(radioA, self, 0, -1, radioA));
    assert(matchesThread(self, radioA, 0, -1, radioA));
    assert(!matchesThread(other, self, 0, -1, radioA));
    assert(!matchesThread(radioA, UINT32_MAX, 0, -1, radioA));
    assert(!matchesThread(radioA, self, 0, 0, 0));
    assert(matchesThread(radioA, UINT32_MAX, 0, 0, 0));
    assert(matchesThread(radioA, 0, 0, 0, 0));
    assert(!matchesThread(radioA, UINT32_MAX, 1, 0, 0));
    assert(matchesThread(other, self, 0, -1, 0));
    struct Record {
        uint16_t textOffset;
        int thread;
    };
    std::deque<Record> records;
    for (int n = 0; n < 20; ++n)
        records.push_back({static_cast<uint16_t>(n * 220), n % 2});
    assert(availableTextSlot<20>(records, 220) == 0);
    records.pop_back();
    assert(availableTextSlot<20>(records, 220) == 19 * 220);
    records.erase(records.begin() + 5);
    assert(availableTextSlot<20>(records, 220) == 5 * 220);
    const auto count = records.size();
    eraseOldestMatching(records, [](const Record &r) { return r.thread == 1; });
    assert(records.size() == count - 1);
    assert(records.front().textOffset == 0);
    assert(records[1].textOffset == 440);
    eraseOldestMatching(records, [](const Record &r) { return r.thread == 3; });
    assert(records.size() == count - 1);
    int forgotten = -1;
    eraseOldestMatching(
        records, [](const Record &r) { return r.thread == 0; }, [&](const Record &r) { forgotten = r.textOffset; });
    assert(forgotten == 0);
    forgotten = -1;
    eraseOldestMatching(
        records, [](const Record &r) { return r.thread == 99; }, [&](const Record &r) { forgotten = r.textOffset; });
    assert(forgotten == -1);
    std::cout << "Message boundary and conversation isolation tests passed\n";
}
