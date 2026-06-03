#include "storage.h"

Preferences storageOpen(const char* ns, bool readOnly) {
    Preferences p;
    p.begin(ns, readOnly);
    return p;
}
