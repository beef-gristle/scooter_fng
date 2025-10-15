/* (c) Pig-Eye. */
/* Not that anyone cares */

#ifndef GAME_SERVER_GAMEMODES_FNG2_TYPES_H
#define GAME_SERVER_GAMEMODES_FNG2_TYPES_H

#include "fng2.h"
#include "fng2solo.h"
#include "fng2boom.h"
#include "fng2boomsolo.h"

struct {
	const char *gametype;
	CGameControllerFNG2* (*s_constructor)(CGameContext*);
	CGameControllerFNG2* (*c_constructor)(CGameContext*, CConfiguration&);
} fng_gametypes[4] = {
#define WITH_TYPE(T) {T::g_Gametype, T::Construct, T::Construct}
	WITH_TYPE(CGameControllerFNG2),
	WITH_TYPE(CGameControllerFNG2Solo),
	WITH_TYPE(CGameControllerFNG2Boom),
	WITH_TYPE(CGameControllerFNG2BoomSolo),
#undef WITH_TYPE
};

bool isValidFNGType(const char* gametypeName) {
    for (auto& gametype : fng_gametypes) {
        if (strcmp(gametypeName, gametype.gametype) == 0) {
            return true;
        }
    }

    return false;
}

#endif
