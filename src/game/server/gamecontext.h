/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/server.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>
#include <engine/shared/protocol.h> // for NETADDR

#include <game/layers.h>
#include <game/voting.h>

#include "game/server/entity.h"
#include "eventhandler.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"

#include <string>
#include <vector>
#include <map>

#include <engine/storage.h>

#ifndef QUADRO_MASK
#define QUADRO_MASK
struct QuadroMask {
	long long m_Mask[4];
	QuadroMask(long long mask) {
		memset(m_Mask, mask, sizeof(m_Mask));
	}
	QuadroMask() {}
	QuadroMask(long long Mask, int id) {
		memset(m_Mask, 0, sizeof(m_Mask));
		m_Mask[id] = Mask;
	}

	void operator|=(const QuadroMask& mask) {
		m_Mask[0] |= mask[0];
		m_Mask[1] |= mask[1];
		m_Mask[2] |= mask[2];
		m_Mask[3] |= mask[3];
	}

	long long& operator[](int id) {
		return m_Mask[id];
	}

	long long operator[](int id) const {
		return m_Mask[id];
	}

	QuadroMask operator=(long long mask) {
		memset(m_Mask, mask, sizeof(m_Mask));
		return *this;
	}

	long long operator & (const QuadroMask& mask){
		return (m_Mask[0] & mask[0]) | (m_Mask[1] & mask[1]) | (m_Mask[2] & mask[2]) | (m_Mask[3] & mask[3]);
	}	

	QuadroMask& operator^(long long mask) {
		m_Mask[0] ^= mask;
		m_Mask[1] ^= mask;
		m_Mask[2] ^= mask;
		m_Mask[3] ^= mask;
		return *this;
	}
	
	bool operator==(QuadroMask& q){
		return m_Mask[0] == q.m_Mask[0] && m_Mask[1] == q.m_Mask[1] && m_Mask[2] == q.m_Mask[2] && m_Mask[3] == q.m_Mask[3];
	}

	int Count() {
		int Counter = 0;
		for (int i = 0; i < 4; ++i) {
			for (int n = 0; n < 64; ++n) {
				if ((m_Mask[i] & (1ll << n)) != 0)
					++Counter;
			}
		}

		return Counter;
	}

	int PositionOfNonZeroBit(int Offset) {
		for (int i = (Offset / 64); i < 4; ++i) {
			for (int n = (Offset % 64); n < 64; ++n) {
				if ((m_Mask[i] & (1ll << n)) != 0) {
					return i * 64 + n;
				}
			}
		}
		return -1;
	}
	
	void SetBitOfPosition(int Pos){
		m_Mask[Pos / 64] |= 1 << (Pos % 64);
	}
};
#endif

//str_comp_nocase_whitespace
//IMPORTANT: the pArgs can not be accessed by null zero termination. they are not splited by 0, but by space... in case a function needs the whole argument at once. use functions above
typedef void (*ServerCommandExecuteFunc)(class CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);

struct sServerCommand{
	const char* m_Cmd;
	const char* m_Desc;
	const char* m_ArgFormat;
	sServerCommand* m_NextCommand;
	ServerCommandExecuteFunc m_Func;
	
	sServerCommand(const char* pCmd, const char* pDesc, const char* pArgFormat, ServerCommandExecuteFunc pFunc) : m_Cmd(pCmd), m_Desc(pDesc), m_ArgFormat(pArgFormat), m_NextCommand(0), m_Func(pFunc) {}
	
	void ExecuteCommand(class CGameContext* pContext, int pClientID, const char* pArgs){	
		const char* m_Args[128/2];
		int m_ArgCount = 0;
		
		const char* c = pArgs;
		const char* s = pArgs;
		while(c && *c){
			if(is_whitespace(*c)){
				m_Args[m_ArgCount++] = s;
				s = c + 1;
			}
			++c;
		}
		if (s) {
			m_Args[m_ArgCount++] = s;
		}
		
		m_Func(pContext, pClientID, m_Args, m_ArgCount);
	}
};

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/
class CPlayer;

class CGameContext : public IGameServer
{
	IServer *m_pServer;
	class IConsole *m_pConsole;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConShuffleTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConLockTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void ConChangeGamemode(IConsole::IResult *pResult, void *pUserData);
	static void ConMakeSay(IConsole::IResult *pResult, void *pUserData);

	CGameContext(int Resetting);
	CGameContext(int Resetting, CConfiguration* pConfig);
	void Construct(int Resetting);

	bool m_Resetting;
	
	sServerCommand* FindCommand(const char* pCmd);
	void AddServerCommandSorted(sServerCommand* pCmd);

	void AddFrozenLeaver(const NETADDR &Addr, int Seconds);
	bool IsFrozenLeaverBlocked(const NETADDR &Addr);
	void ExpireFrozenLeavers();
    
public:
    enum { MAX_FROZEN_LEAVERS = 16 };
    struct CFrozenLeaver
    {
        NETADDR m_Addr;
        int m_ExpireTick;
    };

    CFrozenLeaver m_aFrozenLeavers[MAX_FROZEN_LEAVERS];
    int m_FrozenLeaverCount;

	sServerCommand* m_FirstServerCommand;
	void AddServerCommand(const char* pCmd, const char* pDesc, const char* pArgFormat, ServerCommandExecuteFunc pFunc);
	void ExecuteServerCommand(int pClientID, const char* pLine);
	
	static void CmdStats(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
	static void CmdWhisper(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
	static void CmdConversation(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
	static void CmdHelp(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
	static void CmdEmote(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    
	IServer *Server() const { return m_pServer; }
	class IConsole *Console() { return m_pConsole; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CGameContext();
	~CGameContext();

	void Clear();

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];

	IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);

	int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote();
	void SendVoteSet(int ClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientID);

	int m_VoteCreator;
	int64 m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_NumVoteOptions;
	int m_VoteEnforce;
	enum
	{
		VOTE_ENFORCE_UNKNOWN=0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// Constants regarding player blocking
	float m_BlockSecondsIncrease = 0.05;
	float m_BlockSecondsMax = 2.0;
	int m_BlockMessageDelay = 3 * Server()->TickSpeed();

	// helper functions
	void MakeLaserTextPoints(vec2 pPos, int pOwner, int pPoints);
	
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, int Team, int FromPlayerID = -1, int ViewerID = -1);
	void CreateSoundTeam(vec2 Pos, int Sound, int TeamID, int FromPlayerID = -1);

	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage);
	void CreateHammerHit(vec2 Pos);
	void CreatePlayerSpawn(vec2 Pos);
	void CreateDeath(vec2 Pos, int Who);
	void CreateSound(vec2 Pos, int Sound, QuadroMask Mask=QuadroMask(-1ll));
	void CreateSoundGlobal(int Sound, int Target=-1);
    
    //for triggerbot anticheat
    CCharacter* GetClosestEnemyToAim(CCharacter* pShooter, float MaxDistance);
    
	enum
	{
		CHAT_ALL=-2,
		CHAT_SPEC=-1,
		CHAT_RED=0,
		CHAT_BLUE=1,
		//for ddnet client only
		CHAT_WHISPER_SEND=2,
		CHAT_WHISPER_RECV=3,
	};

	// network
	void SendChatTarget(int To, const char *pText);
	void SendChat(int ClientID, int Team, const char *pText, int To = -1);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendBroadcast(const char *pText, int ClientID);


	//
	void CheckPureTuning();
	void SendTuningParams(int ClientID);
	void SendFakeTuningParams(int ClientID);

	//
	void SwapTeams();

	// engine events
	virtual void OnInit();
	virtual void OnInit(class IKernel *pKernel, class IMap* pMap, struct CConfiguration* pConfigFile = 0);
	virtual void OnConsoleInit();
	virtual void OnShutdown();

	virtual int PreferedTeamPlayer(int ClientID);

	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual void OnClientConnected(int ClientID, int PreferedTeam = -2);
	virtual void OnClientEnter(int ClientID);
	virtual bool OnClientDrop(int ClientID, const char *pReason, bool Force);
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput);

	virtual bool IsClientReady(int ClientID);
	virtual bool IsClientPlayer(int ClientID);

	virtual const char *GameType();
	virtual const char *Version();
	virtual const char *NetVersion();

	void SendRoundStats();
	void SendRandomTrivia();

	template<class T>
	int SendPackMsg(T *pMsg, int Flags)
	{
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p) continue;
			Server()->SendPackMsg(pMsg, Flags, i);
		}
		return 0;
	}

	int SendPackMsg(CNetMsg_Sv_KillMsg *pMsg, int Flags);

	int SendPackMsg(CNetMsg_Sv_Emoticon *pMsg, int Flags);

	int SendPackMsg(CNetMsg_Sv_Chat *pMsg, int Flags);

	int SendPackMsg(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID);
    
    bool m_DyncamEnabled = true;
    std::map<std::string, int64_t> m_MutedIPs;
    
    void SavePlayerStatsToFile(class CPlayer *pPlayer);
    void LoadPlayerStatsFromFile(class CPlayer *pPlayer);
    void SaveRoundStatsToFile(class CPlayer *pPlayer);
    void LoadRoundStatsFromFile(class CPlayer *pPlayer);
    IStorage *Storage() { return Kernel()->RequestInterface<IStorage>(); }
    bool LoadRoundStatsByName(const char* pName, CPlayer* pTmp);
    bool LoadTotalStatsByName(const char* pName, CPlayer* pTmp);
    
    //custom commands
    bool CheckStatCommandCooldown(int ClientID);
    
    static void CmdCmdList(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void ConToggleDyncam(IConsole::IResult *pResult, void *pUserData);
    static void CmdUseless(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void ConMute(IConsole::IResult *pResult, void *pUser);
    static void ConUnmute(IConsole::IResult *pResult, void *pUser);
    static void CmdPause(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdStatsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdFewSteals(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdFewStealsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopSteals(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopStealsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopWalls(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopWallsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopWrong(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopWrongAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopKills(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopKillsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopAccuracy(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopAccuracyAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopFreeze(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopFreezeAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopGreen(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopGold(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopPurple(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopSpree(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopMulti(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopSaves(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopSavesAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopFrozen(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopFrozenAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopKD(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopKDAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum);
    static void CmdTopStats(CGameContext *pContext, int ClientID, const char **pArgs, int ArgNum);
    static void CmdEmoteEyes(CGameContext *pContext, int ClientID, const char **pArgs, int ArgNum);
    void SetEyeEmoteDefault(int ClientID);
    static void CmdEarrape(CGameContext* pContext, int ClientID, const char** pArgs, int ArgNum);

    
    int64_t m_LastEarrapeTick;
    
};

inline QuadroMask CmaskAll() { return QuadroMask(-1); }
inline QuadroMask CmaskOne(int ClientID) { return QuadroMask(1ll<<(ClientID%(sizeof(long long)*8)), (ClientID/(sizeof(long long)*8))); }
inline QuadroMask CmaskAllExceptOne(int ClientID) { return CmaskOne(ClientID)^0xffffffffffffffffll; }
inline bool CmaskIsSet(QuadroMask Mask, int ClientID) { return (Mask&CmaskOne(ClientID)) != 0; }
extern "C" void NormalizeChat(char *pStr, int Size);
#endif
