#include "AdsbdbParse.h"

#include <string.h>

#include "util/Ascii.h"

namespace AdsbdbParse {

bool parseRoute(JsonVariant root, AdsbdbClient::RouteInfo *info) {
    JsonVariant route = root["response"]["flightroute"];
    if (route.isNull()) return false;

    JsonVariant airline = route["airline"];
    JsonVariant origin = route["origin"];
    JsonVariant dest = route["destination"];

    if (!airline.isNull()) {
        strncpy(info->airline, airline["name"] | "", sizeof(info->airline) - 1);
        Ascii::sanitize(info->airline);
        const char *iata = airline["iata"] | "";
        const char *icao = airline["icao"] | "";
        strncpy(info->airlineCode, (*iata ? iata : icao), sizeof(info->airlineCode) - 1);
    }
    if (!origin.isNull()) {
        const char *iata = origin["iata_code"] | "";
        const char *icao = origin["icao_code"] | "";
        strncpy(info->originCode, (*iata ? iata : icao), sizeof(info->originCode) - 1);
        strncpy(info->originName, origin["name"] | "", sizeof(info->originName) - 1);
        Ascii::sanitize(info->originName);
        if (!origin["latitude"].isNull() && !origin["longitude"].isNull()) {
            info->originLat = origin["latitude"].as<float>();
            info->originLon = origin["longitude"].as<float>();
            info->haveOriginCoords = true;
        }
    }
    if (!dest.isNull()) {
        const char *iata = dest["iata_code"] | "";
        const char *icao = dest["icao_code"] | "";
        strncpy(info->destinationCode, (*iata ? iata : icao), sizeof(info->destinationCode) - 1);
        strncpy(info->destinationName, dest["name"] | "", sizeof(info->destinationName) - 1);
        Ascii::sanitize(info->destinationName);
        if (!dest["latitude"].isNull() && !dest["longitude"].isNull()) {
            info->destLat = dest["latitude"].as<float>();
            info->destLon = dest["longitude"].as<float>();
            info->haveDestCoords = true;
        }
    }
    return true;
}

}  // namespace AdsbdbParse
