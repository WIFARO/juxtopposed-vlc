// SPDX-License-Identifier: GPL-2.0-or-later
#include "maininterface/deferred_video_on_top.hpp"
#include <cstdlib>
#include <iostream>

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static void roundtrip()
{
    for (bool value : {true, false})
    {
        vlc::DeferredVideoOnTop state;
        require(!state.take().has_value(), "initially empty");
        state.defer(value);
        const auto result = state.take();
        require(result.has_value(), "deferred request must be present");
        require(*result == value, "true and false roundtrip");
    }
}

static void latestWins()
{
    for (bool latest : {true, false})
    {
        vlc::DeferredVideoOnTop state;
        state.defer(!latest);
        state.defer(latest);
        const auto result = state.take();
        require(result.has_value() && *result == latest, "latest request wins in both directions");
    }
}

static void clearsPending()
{
    for (bool value : {true, false})
    {
        vlc::DeferredVideoOnTop state;
        state.defer(value);
        state.clear();
        require(!state.take().has_value(), "disable/replacement clears pending request");
        state.defer(!value);
        const auto result = state.take();
        require(result.has_value() && *result == !value, "new source may defer after clear");
    }
}

static void replaysOnce()
{
    for (bool value : {true, false})
    {
        vlc::DeferredVideoOnTop state;
        state.defer(value);
        const auto result = state.take();
        require(result.has_value() && *result == value, "first replay returns requested state");
        require(!state.take().has_value(), "replay consumes pending state exactly once");
    }
}

int main()
{
    roundtrip();
    latestWins();
    clearsPending();
    replaysOnce();
    std::cout << "deferred VIDEO topmost PASS\n";
}
