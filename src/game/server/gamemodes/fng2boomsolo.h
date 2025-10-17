/* (c) KeksTW. */
#ifndef GAME_SERVER_GAMEMODES_FNG2BOOM_SOLO_H
#define GAME_SERVER_GAMEMODES_FNG2BOOM_SOLO_H

#include <game/server/gamecontroller.h>
#include <base/vmath.h>

#include "fng2.h"

class CGameControllerFNG2BoomSolo : public CGameControllerFNG2
{
public:
	CGameControllerFNG2BoomSolo(class CGameContext* pGameServer);
	CGameControllerFNG2BoomSolo(class CGameContext* pGameServer, CConfiguration& pConfig);
	virtual void Snap(int SnappingClient);
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	static CGameControllerFNG2* Construct(class CGameContext* pGameServer) {return new CGameControllerFNG2BoomSolo(pGameServer);};
	static CGameControllerFNG2* Construct(class CGameContext* pGameServer, CConfiguration& pConfig) {return new CGameControllerFNG2BoomSolo(pGameServer, pConfig);};
	static constexpr const char *g_Gametype = fng_typenames[3];
};
#endif
