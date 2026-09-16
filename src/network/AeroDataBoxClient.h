#pragma once

#include "RouteProvider.h"

// Implements RouteProvider.h's fetchRouteLegs() against AeroDataBox (via
// RapidAPI) - https://aerodatabox.p.rapidapi.com/flights/callsign/{callsign},
// headers X-RapidAPI-Key/X-RapidAPI-Host. Response is a JSON array (one
// element per scheduled leg for that callsign today) or, per the sibling
// desktop script's defensive handling, sometimes a single object.
//
// Field-name mapping confirmed against a real live response on 2026-09-16
// (captured via DEBUG's raw-JSON Serial.println in fetchOnce()):
// departure/arrival.airport.{iata,icao,location.{lat,lon}},
// {runwayTime,revisedTime,scheduledTime}.utc, top-level
// airline.{name,iata,icao}. Response is a JSON array, one element per
// scheduled leg for that callsign today (confirmed multi-leg in practice:
// a tracked UAL1283 returned two elements, DEN->EWR and EWR->LAS). One
// correction versus the pre-hardware assumption in the original design:
// the airport object DOES carry location.lat/lon, so
// RouteLeg::haveOriginCoords/haveDestCoords are populated when present,
// not always left false.
