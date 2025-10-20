/* (c) Pig-Eye. */
/* Not that anyone cares */

#pragma once

class CGameControllerFNG2;
class CGameControllerFNG2Solo;
class CGameControllerFNG2Boom;
class CGameControllerFNG2BoomSolo;

#include <string.h>

#include "../fng2define.h"
#include "fng2.h"
#include "fng2solo.h"
#include "fng2boom.h"
#include "fng2boomsolo.h"

struct {
	const char *name;
	CGameControllerFNG2* (*s_constructor)(CGameContext*);
	CGameControllerFNG2* (*c_constructor)(CGameContext*, CConfiguration&);
} constexpr const fng_gametypes[4] = {
#define WITH_TYPE(T) {T::g_Gametype, T::Construct, T::Construct}
	WITH_TYPE(CGameControllerFNG2),
	WITH_TYPE(CGameControllerFNG2Solo),
	WITH_TYPE(CGameControllerFNG2Boom),
	WITH_TYPE(CGameControllerFNG2BoomSolo),
#undef WITH_TYPE
};

bool isValidFNGType(const char* name) {
    for (auto& gametype : fng_gametypes) {
        if (strcmp(name, gametype.name) == 0) {
            return true;
        }
    }

    return false;
}
