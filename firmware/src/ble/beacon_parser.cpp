#include "beacon_parser.h"
#include "../core/identity.h"
#include <string.h>
#include <ctype.h>

// Expects manufacturer data as a raw byte string (not necessarily NUL-terminated).
// We treat it as ASCII text starting with "ST:"
bool beaconParse(const uint8_t* data, size_t len, ParsedBeacon* out) {
    if (!data || len < 4) return false;

    // Must start with "ST:"
    if (data[0] != 'S' || data[1] != 'T' || data[2] != ':') return false;

    // Copy to a local NUL-terminated buffer for easier parsing
    char buf[140];
    size_t copyLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, data, copyLen);
    buf[copyLen] = '\0';

    // Detect and strip flag suffixes ~P and ~B in any order, e.g.:
    //   "ST:WAKE~P"    "ST:TAMA:A1B2~B"    "ST:CRY~P~B"
    out->proximityRequired = false;
    out->bootRequired      = false;
    {
        bool stripped;
        do {
            stripped = false;
            size_t blen = strlen(buf);
            if (blen >= 2 && buf[blen - 2] == '~') {
                char flag = buf[blen - 1];
                if (flag == 'P') { buf[blen - 2] = '\0'; out->proximityRequired = true; stripped = true; }
                else if (flag == 'B') { buf[blen - 2] = '\0'; out->bootRequired = true;      stripped = true; }
            }
        } while (stripped);
    }

    // buf = "ST:<CMD>[:<ID>][|<payload>]"
    const char* p = buf + 3; // skip "ST:"

    // Extract command (up to ':' or '|' or end)
    char cmd[16] = {};
    uint8_t ci = 0;
    while (*p && *p != ':' && *p != '|' && ci < sizeof(cmd) - 1) {
        cmd[ci++] = *p++;
    }

    // Optional badge ID (after ':')
    out->badgeId[0] = '\0';
    if (*p == ':') {
        p++;
        uint8_t bi = 0;
        while (*p && *p != '|' && bi < 6) {
            out->badgeId[bi++] = toupper(*p++);
        }
        out->badgeId[bi] = '\0';
    }

    // Optional payload (after '|')
    out->payload[0] = '\0';
    if (*p == '|') {
        p++;
        strncpy(out->payload, p, sizeof(out->payload) - 1);
    }

    // Map command string to enum
    if      (strcmp(cmd, "WAKE")     == 0) out->cmd = BeaconCmd::Wake;
    else if (strcmp(cmd, "SLEEP")    == 0) out->cmd = BeaconCmd::Sleep;
    else if (strcmp(cmd, "BG_ON")    == 0) out->cmd = BeaconCmd::BgOn;
    else if (strcmp(cmd, "BG_OFF")   == 0) out->cmd = BeaconCmd::BgOff;
    else if (strcmp(cmd, "OTA")      == 0) out->cmd = BeaconCmd::Ota;
    else if (strcmp(cmd, "CRY")      == 0) out->cmd = BeaconCmd::Cry;
    else if (strcmp(cmd, "MSG")      == 0) out->cmd = BeaconCmd::Message;
    else if (strcmp(cmd, "TAMA")     == 0) out->cmd = BeaconCmd::Tama;
    else if (strcmp(cmd, "CLEAR")    == 0) out->cmd = BeaconCmd::Clear;
    else                                   { out->cmd = BeaconCmd::Unknown; return false; }

    return true;
}

bool beaconIsForMe(const ParsedBeacon& b) {
    if (b.badgeId[0] == '\0') return true; // broadcast
    return strcmp(b.badgeId, badgeIdHexString()) == 0;
}
