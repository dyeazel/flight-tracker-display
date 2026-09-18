#pragma once

#include "Flight.h"

// Checks a commercial flight's claimed destination against its own current
// position/altitude/vertical-rate/on-ground state - all already fetched
// every poll, so (unlike DepartureVerifier) this makes no network call of
// its own. See config.h's "Destination verification" section for the
// rationale and tuning constants.
namespace DestinationVerifier {

// Call once per commercial flight per real poll, after f.destination is
// set. Two independent checks, in priority order:
//
//  - f.onGround: f.lat/f.lon IS the landing position, a settled fact. If
//    it doesn't match the claimed destination, corrects f.destination/
//    f.destinationName to the real nearest airport found there.
//  - Still airborne, descending, and below DESCENT_GUESS_MAX_AGL_FT above
//    some airport's own field elevation: if that airport isn't the
//    claimed destination, corrects f.destination/f.destinationName to it -
//    but this is a guess (a go-around/hold could still happen), so
//    f.destinationGuessed is set true when this is the reason for a
//    correction, false otherwise (including when nothing needed
//    correcting, or the flight was landed-confirmed instead).
//
// No-op (leaves f.destination/f.destinationName untouched,
// f.destinationGuessed left at its current value) if f isn't commercial,
// has no claimed destination code, or neither check applies (cruising, or
// too far from any table airport to say anything useful).
void verifyOrCorrectDestination(Flight &f);

}  // namespace DestinationVerifier
