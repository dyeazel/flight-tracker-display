#include "config.h"

bool DEBUG = false;

// Leave empty to show whatever's nearby, per config.h's TARGET_CALLSIGNS_COUNT.
// To track specific flights, set TARGET_CALLSIGNS_COUNT to match and list them
// here, e.g. {"DAL123", "UAL456"}.
const char *const TARGET_CALLSIGNS[] = {};

const char *const CHARTER_OPERATOR_PREFIXES[CHARTER_OPERATOR_PREFIXES_COUNT] = {
    "EJA", "EJM", "LXJ", "OPT", "XOJ"};
