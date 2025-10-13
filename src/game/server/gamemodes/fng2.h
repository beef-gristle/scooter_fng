/* (c) KeksTW. */
#ifndef GAME_SERVER_GAMEMODES_FNG2_H
#define GAME_SERVER_GAMEMODES_FNG2_H

#include <game/server/gamecontroller.h>
#include <base/vmath.h>

class CGameControllerFNG2 : public IGameController
{
public:
	CGameControllerFNG2(class CGameContext* pGameServer);
	CGameControllerFNG2(class CGameContext* pGameServer, CConfiguration& pConfig);
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	
	virtual void DoWincheck();
	
	virtual void PostReset();

	static CGameControllerFNG2* Construct(class CGameContext* pGameServer) {return new CGameControllerFNG2(pGameServer);};
	static CGameControllerFNG2* Construct(class CGameContext* pGameServer, CConfiguration& pConfig) {return new CGameControllerFNG2(pGameServer, pConfig);};
	static constexpr const char *g_Gametype = "fng2";
protected:
	void EndRound();	
};
#endif
