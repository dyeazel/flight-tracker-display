#pragma once

#include "Flight.h"

#ifdef SIM_DATA

// Scripted scenes for display work with no live traffic or network (env:esp32dev-sim; pattern:
// ../mlb-board's SimGames). Real traffic rarely produces the interesting states - a flagged stale route, a
// corrected origin, a guessed destination, a landed flight, very long airport names - so each scene forces
// one, and tools/screenshot.py can capture every scene from a real panel:
//   python3 tools/screenshot.py out.png --sim
// Scenes auto-cycle every kStepMs unless one is held with the "_sim" serial command.
namespace SimFlights {

constexpr uint32_t kStepMs = 7000;

int count();
const char *name(int scene);

// The scene to show at `nowMs` (auto-cycling, or the held one).
int current(uint32_t nowMs);
// n >= 0 holds scene n; n < 0 resumes cycling.
void hold(int n);

struct Scene {
    int count = 0;              // flights filled
    int missingCount = 0;       // "Not found: ..." targets
    bool anyFlagged = false;    // legend "* = route may be stale"
    bool error = false;         // show the "Error fetching data" screen instead
    bool refreshing = false;    // overlay the yellow "refreshing" heartbeat
};

// Fills `flights` (up to maxFlights) and `missing` for `scene`; times are relative to `nowUtc` (0 = unsynced,
// in which case the timeline row is simply omitted).
Scene fill(int scene, Flight *flights, int maxFlights, char missing[][12], long nowUtc);

}  // namespace SimFlights

#endif  // SIM_DATA
