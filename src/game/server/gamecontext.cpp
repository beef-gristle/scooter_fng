/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include "engine/shared/protocol.h"
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include "gamemodes/fng2_types.h"
#include <cstdint>
#include <cctype>

//other gametypes(for modding without changing original sources)
#include "gamecontext_additional_gametypes_includes.h"

#include "laserText.h"
#include "gameserver_config.h"

#include <engine/storage.h>       // for Storage()
#define LINE_READER_IMPLEMENTATION
#include <engine/shared/linereader.h> // for io_read_line
#include <cstring>                // for strncmp / strcpy if needed
#include <algorithm>
#include <set>
#include <string>
#include <base/system.h>

#include <vector>
#include <time.h>
#include "player.h"
#include <engine/server.h>       // for IServer, NETADDR
#include <engine/server/server.h> // for CServer, BanAddr
#include <engine/shared/protocol.h> // for NETADDR
#include "slurlist.h" //for toxic player mute
#include <climits>
#define STORAGE_IMPLEMENTATION
extern IStorage *CreateStorage(int Type, int NumArgs, const char **ppArguments);
extern IStorage *CreateTempStorage();
extern IStorage *Storage();

static const int DUMMY_CID = CPlayer::VANILLA_CLIENT_MAX_CLIENTS; // == 16 on vanilla
void FormatTime(int Seconds, char* pBuf, int BufSize);


inline const char *str_chr(const char *pStr, char c)
{
	while(*pStr)
	{
		if(*pStr == c)
			return pStr;
		pStr++;
	}
	return 0;
}

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;
	m_FirstServerCommand = 0;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	m_Config = &g_Config;
	Construct(Resetting);
    m_LastEarrapeTick = -Server()->Tick();
}

CGameContext::CGameContext(int Resetting, CConfiguration* pConfig)
{
	m_Config = pConfig;
	Construct(Resetting);
}

CGameContext::CGameContext() 
{
	m_Config = &g_Config;
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	
	sServerCommand* pCmd = m_FirstServerCommand;
	while(pCmd){
		sServerCommand* pThis = pCmd;
		pCmd = pCmd->m_NextCommand;
		delete pThis;
	}
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;
	
	CConfiguration* pConfig = m_Config;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET, pConfig);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::MakeLaserTextPoints(vec2 pPos, int pOwner, int pPoints){
	char text[10];
	if(pPoints >= 0) str_format(text, 10, "+%d", pPoints);
	else str_format(text, 10, "%d", pPoints);
	pPos.y -= 20.0 * 2.5;
	new CLaserText(&m_World, pPos, pOwner, Server()->TickSpeed() * 3, text, (int)(strlen(text)));
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount, int Team, int FromPlayerID, int ViewerID)
{
	float a = 3 * 3.14159f / 2 + Angle;
	float s = a - pi / 3;
	float e = a + pi / 3;

	if(m_pController->IsTeamplay()){
		QuadroMask mask = 0;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (m_apPlayers[i] && (m_apPlayers[i]->GetTeam() == Team || m_apPlayers[i]->GetCID() == ViewerID)) mask |= CmaskOne(i);
			
			// m_apPlayers[i]->m_LastKill;
			// m_apPlayers[i]->GetCharacter()->m_FrozenBy

			
		}

		for (int i = 0; i < Amount; i++)
		{
			float f = mix(s, e, float(i + 1) / float(Amount + 2));
			CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd), mask);
			if (pEvent)
			{
				pEvent->m_X = (int)Pos.x;
				pEvent->m_Y = (int)Pos.y;
				pEvent->m_Angle = (int)(f*256.0f);
			}
		}
	} else if(FromPlayerID != -1){
		for (int i = 0; i < Amount; i++)
		{
			float f = mix(s, e, float(i + 1) / float(Amount + 2));
			CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd), CmaskOne(FromPlayerID));
			if (pEvent)
			{
				pEvent->m_X = (int)Pos.x;
				pEvent->m_Y = (int)Pos.y;
				pEvent->m_Angle = (int)(f*256.0f);
			}
		}		
	}
}

void CGameContext::CreateSoundTeam(vec2 Pos, int Sound, int TeamID, int FromPlayerID)
{
	if (Sound < 0)
		return;

	//Only when teamplay is activated
	if (m_pController->IsTeamplay()) {
		QuadroMask mask = 0;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (m_apPlayers[i] && m_apPlayers[i]->GetTeam() == TeamID && (FromPlayerID != i)) mask |= CmaskOne(i);
		}

		// create a sound
		CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), mask);
		if (pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_SoundID = Sound;
		}
	}
	//the player "causing" this events gets a global sound
	if (FromPlayerID != -1) CreateSoundGlobal(Sound, FromPlayerID);
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l;
			if((int)Dmg)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, QuadroMask Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	int Flag = MSGFLAG_VITAL;
	if(Target != -1)
		Flag |= MSGFLAG_NORECORD;
	Server()->SendPackMsg(&Msg, Flag, Target);	
}


void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}


void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText, int To)
{
	if(Team != CHAT_WHISPER_RECV && Team != CHAT_WHISPER_SEND){
		char aBuf[256];
		if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
			str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
		else
			str_format(aBuf, sizeof(aBuf), "*** %s", pText);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);
	}
	
	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		SendPackMsg(&Msg, MSGFLAG_VITAL);
	}

	else if((Team == CHAT_WHISPER_RECV || Team == CHAT_WHISPER_SEND) && To != -1){
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = Team;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, To);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
				SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	SendPackMsg(&Msg, MSGFLAG_VITAL);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//tune for frozen tees
void CGameContext::SendFakeTuningParams(int ClientID)
{
	static CTuningParams FakeTuning;
	
	FakeTuning.m_GroundControlSpeed = 0;
	FakeTuning.m_GroundJumpImpulse = 0;
	FakeTuning.m_GroundControlAccel = 0;
	FakeTuning.m_AirControlSpeed = 0;
	FakeTuning.m_AirJumpImpulse = 0;
	FakeTuning.m_AirControlAccel = 0;
	FakeTuning.m_HookDragSpeed = 0;
	FakeTuning.m_HookDragAccel = 0;
	FakeTuning.m_HookFireSpeed = 0;
	
	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&FakeTuning;
	for(unsigned i = 0; i < sizeof(FakeTuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;
	
	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_apPlayers[i]->SetTeam(m_apPlayers[i]->GetTeam()^1, false);
	}

	(void)m_pController->CheckTeamBalance();
}

void CGameContext::OnTick()
{
	ExpireFrozenLeavers();

	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	// game controller logic
	m_pController->Tick();

	// update all players
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(pPlayer)
		{
			pPlayer->Tick();
			pPlayer->PostTick();

			// --- MULTI KILL EXPIRY CHECK ---
			if(pPlayer->m_MultiCount > 1 && Server()->Tick() > pPlayer->m_MultiEndAnnounceTick)
            {
                SendChatTarget(i, "multi ended");

                pPlayer->m_MultiCount = 0;
                pPlayer->m_LastKillTick = -1;
                pPlayer->m_MultiEndAnnounceTick = -1;
            }
		}
	}

	// voting system
	if(m_VoteCloseTime)
	{
		// abort vote
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;

			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);

				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

#ifdef CONF_DEBUG
	if(m_Config->m_DbgDummies)
	{
		for(int i = 0; i < m_Config->m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->Respawn();
    str_copy(m_apPlayers[ClientID]->m_aOriginalName, Server()->ClientName(ClientID), sizeof(m_apPlayers[ClientID]->m_aOriginalName));
	if(!m_Config->m_SvTournamentMode || m_pController->IsGameOver()) {
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()));
		SendChat(-1, CGameContext::CHAT_ALL, aBuf);
        

		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	m_VoteUpdate = true;
}

void CGameContext::OnClientConnected(int ClientID, int PreferedTeam)
{
	// Get IP before doing anything
	const NETADDR *pRawAddr = ((CServer*)Server())->m_NetServer.ClientAddr(ClientID);
	if(!pRawAddr)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", "[debug] m_NetServer.ClientAddr() failed!");
		return;
	}

	NETADDR Addr = *pRawAddr;
	Addr.port = 0; // Ignore port for blocking

	// Show joining IP
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pRawAddr, aAddrStr, sizeof(aAddrStr), true);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "[debug] ClientID=%d IP=%s (port ignored for check)", ClientID, aAddrStr);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", aBuf);

	// Check frozen leaver block
	if(IsFrozenLeaverBlocked(Addr))
    {
        // Calculate remaining time
        int RemainingTicks = 0;
        for(int i = 0; i < m_FrozenLeaverCount; ++i)
        {
            NETADDR BanAddr = m_aFrozenLeavers[i].m_Addr;
            BanAddr.port = 0;

            if(net_addr_comp(&BanAddr, &Addr) == 0)
            {
                RemainingTicks = m_aFrozenLeavers[i].m_ExpireTick - Server()->Tick();
                break;
            }
        }
        int RemainingSeconds = RemainingTicks / Server()->TickSpeed();

        // Build and print messages
        char aAddrStr[NETADDR_MAXSTRSIZE];
        net_addr_str(pRawAddr, aAddrStr, sizeof(aAddrStr), true);

        char aBuf[128];
        str_format(aBuf, sizeof(aBuf), "Rejected frozen leaver %s (still blocked, %ds left)", aAddrStr, RemainingSeconds);
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", aBuf);

        str_format(aBuf, sizeof(aBuf), "You left while frozen. Please wait %d seconds.", RemainingSeconds);
        Server()->Kick(ClientID, aBuf);
        return;
    }
    
	// Create player
	int StartTeam = m_pController->ClampTeam(PreferedTeam);
	if(PreferedTeam == -2)
		StartTeam = m_pController->GetAutoTeam(ClientID);

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
	m_pController->CheckTeamBalance();
    
    CPlayer *pPlayer = m_apPlayers[ClientID];
    if(pPlayer)
    {
        pPlayer->ResetStats(); // always clear current session stats
        LoadPlayerStatsFromFile(pPlayer); // load saved stats into max spree, multi, etc
        LoadRoundStatsFromFile(pPlayer); // load saved stats into max spree, multi, etc
    }
    
#ifdef CONF_DEBUG
	if(m_Config->m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS - m_Config->m_DbgDummies)
		{
			m_apPlayers[ClientID]->m_Stats.m_Kills = ClientID;
			return;
		}
	}
#endif

	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = m_Config->m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

bool CGameContext::OnClientDrop(int ClientID, const char *pReason, bool Force)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return true;

	CCharacter *pChar = pPlayer->GetCharacter();

	if(pChar && pChar->IsFrozen() && !m_pController->IsGameOver() && !Force)
	{
		if(!pPlayer->m_SentRagequitMessage)
		{
			const char *pOld = Server()->ClientName(ClientID, true);
			char aOldName[64];
			str_copy(aOldName, pOld, sizeof(aOldName));
			
			// Save original name to player and character
			str_copy(pPlayer->m_aOriginalName, aOldName, sizeof(pPlayer->m_aOriginalName));
			if(pChar)
				str_copy(pChar->m_aOriginalName, aOldName, sizeof(pChar->m_aOriginalName));

			// Rename the player name (invisible to character code)
			Server()->SetClientName(ClientID, "noob");

			// Broadcast ragequit
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "'%s' ragequit", aOldName);
			SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			pPlayer->m_SentRagequitMessage = true;

			// Add to frozen leaver ban list
			char aAddrStr[NETADDR_MAXSTRSIZE];
			Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));
			NETADDR Addr;
			Addr.port = 0;
			if(net_addr_from_str(&Addr, aAddrStr) == 0)
				AddFrozenLeaver(Addr, 30); // block for 30 seconds
		}

		if(pChar)
		{
			// Mark as owned by the original client for hook cleanup
			pChar->m_OwnerCID = ClientID;
			pChar->m_IsFrozenNoob = true;
		}

		// Don’t delete player object yet
		return false;
	}

	// Normal disconnect
	AbortVoteKickOnDisconnect(ClientID);

	// Attempt to restore name before sending leave message
	if(pPlayer->m_aOriginalName[0])
	{
		//dbg_msg("hook", "'%s' disconnecting, trying to rename to '%s'", Server()->ClientName(ClientID), pPlayer->m_aOriginalName);
		Server()->SetClientName(ClientID, pPlayer->m_aOriginalName);
	}

	pPlayer->OnDisconnect(pReason);
	delete pPlayer;
	m_apPlayers[ClientID] = nullptr;

	m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;

	return true;
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];
    
	if(!pRawMsg)
	{
		if(m_Config->m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
        {
            if(m_Config->m_SvSpamprotection && pPlayer->m_LastChat &&
                pPlayer->m_LastChat + Server()->TickSpeed() > Server()->Tick())
                return;

            CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;

            // Reset strikes after mute ends
            if(pPlayer->m_ResetStrikesAfterMute && Server()->Tick() >= pPlayer->m_MuteTick)
            {
                pPlayer->m_ToxicStrikes = 2;
                pPlayer->m_ResetStrikesAfterMute = false;
            }

            for(auto it = m_MutedIPs.begin(); it != m_MutedIPs.end(); )
            {
                if(it->second <= Server()->Tick())
                    it = m_MutedIPs.erase(it);
                else
                    ++it;
            }

            char aAddrStr[NETADDR_MAXSTRSIZE];
            Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));
            auto It = m_MutedIPs.find(aAddrStr);
            if(It != m_MutedIPs.end() && It->second > Server()->Tick())
            {
                int Remaining = (It->second - Server()->Tick()) / Server()->TickSpeed();
                char aBuf[128];
                str_format(aBuf, sizeof(aBuf), "You may not chat now, you are muted for %d more second(s)", Remaining);
                SendChatTarget(ClientID, aBuf);
                return;
            }

            // Tournament restriction
            int Team = pMsg->m_Team ? pPlayer->GetTeam() : CHAT_ALL;
            if(m_Config->m_SvTournamentMode)
            {
                if(pPlayer->GetTeam() == TEAM_SPECTATORS && Team == CHAT_ALL && !m_pController->IsGameOver())
                {
                    SendChatTarget(ClientID, "Spectators aren't allowed to write to global chat during a tournament game.");
                    return;
                }
            }

            // Trim and limit message length
            int Length = 0;
            const char *p = pMsg->m_pMessage;
            const char *pEnd = 0;
            while(*p)
            {
                const char *pStrOld = p;
                int Code = str_utf8_decode(&p);

                if(Code > 0x20 && Code != 0xA0 && Code != 0x034F &&
                   (Code < 0x2000 || Code > 0x200F) &&
                   (Code < 0x2028 || Code > 0x202F) &&
                   (Code < 0x205F || Code > 0x2064) &&
                   (Code < 0x206A || Code > 0x206F) &&
                   (Code < 0xFE00 || Code > 0xFE0F) &&
                   Code != 0xFEFF &&
                   (Code < 0xFFF9 || Code > 0xFFFC))
                {
                    pEnd = 0;
                }
                else if(pEnd == 0)
                    pEnd = pStrOld;

                if(++Length >= 256)
                {
                    *(const_cast<char *>(p)) = 0;
                    break;
                }
            }
            if(pEnd != 0)
                *(const_cast<char *>(pEnd)) = 0;

            // Spam throttle
            if(Length == 0 || (m_Config->m_SvSpamprotection &&
                pPlayer->m_LastChat &&
                pPlayer->m_LastChat + Server()->TickSpeed() * ((15+Length)/16) > Server()->Tick()))
                return;

            // Fair spam protection
            if(pPlayer->m_LastChat + Server()->TickSpeed() * ((15+Length)/16 + 1) > Server()->Tick())
            {
                ++pPlayer->m_ChatSpamCount;
                if(++pPlayer->m_ChatSpamCount >= 5)
                {
                    pPlayer->m_LastChat = Server()->Tick() + Server()->TickSpeed() * 2;
                    pPlayer->m_ChatSpamCount = 0;
                }
                else pPlayer->m_LastChat = Server()->Tick();
            }
            else
            {
                pPlayer->m_LastChat = Server()->Tick();
                pPlayer->m_ChatSpamCount = 0;
            }

            // Command handling
            if(pMsg->m_pMessage[0] == '/' && Length > 1)
            {
                ExecuteServerCommand(ClientID, pMsg->m_pMessage + 1);
                return;
            }

            // Send chat message first
            SendChat(ClientID, Team, pMsg->m_pMessage);

            /* Run filter after chat is sent
            char aMsgClean[256];
            str_copy(aMsgClean, pMsg->m_pMessage, sizeof(aMsgClean));
            NormalizeChat(aMsgClean, sizeof(aMsgClean));

            for(int i = 0; i < g_NumSlurs; i++)
            {
                if(str_find(aMsgClean, g_aSlurs[i]))
                {
                    pPlayer->m_ToxicStrikes++;

                    if(pPlayer->m_ToxicStrikes >= 3)
                    {
                        char aAddrStr[NETADDR_MAXSTRSIZE];
                        Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));
                        m_MutedIPs[aAddrStr] = Server()->Tick() + 10 * 60 * Server()->TickSpeed(); // 10 min
                        pPlayer->m_MuteTick = m_MutedIPs[aAddrStr];
                        pPlayer->m_ResetStrikesAfterMute = true;

                        char aMute[128];
                        str_format(aMute, sizeof(aMute), "'%s' has been auto-muted for 10 minutes (toxicity)", Server()->ClientName(ClientID));
                        SendChat(-1, CHAT_ALL, aMute);
                    }
                    else
                    {
                        char aWarn[128];
                        str_format(aWarn, sizeof(aWarn), "⚠️ %s has been flagged for toxicity", Server()->ClientName(ClientID));
                        SendChat(-1, CHAT_ALL, aWarn);
                    }

                    break;
                }
            }*/
        }

		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			if(m_Config->m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			int64 Now = Server()->Tick();
			pPlayer->m_LastVoteTry = Now;

			/* Maybe we could allow spectators to start certain votes, but not all? */
			/* Though I'm not sure what would be the criteria for that */
			//
			// if(pPlayer->GetTeam() == TEAM_SPECTATORS)
			// {
			// 	SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			// 	return;
			// }

			if(m_VoteCloseTime)
			{
				SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
				return;
			}

			int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
			if(pPlayer->m_LastVoteCall && Timeleft > 0)
			{
				char aChatmsg[512] = {0};
				str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote", (Timeleft/Server()->TickSpeed())+1);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}

			char aChatmsg[512] = {0};
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
									pOption->m_aDescription, pReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!m_Config->m_SvVoteKick)
				{
					SendChatTarget(ClientID, "Server does not allow voting to kick players");
					return;
				}

				if(m_Config->m_SvVoteKickMin)
				{
					int PlayerNum = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
						if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
							++PlayerNum;

					if(PlayerNum < m_Config->m_SvVoteKickMin)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players on the server", m_Config->m_SvVoteKickMin);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
				}

				int KickID = str_toint(pMsg->m_Value);
				KickID = pPlayer->GetRealIDFromSnappingClients(KickID);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget(ClientID, "Invalid client id to kick");
					return;
				}
				// if(KickID == ClientID)
				// {
				// 	SendChatTarget(ClientID, "You can't kick yourself");
				// 	return;
				// }
				if(Server()->IsAuthed(KickID))
				{
					SendChatTarget(ClientID, "You can't kick admins");
					char aBufKick[128];
					str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
					SendChatTarget(KickID, aBufKick);
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
				str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
				if (!m_Config->m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, m_Config->m_SvVoteKickBantime);
				}
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!m_Config->m_SvVoteSpectate)
				{
					SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
					return;
				}

				int SpectateID = str_toint(pMsg->m_Value);
				SpectateID = pPlayer->GetRealIDFromSnappingClients(SpectateID);
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget(ClientID, "Invalid client id to move");
					return;
				}
				if(SpectateID == ClientID)
				{
					SendChatTarget(ClientID, "You can't move yourself");
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
				str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, m_Config->m_SvVoteSpectateRejoindelay);
			}

			if(aCmd[0])
			{
				SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
				StartVote(aDesc, aCmd, pReason);
				pPlayer->m_Vote = 1;
				pPlayer->m_VotePos = m_VotePos = 1;
				m_VoteCreator = ClientID;
				pPlayer->m_LastVoteCall = Now;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(!m_VoteCloseTime)
				return;

			if(pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if ((pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsFrozen()) || (pPlayer->GetTeam() == pMsg->m_Team || (m_Config->m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam + Server()->TickSpeed() * 3 > Server()->Tick())))
				return;

			if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				SendBroadcast("Teams are locked", ClientID);
				return;
			}

			if(pPlayer->m_TeamChangeTick > Server()->Tick())
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
				SendBroadcast(aBuf, ClientID);
				return;
			}

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
				{
					pPlayer->m_LastSetTeam = Server()->Tick();
					if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->SetTeam(pMsg->m_Team);
					(void)m_pController->CheckTeamBalance();
					pPlayer->m_TeamChangeTick = Server()->Tick();
				}
				else
					SendBroadcast("Teams must be balanced, please join other team", ClientID);
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-m_Config->m_SvSpectatorSlots);
				SendBroadcast(aBuf, ClientID);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			int RealID = pPlayer->GetRealIDFromSnappingClients(pMsg->m_SpectatorID);

			if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == RealID || ClientID == RealID ||
				(m_Config->m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();

			if(RealID != SPEC_FREEVIEW && (!m_apPlayers[RealID] || m_apPlayers[RealID]->GetTeam() == TEAM_SPECTATORS) && !m_Config->m_SvTournamentMode)
				SendChatTarget(ClientID, "Invalid spectator id used");
			else
				pPlayer->m_SpectatorID = RealID;
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if(m_Config->m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
				return;

			CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set infos
			char aOldName[MAX_NAME_LENGTH];
			str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
			Server()->SetClientName(ClientID, pMsg->m_pName);
			if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
			{
				char aChatText[256];
				str_format(aChatText, sizeof(aChatText), "'%s' changed name to '%s'", aOldName, Server()->ClientName(ClientID));
				SendChat(-1, CGameContext::CHAT_ALL, aChatText);
			}
            str_copy(m_apPlayers[ClientID]->m_aOriginalName, Server()->ClientName(ClientID), sizeof(m_apPlayers[ClientID]->m_aOriginalName));
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(m_Config->m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*m_Config->m_SvEmoticonDelay > Server()->Tick())
				return;
			if(m_Config->m_SvSpamprotection && pPlayer->m_LastEmote == Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();
			++pPlayer->m_Stats.m_NumEmotes;

			SendEmoticon(ClientID, pMsg->m_Emoticon); 
			
			int eicon = EMOTE_NORMAL;
			switch (pMsg->m_Emoticon) {
			case EMOTICON_OOP: eicon = EMOTE_PAIN; break;
			case EMOTICON_EXCLAMATION: eicon = EMOTE_SURPRISE; break;
			case EMOTICON_HEARTS: eicon = EMOTE_HAPPY; break;
			case EMOTICON_DROP: eicon = EMOTE_BLINK; break;
			case EMOTICON_DOTDOT: eicon = EMOTE_BLINK; break;
			case EMOTICON_MUSIC: eicon = EMOTE_HAPPY; break;
			case EMOTICON_SORRY: eicon = EMOTE_PAIN; break;
			case EMOTICON_GHOST: eicon = EMOTE_SURPRISE; break;
			case EMOTICON_SUSHI: eicon = EMOTE_PAIN; break;
			case EMOTICON_SPLATTEE: eicon = EMOTE_ANGRY;  break;
			case EMOTICON_DEVILTEE: eicon = EMOTE_ANGRY; break;
			case EMOTICON_ZOMG: eicon = EMOTE_ANGRY; break;
			case EMOTICON_ZZZ: eicon = EMOTE_BLINK; break;
			case EMOTICON_WTF: eicon = EMOTE_SURPRISE; break;
			case EMOTICON_EYES: eicon = EMOTE_HAPPY; break;
			case EMOTICON_QUESTION: eicon = EMOTE_SURPRISE; break;
			}
			if(pPlayer)
            {
                pPlayer->m_EyeEmote = eicon;
                pPlayer->m_EyeEmoteDuration = Server()->TickSpeed() * 2.0f; // 2 seconds
            }
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if((pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsFrozen()) || (pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*m_Config->m_SvKillDelay > Server()->Tick()) || m_Config->m_SvKillDelay == -1)
				return;

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
		else if (MsgID == NETMSGTYPE_CL_ISDDNET)
		{
			pPlayer->m_ClientVersion = CPlayer::CLIENT_VERSION_DDNET;
			int Version = pUnpacker->GetInt();
			pPlayer->m_DDNetVersion = Version;
			Server()->SetClientVersion(ClientID, Version);
			if (pUnpacker->Error() || Version < 217)
			{
				pPlayer->m_ClientVersion = CPlayer::CLIENT_VERSION_NORMAL;
			}
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReady)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);

			// send vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			CNetMsg_Sv_VoteOptionListAdd OptionMsg;
			int NumOptions = 0;
			OptionMsg.m_pDescription0 = "";
			OptionMsg.m_pDescription1 = "";
			OptionMsg.m_pDescription2 = "";
			OptionMsg.m_pDescription3 = "";
			OptionMsg.m_pDescription4 = "";
			OptionMsg.m_pDescription5 = "";
			OptionMsg.m_pDescription6 = "";
			OptionMsg.m_pDescription7 = "";
			OptionMsg.m_pDescription8 = "";
			OptionMsg.m_pDescription9 = "";
			OptionMsg.m_pDescription10 = "";
			OptionMsg.m_pDescription11 = "";
			OptionMsg.m_pDescription12 = "";
			OptionMsg.m_pDescription13 = "";
			OptionMsg.m_pDescription14 = "";
			CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
			while(pCurrent)
			{
				switch(NumOptions++)
				{
				case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
				case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
				case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
				case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
				case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
				case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
				case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
				case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
				case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
				case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
				case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
				case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
				case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
				case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
				case 14:
					{
						OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
						OptionMsg.m_NumOptions = NumOptions;
						Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
						OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
						NumOptions = 0;
						OptionMsg.m_pDescription1 = "";
						OptionMsg.m_pDescription2 = "";
						OptionMsg.m_pDescription3 = "";
						OptionMsg.m_pDescription4 = "";
						OptionMsg.m_pDescription5 = "";
						OptionMsg.m_pDescription6 = "";
						OptionMsg.m_pDescription7 = "";
						OptionMsg.m_pDescription8 = "";
						OptionMsg.m_pDescription9 = "";
						OptionMsg.m_pDescription10 = "";
						OptionMsg.m_pDescription11 = "";
						OptionMsg.m_pDescription12 = "";
						OptionMsg.m_pDescription13 = "";
						OptionMsg.m_pDescription14 = "";
					}
				}
				pCurrent = pCurrent->m_pNext;
			}
			if(NumOptions > 0)
			{
				OptionMsg.m_NumOptions = NumOptions;
				Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			}

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

sServerCommand* CGameContext::FindCommand(const char* pCmd){
	if(!pCmd) return NULL;
	sServerCommand* pFindCmd = m_FirstServerCommand;
	while(pFindCmd){
		if(str_comp_nocase_whitespace(pFindCmd->m_Cmd, pCmd) == 0) return pFindCmd;
		pFindCmd = pFindCmd->m_NextCommand;
	}
	return NULL;
}

void CGameContext::AddServerCommandSorted(sServerCommand* pCmd){
	if(!m_FirstServerCommand){
		m_FirstServerCommand = pCmd;
	} else {
		sServerCommand* pFindCmd = m_FirstServerCommand;
		sServerCommand* pPrevFindCmd = 0;
		while(pFindCmd){
			if(str_comp_nocase(pCmd->m_Cmd, pFindCmd->m_Cmd) < 0) {
				if(pPrevFindCmd){
					pPrevFindCmd->m_NextCommand = pCmd;
				} else {
					m_FirstServerCommand = pCmd;
				}
				pCmd->m_NextCommand = pFindCmd;
				return;
			}
			pPrevFindCmd = pFindCmd;
			pFindCmd = pFindCmd->m_NextCommand;
		}
		pPrevFindCmd->m_NextCommand = pCmd;
	}
}

void CGameContext::AddServerCommand(const char* pCmd, const char* pDesc, const char* pArgFormat, ServerCommandExecuteFunc pFunc){
	if(!pCmd) return;
	sServerCommand* pFindCmd = FindCommand(pCmd);
	if(!pFindCmd){
		pFindCmd = new sServerCommand(pCmd, pDesc, pArgFormat, pFunc);	
		AddServerCommandSorted(pFindCmd);
	} else {
		pFindCmd->m_Func = pFunc;
		pFindCmd->m_Desc = pDesc;
		pFindCmd->m_ArgFormat = pArgFormat;
	}
}

void CGameContext::ExecuteServerCommand(int pClientID, const char* pLine){
	if(!pLine || !*pLine) return;
	const char* pCmdEnd = pLine;
	while(*pCmdEnd && !is_whitespace(*pCmdEnd)) ++pCmdEnd;
	
	const char* pArgs = (*pCmdEnd) ? pCmdEnd + 1 : 0;
	
	sServerCommand* pCmd = FindCommand(pLine);
	if(pCmd) pCmd->ExecuteCommand(this, pClientID, pArgs);
	else SendChatTarget(pClientID, "Server command not found");
}

void CGameContext::CmdStats(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	CPlayer* pReq = pContext->m_apPlayers[pClientID];

	// Cooldown check
	if(!pContext->CheckStatCommandCooldown(pClientID))
        return;

	// Determine who we’re checking
	const char* pRawName = ArgNum > 0 ? pArgs[0] : pContext->Server()->ClientName(pClientID);

    // Strip quotes from name if present
    static char aStrippedName[MAX_NAME_LENGTH];
    str_copy(aStrippedName, pRawName, sizeof(aStrippedName));
    int len = str_length(aStrippedName);
    if(len >= 2 && aStrippedName[0] == '"' && aStrippedName[len-1] == '"')
    {
        aStrippedName[len-1] = 0;
        mem_move(aStrippedName, aStrippedName+1, len-1); // shift left to remove leading quote
    }
    const char* pTargetName = aStrippedName;
	int TargetID = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pContext->m_apPlayers[i]) continue;
		if(str_comp(pContext->Server()->ClientName(i), pTargetName) == 0)
		{
			TargetID = i;
			break;
		}
	}

	CPlayer* pTarget = nullptr;
	CPlayer Tmp(pContext, -1, TEAM_SPECTATORS);
	str_copy(Tmp.m_aSavedName, pTargetName, sizeof(Tmp.m_aSavedName));

	if(TargetID != -1)
	{
		pTarget = pContext->m_apPlayers[TargetID];
	}
	else if(pContext->LoadRoundStatsByName(pTargetName, &Tmp))
	{
		pTarget = &Tmp;
	}
	else
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "No round stats found for '%s'", pTargetName);
		pContext->SendChatTarget(pClientID, aBuf);
		return;
	}

	const char* pName = pTargetName;
	const char* pRequester = pContext->Server()->ClientName(pClientID);
	char aBuf[256];

	// Global broadcast
	str_format(aBuf, sizeof(aBuf), "round stats for '%s' (req. by '%s')", pName, pRequester);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	float Accuracy = pTarget->m_RoundShots > 0 ? ((float)pTarget->m_RoundFreezes / pTarget->m_RoundShots) * 100.0f : 0.0f;
	float KDRatio = (float)pTarget->m_RoundKills / (pTarget->m_RoundDeaths > 0 ? pTarget->m_RoundDeaths : 1);

	str_format(aBuf, sizeof(aBuf), "- k/d: %d/%d = %.3f | accuracy: %.3f%%",
		pTarget->m_RoundKills, pTarget->m_RoundDeaths, KDRatio, Accuracy);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- net steals: %d - %d = %d | wrong kills: %d",
		pTarget->m_RoundSteals, pTarget->m_RoundStolenFrom, pTarget->m_RoundSteals - pTarget->m_RoundStolenFrom,
		pTarget->m_RoundWrongShrineKills);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- shots: %d | wallshots: %d | saves: %d/%d",
		pTarget->m_RoundShots, pTarget->m_RoundWallshots, pTarget->m_RoundSaves, pTarget->m_RoundSavedBy);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- max spree: %d | max multi: %d",
		pTarget->m_RoundMaxSpree, pTarget->m_RoundMaxMulti);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- green kills: %d | gold kills: %d | purple kills: %d",
		pTarget->m_RoundGreenSpikeKills, pTarget->m_RoundGoldSpikeKills, pTarget->m_RoundPurpleSpikeKills);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	int sec = pTarget->m_RoundTimeFrozen;
	int h = sec / 3600;
	int m = (sec % 3600) / 60;
	int s = sec % 60;

	str_format(aBuf, sizeof(aBuf), "- freezes: %d/%d | time frozen: %dh %dm %ds",
		pTarget->m_RoundFreezes, pTarget->m_RoundFrozen, h, m, s);
	pContext->SendChat(-1, CHAT_ALL, aBuf);
}

void CGameContext::CmdStatsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	CPlayer* pReq = pContext->m_apPlayers[pClientID];

	// Cooldown check
	if(!pContext->CheckStatCommandCooldown(pClientID))
        return;

	const char* pRawName = ArgNum > 0 ? pArgs[0] : pContext->Server()->ClientName(pClientID);

    // Strip quotes from name if present
    static char aStrippedName[MAX_NAME_LENGTH];
    str_copy(aStrippedName, pRawName, sizeof(aStrippedName));
    int len = str_length(aStrippedName);
    if(len >= 2 && aStrippedName[0] == '"' && aStrippedName[len-1] == '"')
    {
        aStrippedName[len-1] = 0;
        mem_move(aStrippedName, aStrippedName+1, len-1); // shift left to remove leading quote
    }
    const char* pTargetName = aStrippedName;
	int TargetID = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pContext->m_apPlayers[i]) continue;
		if(str_comp(pContext->Server()->ClientName(i), pTargetName) == 0)
		{
			TargetID = i;
			break;
		}
	}

	CPlayer* pTarget = nullptr;
	CPlayer Tmp(pContext, -1, TEAM_SPECTATORS);
	str_copy(Tmp.m_aSavedName, pTargetName, sizeof(Tmp.m_aSavedName));

	if(TargetID != -1)
	{
		pTarget = pContext->m_apPlayers[TargetID];
	}
	else if(pContext->LoadTotalStatsByName(pTargetName, &Tmp))
	{
		pTarget = &Tmp;
	}
	else
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "No saved stats found for '%s'", pTargetName);
		pContext->SendChatTarget(pClientID, aBuf);
		return;
	}

	const char* pName = pTargetName;
	const char* pRequester = pContext->Server()->ClientName(pClientID);
	char aBuf[256];

	str_format(aBuf, sizeof(aBuf), "total stats for '%s' (req. by '%s')", pName, pRequester);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	float Accuracy = pTarget->m_Shots > 0 ? ((float)pTarget->m_Freezes / pTarget->m_Shots) * 100.0f : 0.0f;
	float KDRatio = (float)pTarget->m_Kills / (pTarget->m_Deaths > 0 ? pTarget->m_Deaths : 1);

	str_format(aBuf, sizeof(aBuf), "- k/d: %d/%d = %.3f | accuracy: %.3f%%",
		pTarget->m_Kills, pTarget->m_Deaths, KDRatio, Accuracy);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- net steals: %d - %d = %d | wrong kills: %d",
		pTarget->m_Steals, pTarget->m_StolenFrom, pTarget->m_Steals - pTarget->m_StolenFrom,
		pTarget->m_WrongShrineKills);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- shots: %d | wallshots: %d | saves: %d/%d",
		pTarget->m_Shots, pTarget->m_Wallshots, pTarget->m_Saves, pTarget->m_SavedBy);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- max spree: %d | max multi: %d",
		pTarget->m_MaxSpree, pTarget->m_MaxMulti);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "- green kills: %d | gold kills: %d | purple kills: %d",
		pTarget->m_GreenSpikeKills, pTarget->m_GoldSpikeKills, pTarget->m_PurpleSpikeKills);
	pContext->SendChat(-1, CHAT_ALL, aBuf);

	int sec = pTarget->m_TimeFrozen;
	int h = sec / 3600;
	int m = (sec % 3600) / 60;
	int s = sec % 60;

	str_format(aBuf, sizeof(aBuf), "- freezes: %d/%d | time frozen: %dh %dm %ds",
		pTarget->m_Freezes, pTarget->m_Frozen, h, m, s);
	pContext->SendChat(-1, CHAT_ALL, aBuf);
}
#define minimum(a, b) ((a) < (b) ? (a) : (b))
void CGameContext::CmdWhisper(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum){
	CPlayer* p = pContext->m_apPlayers[pClientID];
	if (!p || ArgNum < 1)
	{
		pContext->SendChatTarget(pClientID, "[/whisper] usage: /w <playername> <text>");
		return;
	}

	const char* pInput = *pArgs;
	const char* pStart = pInput;
	const char* pNameEnd = 0;
	bool Quoted = false;

	// Handle quoted name
	if(*pStart == '"')
	{
		Quoted = true;
		pStart++;
		const char* pEndQuote = str_find(pStart, "\"");
		if(!pEndQuote)
		{
			pContext->SendChatTarget(pClientID, "*** Missing closing quote.");
			return;
		}
		pNameEnd = pEndQuote;
	}
	else
	{
		// Unquoted name: go until space
		const char* pSpace = str_find(pStart, " ");
		if(!pSpace)
		{
			pContext->SendChatTarget(pClientID, "Player not found or message missing.");
			return;
		}
		pNameEnd = pSpace;
	}

	// Extract name substring
	int NameLen = minimum((int)(pNameEnd - pStart), MAX_NAME_LENGTH - 1);
	char aTargetName[MAX_NAME_LENGTH];
	str_copy(aTargetName, pStart, NameLen + 1);

	// Reconstruct name pointer into full string
	const char* pRestMessage = Quoted ? str_find(pNameEnd, " ") : pNameEnd;
	if(!pRestMessage)
	{
		pContext->SendChatTarget(pClientID, "No whisper text written.");
		return;
	}
	while(*pRestMessage == ' ')
		pRestMessage++;

	// Match best player by name prefix
	int max = 0;
	int maxPlayerID = -1;
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (pContext->m_apPlayers[i]) {
			const char* name = pContext->Server()->ClientName(i);
			const char* arg_names = aTargetName;

			int count = 0;
			while (*name && *arg_names) {
				if (*name != *arg_names) break;
				++count;
				++name; ++arg_names;
			}

			if (*name == 0 && max < count) {
				max = count;
				maxPlayerID = i;
			}
		}
	}

	if (maxPlayerID == -1) {
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Player '%s' not found.", aTargetName);
	pContext->SendChatTarget(pClientID, aBuf);
	return;
    }

	char buff[257];
	if(pContext->m_apPlayers[maxPlayerID]->m_ClientVersion != CPlayer::CLIENT_VERSION_DDNET) {
		str_format(buff, sizeof(buff), "[← %s] %s", pContext->Server()->ClientName(pClientID), pRestMessage);
		pContext->SendChatTarget(maxPlayerID, buff);
	}
	else {
		pContext->SendChat(pClientID, CHAT_WHISPER_RECV, pRestMessage, maxPlayerID);
	}

	if(p->m_ClientVersion != CPlayer::CLIENT_VERSION_DDNET) {
		str_format(buff, sizeof(buff), "[→ %s] %s", pContext->Server()->ClientName(maxPlayerID), pRestMessage);
		pContext->SendChatTarget(pClientID, buff);
	}
	else {
		pContext->SendChat(maxPlayerID, CHAT_WHISPER_SEND, pRestMessage, pClientID);
	}

	p->m_WhisperPlayer.PlayerID = maxPlayerID;
	str_copy(p->m_WhisperPlayer.PlayerName, pContext->Server()->ClientName(maxPlayerID), sizeof(p->m_WhisperPlayer.PlayerName));
}


void CGameContext::CmdConversation(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum){
	CPlayer* p = pContext->m_apPlayers[pClientID];
	
	if (ArgNum >= 1) {
		if (p->m_WhisperPlayer.PlayerID != -1) {
			if (strcmp(pContext->Server()->ClientName(p->m_WhisperPlayer.PlayerID), p->m_WhisperPlayer.PlayerName) == 0) {
				char buff[257];
				if(pContext->m_apPlayers[p->m_WhisperPlayer.PlayerID]->m_ClientVersion != CPlayer::CLIENT_VERSION_DDNET) {
					str_format(buff, 256, "[← %s] %s", pContext->Server()->ClientName(pClientID), (*pArgs));
					pContext->SendChatTarget(p->m_WhisperPlayer.PlayerID, buff);
				}
				else {
					pContext->SendChat(pClientID, CHAT_WHISPER_RECV, (*pArgs), p->m_WhisperPlayer.PlayerID);
				}

				if(p->m_ClientVersion != CPlayer::CLIENT_VERSION_DDNET) {
					str_format(buff, 256, "[→ %s] %s", pContext->Server()->ClientName(p->m_WhisperPlayer.PlayerID), (*pArgs));
					pContext->SendChatTarget(pClientID, buff);
				}
				else {
					pContext->SendChat(p->m_WhisperPlayer.PlayerID, CHAT_WHISPER_SEND, (*pArgs), pClientID);
				}
			}
			else pContext->SendChatTarget(pClientID, "Player left the game or renamed.");
		}
		else pContext->SendChatTarget(pClientID, "No player whispered to yet.");
	}
	else pContext->SendChatTarget(pClientID, "[/conversation] usage: /c <text>, after you already whispered to a player");
}

void CGameContext::CmdHelp(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{	
	if(ArgNum > 0)
	{
		sServerCommand* pCmd = pContext->FindCommand(pArgs[0]);
		if(pCmd)
		{
			char buff[200];
			str_format(buff, sizeof(buff), "[/%s] %s", pCmd->m_Cmd, pCmd->m_Desc);
			pContext->SendChatTarget(pClientID, buff);
		}
		else
		{
			char buff[200];
			str_format(buff, sizeof(buff), "Unknown command: '%s'. Use /cmdlist to view all commands.", pArgs[0]);
			pContext->SendChatTarget(pClientID, buff);
		}
	}
	else
	{
		pContext->SendChatTarget(pClientID, "Usage: /help <command>");
		pContext->SendChatTarget(pClientID, "Use /cmdlist to view all commands");
	}
}

void CGameContext::CmdCmdList(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	char Line[256];
	str_copy(Line, "", sizeof(Line));
	int LineLen = str_length(Line);
	bool FirstInLine = true;

	sServerCommand* pCmd = pContext->m_FirstServerCommand;

	while(pCmd)
	{
		const char* Cmd = pCmd->m_Cmd;
		char aCmdBuf[64];

		// Format with or without comma based on position in the current line
		str_format(aCmdBuf, sizeof(aCmdBuf), "%s/%s", FirstInLine ? " " : ", ", Cmd);
		int CmdLen = str_length(aCmdBuf);

		// If it would overflow, send the line and start new one
		if(LineLen + CmdLen >= 239)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "/%s", Cmd); // New line starts clean, no comma
			LineLen = str_length(Line);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aCmdBuf, sizeof(Line));
			LineLen += CmdLen;
			FirstInLine = false;
		}

		pCmd = pCmd->m_NextCommand;
	}

	// Send the last line
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdPause(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{

}

void CGameContext::CmdEmote(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum){
	CPlayer* pPlayer = pContext->m_apPlayers[pClientID];
	
	if (ArgNum > 0) {
		if (!str_comp_nocase_whitespace(pArgs[0], "angry"))
				pPlayer->m_Emotion = EMOTE_ANGRY;
		else if (!str_comp_nocase_whitespace(pArgs[0], "blink"))
			pPlayer->m_Emotion = EMOTE_BLINK;
		else if (!str_comp_nocase_whitespace(pArgs[0], "close"))
			pPlayer->m_Emotion = EMOTE_BLINK;
		else if (!str_comp_nocase_whitespace(pArgs[0], "happy"))
			pPlayer->m_Emotion = EMOTE_HAPPY;
		else if (!str_comp_nocase_whitespace(pArgs[0], "pain"))
			pPlayer->m_Emotion = EMOTE_PAIN;
		else if (!str_comp_nocase_whitespace(pArgs[0], "surprise"))
			pPlayer->m_Emotion = EMOTE_SURPRISE;
		else if (!str_comp_nocase_whitespace(pArgs[0], "normal"))
			pPlayer->m_Emotion = EMOTE_NORMAL;
		else
			pContext->SendChatTarget(pClientID, "Unknown emote... Say /emote");
		
		int Duration = pContext->Server()->TickSpeed();
		if(ArgNum > 1) Duration = str_toint(pArgs[1]);
		
		pPlayer->m_EmotionDuration = Duration * pContext->Server()->TickSpeed();
	} else {
		//ddrace like
		pContext->SendChatTarget(pClientID, "Emote commands are: /emote surprise /emote blink /emote close /emote angry /emote happy /emote pain");
		pContext->SendChatTarget(pClientID, "Example: /emote surprise 10 for 10 seconds or /emote surprise (default 1 second)");
	}
}

void CGameContext::CmdEmoteEyes(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	if(!pContext->m_apPlayers[pClientID])
		return;

	CPlayer* pPlayer = pContext->m_apPlayers[pClientID];

	if(ArgNum < 1)
	{
		pContext->SendChatTarget(pClientID, "Usage: /emoteeyes <normal|happy|pain|surprise|angry|blink> [duration]");
		return;
	}

	int Emote = EMOTE_NORMAL;

	if(!str_comp_nocase(pArgs[0], "angry") || !str_comp_nocase(pArgs[0], "angry 999999"))
		Emote = EMOTE_ANGRY;
	else if(!str_comp_nocase(pArgs[0], "blink") || !str_comp_nocase(pArgs[0], "blink 999999"))
		Emote = EMOTE_BLINK;
	else if(!str_comp_nocase(pArgs[0], "happy") || !str_comp_nocase(pArgs[0], "happy 999999"))
		Emote = EMOTE_HAPPY;
	else if(!str_comp_nocase(pArgs[0], "pain") || !str_comp_nocase(pArgs[0], "pain 999999"))
		Emote = EMOTE_PAIN;
	else if(!str_comp_nocase(pArgs[0], "surprise") || !str_comp_nocase(pArgs[0], "surprise 999999"))
		Emote = EMOTE_SURPRISE;
	else if(!str_comp_nocase(pArgs[0], "normal") || !str_comp_nocase(pArgs[0], "normal 999999"))
		Emote = EMOTE_NORMAL;
	else
	{
		pContext->SendChatTarget(pClientID, "Unknown emote type. Options: normal, happy, pain, surprise, angry, blink");
		return;
	}

	// Default to -1 (permanent)
	int Duration = -1;

	if(ArgNum >= 2)
	{
		int Seconds = clamp(str_toint(pArgs[1]), 1, 999999);
		Duration = Seconds * pContext->Server()->TickSpeed();

		if(Seconds >= 999999)
		{
			pPlayer->m_DefaultEyeEmote = Emote;
		}
	}
	else
	{
		pPlayer->m_DefaultEyeEmote = Emote;
	}

	// Apply eye emote
	pPlayer->m_EyeEmote = Emote;
	pPlayer->m_EyeEmoteDuration = Duration;

	if(pPlayer->GetCharacter())
	{
		int Until = (Duration > 0) ? (pContext->Server()->Tick() + Duration) : -1;
		pPlayer->GetCharacter()->SetEmote(Emote, Until);
	}
}

void CGameContext::SetEyeEmoteDefault(int ClientID)
{
	if(!m_apPlayers[ClientID])
		return;

	CPlayer* pPlayer = m_apPlayers[ClientID];
	const char* aEmoteNames[] = { "normal", "happy", "pain", "surprise", "angry", "blink" };
	int Default = clamp(pPlayer->m_DefaultEyeEmote, 0, (int)(sizeof(aEmoteNames)/sizeof(aEmoteNames[0])) - 1);

	const char* aArgs[] = { aEmoteNames[Default], "999999" };
	CmdEmoteEyes(this, ClientID, aArgs, 2);
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->TogglePause();
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = pSelf->m_pController->ClampTeam(pResult->GetInteger(1));
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = pSelf->m_pController->ClampTeam(pResult->GetInteger(0));

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;
	
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");
	
	pSelf->m_pController->ShuffleTeams();
	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s', because the command string exceeds the maximum of %d", pCommand, (int)VOTE_CMD_LENGTH);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s', because the description exceeds the maximum of %d", pDescription, (int)VOTE_DESC_LENGTH);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dublicate vote option for '%s'", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			//pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!pSelf->m_Config->m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, pSelf->m_Config->m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, pSelf->m_Config->m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = pSelf->m_Config->m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

    //added by Scooter5561
    Console()->Register("toggle_dyncam", "i", CFGFLAG_SERVER, CGameContext::ConToggleDyncam, this, "Enable or disable dynamic camera draw distances");
    Console()->Register("mute", "i", CFGFLAG_SERVER, ConMute, this, "Mute a player from sending chat messages");
    Console()->Register("unmute", "i", CFGFLAG_SERVER, ConUnmute, this, "Unmute a player by ID");

	// Added by Pig-Eye
	Console()->Register("gamemode", "?s", CFGFLAG_SERVER, ConChangeGamemode, this, "Change the gamemode");
	Console()->Register("makesay", "ir", CFGFLAG_SERVER, ConMakeSay, this, "Force an unassuming client to say something probably bad");

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
    dbg_msg("debug","server starting");
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
    dbg_msg("debug","pServer and pConsole started");

	AddServerCommand("stats", "shows your round stats or another player's", "[name]", CmdStats);
	AddServerCommand("s", "shows your round stats or another player's", "[name]", CmdStats);
    
    //custom
    AddServerCommand("useless", "Gives you the useless gun", "", CmdUseless);
    AddServerCommand("help", "Shows more information to any command", "<command>", CmdHelp);
    AddServerCommand("conversation", "Whisper to the player, you whispered to last", "<text>", CmdConversation);
	AddServerCommand("c", "Whisper to the player, you whispered to last", "<text>", CmdConversation);
    AddServerCommand("whisper", "Whisper to a player in the server privately", "<playername> <text>", CmdWhisper);
	AddServerCommand("w", "Whisper to a player in the server privately", "<playername> <text>", CmdWhisper);
    AddServerCommand("cmdlist", "Shows the list of all commands", 0, CmdCmdList);
    AddServerCommand("pause", "Just here so pressing your pause key doesnt say command not found", 0, CmdPause);
    AddServerCommand("spec", "Just here so pressing your spec key doesnt say command not found", 0, CmdPause);
    AddServerCommand("statsall", "Shows all-time total stats for you or another player", "[name]", CmdStatsAll);
    AddServerCommand("statsa", "Shows all-time total stats for you or another player", "[name]", CmdStatsAll);
    AddServerCommand("fewsteals", "Show player with lowest steals this round", "", CmdFewSteals);
    AddServerCommand("fewstealsa", "Show top 20 players with lowest steals (all-time)", "", CmdFewStealsAll);
    AddServerCommand("topsteals", "Players with most steals this round", "", CmdTopSteals);
    AddServerCommand("topstealsa", "Top 20 players with most steals", "", CmdTopStealsAll);
    AddServerCommand("topwalls", "Players with most wallshots this round", "", CmdTopWalls);
    AddServerCommand("topwallsa", "Top 20 players with most wallshots", "", CmdTopWallsAll);
    AddServerCommand("topwrong", "Players with most wrong kills this round", "", CmdTopWrong);
    AddServerCommand("topwronga", "Top 20 players with most wrong kills", "", CmdTopWrongAll);
    AddServerCommand("topkills", "Players with most kills this round", "", CmdTopKills);
    AddServerCommand("topkillsa", "Top 20 players with most kills", "", CmdTopKillsAll);
    AddServerCommand("topaccuracy", "Players with best accuracy this round (1000+ shots)", "", CmdTopAccuracy);
    AddServerCommand("topaccuracya", "Top 20 players with best accuracy", "", CmdTopAccuracyAll);
    AddServerCommand("topacc", "Players with best accuracy this round", "", CmdTopAccuracy);
    AddServerCommand("topacca", "Top 20 players with best accuracy", "", CmdTopAccuracyAll);
    AddServerCommand("topfreeze", "Players with most freezes this round", "", CmdTopFreeze);
    AddServerCommand("topfreezea", "Top 20 players with most freezes", "", CmdTopFreezeAll);
    AddServerCommand("topgreen", "Top 20 players with most green kills", "", CmdTopGreen);
    AddServerCommand("topgold", "Top 20 players with most gold kills", "", CmdTopGold);
    AddServerCommand("toppurple", "Top 20 players with most purple kills", "", CmdTopPurple);
    AddServerCommand("topspree", "Top 20 players with highest spree", "", CmdTopSpree);
    AddServerCommand("topmulti", "Top 20 players with highest multi", "", CmdTopMulti);
    AddServerCommand("topsaves", "Players with most saves this round", "", CmdTopSaves);
    AddServerCommand("topsavesa", "Top 20 players with most saves", "", CmdTopSavesAll);
    AddServerCommand("tophammer", "Players with most saves this round", "", CmdTopSaves);
    AddServerCommand("tophammera", "Top 20 players with most saves", "", CmdTopSavesAll);
    AddServerCommand("topfrozen", "Players with longest time frozen this round", "", CmdTopFrozen);
    AddServerCommand("topfrozena", "Top 20 players with longest time frozen", "", CmdTopFrozenAll);
    AddServerCommand("topkd", "Players with highest K/D this round", "", CmdTopKD);
    AddServerCommand("topkda", "Top 20 players with highest K/D", "", CmdTopKDAll);
    AddServerCommand("topstats", "Top 1 players for each stat", "", CmdTopStats);
    AddServerCommand("emoteeyes", "Change your eye emote", "<normal|happy|pain|surprise|angry|blink> [duration]", CmdEmoteEyes);
    AddServerCommand("emote", "Change your eye emote", "<normal|happy|pain|surprise|angry|blink> [duration]", CmdEmoteEyes);
    AddServerCommand("earrape", "funny little command", "", CmdEarrape);
    
	// ... rest of AddServerCommand calls ...

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// select gametype
	bool found = false;
	for (auto& type : fng_gametypes) {
		if (str_comp(m_Config->m_SvGametype, type.name) == 0) {
			m_pController = type.s_constructor(this);
			found = true;
		}
	}
	if (!found) {
#define CONTEXT_INIT_WITHOUT_CONFIG
#include "gamecontext_additional_gametypes.h"
#undef CONTEXT_INIT_WITHOUT_CONFIG
		m_pController = new CGameControllerFNG2(this);
	}

	// create entities from map
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;
			if(Index >= 128)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

#ifdef CONF_DEBUG
	if(m_Config->m_DbgDummies)
	{
		for(int i = 0; i < m_Config->m_DbgDummies; i++)
			OnClientConnected(MAX_CLIENTS - i - 1);
	}
#endif
}


void CGameContext::OnInit(IKernel *pKernel, IMap* pMap, CConfiguration* pConfigFile)
{
	IKernel *kernel = NULL;
	if(pKernel != NULL) kernel = pKernel;
	else kernel = Kernel();
	
	if(Kernel() == NULL) SetKernel(kernel);
	
	m_pServer = kernel->RequestInterface<IServer>();
	m_pConsole = kernel->RequestInterface<IConsole>();

	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	
	AddServerCommand("stats", "shows your round stats or another player's", "[name]", CmdStats);
	AddServerCommand("s", "shows your round stats or another player's", "[name]", CmdStats);
	AddServerCommand("whisper", "whisper to a player in the server privately", "<playername> <text>", CmdWhisper);
	AddServerCommand("w", "whisper to a player in the server privately", "<playername> <text>", CmdWhisper);
	AddServerCommand("conversation", "whisper to the player, you whispered to last", "<text>", CmdConversation);
	AddServerCommand("c", "whisper to the player, you whispered to last", "<text>", CmdConversation);
	AddServerCommand("help", "shows more information to any command", "<command>", CmdHelp);
	AddServerCommand("cmdlist", "show the cmd list", 0, CmdCmdList);
	if(m_Config->m_SvEmoteWheel || m_Config->m_SvEmotionalTees) AddServerCommand("emote", "enable custom emotes", "<emote type> <time in seconds>", CmdEmote);

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(kernel, pMap);
	m_Collision.Init(&m_Layers);

	
	CConfiguration* pConfig;
	CConfiguration Config;
	if(pConfigFile) {
		pConfig = pConfigFile;
		m_Config = pConfig;
	} else {
		pConfig = &Config;
	}

	// select gametype
	bool found = false;
	for (auto& type : fng_gametypes) {
		if (str_comp(m_Config->m_SvGametype, type.name) == 0) {
			m_pController = type.c_constructor(this, *pConfig);
			found = true;
		}
	}
	if (!found) {
#define CONTEXT_INIT_WITHOUT_CONFIG
#include "gamecontext_additional_gametypes.h"
#undef CONTEXT_INIT_WITHOUT_CONFIG
		m_pController = new CGameControllerFNG2(this);
	}
	
	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)pMap->GetData(pTileMap->m_Data);

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index >= 128)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

#ifdef CONF_DEBUG
	if(m_Config->m_DbgDummies)
	{
		for(int i = 0; i < m_Config->m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = nullptr;
	Clear();
}

int CGameContext::PreferedTeamPlayer(int ClientID) {
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_apPlayers[ClientID])
		return m_apPlayers[ClientID]->GetTeam();

	return -2;
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	if (ClientID > -1 && m_apPlayers[ClientID]) {
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
				m_apPlayers[i]->Snap(ClientID);
		}

		if(m_apPlayers[ClientID]->m_ClientVersion != CPlayer::CLIENT_VERSION_DDNET)
			m_apPlayers[ClientID]->FakeSnap();
		else 
			m_apPlayers[ClientID]->FakeSnap(CPlayer::DDNET_CLIENT_MAX_CLIENTS - 1);
	}
	else if (ClientID == -1) {
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
				m_apPlayers[i]->Snap(ClientID);
		}
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && (m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true);
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return m_Config->m_SvEmoteWheel ? GAME_VERSION_PLUS : GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }


void CGameContext::SendRoundStats()
{
	struct Entry
	{
		char Name[MAX_NAME_LENGTH];
		int Kills, Deaths, Shots, Wallshots, Freezes, Saves;
		int Green, Gold, Purple, Wrong, Steals, StolenFrom;
		int Spree, Multi, TimeFrozen;
	};

	std::vector<Entry> List;
	std::set<std::string> Seen;

	// In-game players first
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = m_apPlayers[i];
		if(!p || p->GetTeam() == TEAM_SPECTATORS)
			continue;

		Entry e;
		str_copy(e.Name, Server()->ClientName(i), sizeof(e.Name));
		Seen.insert(e.Name);
		e.Kills = p->m_RoundKills;
		e.Deaths = p->m_RoundDeaths;
		e.Shots = p->m_RoundShots;
		e.Wallshots = p->m_RoundWallshots;
		e.Freezes = p->m_RoundFreezes;
		e.Saves = p->m_RoundSaves;
		e.Green = p->m_RoundGreenSpikeKills;
		e.Gold = p->m_RoundGoldSpikeKills;
		e.Purple = p->m_RoundPurpleSpikeKills;
		e.Wrong = p->m_RoundWrongShrineKills;
		e.Steals = p->m_RoundSteals;
		e.StolenFrom = p->m_RoundStolenFrom;
		e.Spree = p->m_RoundMaxSpree;
		e.Multi = p->m_RoundMaxMulti;
		e.TimeFrozen = p->m_RoundTimeFrozen;
		List.push_back(e);
	}

	// Load additional players from roundstats.db if not seen already
	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;
		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int D[18];

				if(sscanf(aBuf, "%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7], &D[8],
					&D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
				{
					if(Seen.find(aName) == Seen.end())
					{
						Entry e;
						str_copy(e.Name, aName, sizeof(e.Name));
						e.Kills = D[0];
						e.Deaths = D[1];
						e.Shots = D[2];
						e.Wallshots = D[4];
						e.Freezes = D[5];
						e.Saves = D[8];
						e.Green = D[10];
						e.Gold = D[11];
						e.Purple = D[12];
						e.Wrong = D[13];
						e.Steals = D[14];
						e.StolenFrom = D[15];
						e.Spree = D[16];
						e.Multi = D[17];
						e.TimeFrozen = D[7];
						List.push_back(e);
					}
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	// Helpers
	auto PrintTop = [&](const char *Label, int (*Getter)(const Entry&), bool AllowNegative = false)
    {
        int MaxVal = AllowNegative ? INT_MIN : 1;
        std::vector<const Entry*> Top;

        for(const auto &e : List)
        {
            int val = Getter(e);
            if(val == 0 || (!AllowNegative && val < 0))
                continue;

            if(val > MaxVal)
            {
                MaxVal = val;
                Top.clear();
                Top.push_back(&e);
            }
            else if(val == MaxVal)
                Top.push_back(&e);
        }

        char Line[256];
        if(Top.empty())
        {
            str_format(Line, sizeof(Line), "- %s: 0 (none)", Label);
            SendChat(-1, CHAT_ALL, Line);
            return;
        }
        str_format(Line, sizeof(Line), "- %s: %d (%s", Label, Getter(*Top[0]), Top[0]->Name);
        for(size_t i = 1; i < Top.size(); ++i)
        {
            str_append(Line, ", ", sizeof(Line));
            str_append(Line, Top[i]->Name, sizeof(Line));
        }
        str_append(Line, ")", sizeof(Line));
        SendChat(-1, CHAT_ALL, Line);
    };

	PrintTop("most net steals", [](const Entry &e) { return e.Steals - e.StolenFrom; }, true);
	PrintTop("best spree", [](const Entry &e) { return e.Spree; });
	PrintTop("best multi", [](const Entry &e) { return e.Multi; });

	// Best K/D
	float BestKD = 0.f;
	std::vector<const Entry*> TopKD;
	for(const auto &e : List)
	{
		if(e.Kills == 0)
			continue;
		float kd = (float)e.Kills / (e.Deaths > 0 ? e.Deaths : 1);
		if(kd > BestKD)
		{
			BestKD = kd;
			TopKD.clear();
			TopKD.push_back(&e);
		}
		else if(kd == BestKD)
			TopKD.push_back(&e);
	}
	if(!TopKD.empty())
	{
		char Line[256];
		str_format(Line, sizeof(Line), "- best k/d: %.3f (%s", BestKD, TopKD[0]->Name);
		for(size_t i = 1; i < TopKD.size(); ++i)
		{
			str_append(Line, ", ", sizeof(Line));
			str_append(Line, TopKD[i]->Name, sizeof(Line));
		}
		str_append(Line, ")", sizeof(Line));
		SendChat(-1, CHAT_ALL, Line);
	}
	else
	{
		SendChat(-1, CHAT_ALL, "- best k/d: 0 (none)");
	}

	PrintTop("most wallshots", [](const Entry &e) { return e.Wallshots; });
	PrintTop("most kills", [](const Entry &e) { return e.Kills; });

	// Accuracy
	std::vector<const Entry*> TopAcc;
	for(const auto &e : List)
		if(e.Shots >= 10 && e.Freezes > 0)
			TopAcc.push_back(&e);

	std::sort(TopAcc.begin(), TopAcc.end(), [](const Entry *a, const Entry *b) {
		return (float)a->Freezes / a->Shots > (float)b->Freezes / b->Shots;
	});

	if(!TopAcc.empty())
	{
		char Line[256];
		for(size_t i = 0; i < TopAcc.size() && i < 4; ++i)
		{
			float acc = (float)TopAcc[i]->Freezes / TopAcc[i]->Shots * 100.f;
			if(i == 0)
				str_format(Line, sizeof(Line), "- best accuracy: %.3f%% (%s (%d freezes))", acc, TopAcc[i]->Name, TopAcc[i]->Freezes);
			else
				str_format(Line, sizeof(Line), "- %.3f%% (%s (%d freezes))", acc, TopAcc[i]->Name, TopAcc[i]->Freezes);
			SendChat(-1, CHAT_ALL, Line);
		}
	}
	else
	{
		SendChat(-1, CHAT_ALL, "- best accuracy: 0 (none)");
	}

	// Longest frozen
	const Entry* Frozen = nullptr;
	for(const auto &e : List)
	{
		if(!Frozen || e.TimeFrozen > Frozen->TimeFrozen)
			Frozen = &e;
	}
	if(Frozen && Frozen->TimeFrozen > 0)
	{
		char Line[128];
		str_format(Line, sizeof(Line), "- longest frozen: %dm %ds (%s)", Frozen->TimeFrozen / 60, Frozen->TimeFrozen % 60, Frozen->Name);
		SendChat(-1, CHAT_ALL, Line);
	}
	else
	{
		SendChat(-1, CHAT_ALL, "- longest frozen: 0 (none)");
	}
}

void CGameContext::SendRandomTrivia(){	
	srand (time(NULL));
	int r = rand()%8;
	
	bool TriviaSent = false;
	
	//most jumps
	if(r == 0){
		int MaxJumps = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxJumps < p->m_Stats.m_NumJumped){
				MaxJumps = p->m_Stats.m_NumJumped;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' jumped %d time%s in this round.", Server()->ClientName(PlayerID), MaxJumps, (MaxJumps == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//longest travel distance
	else if(r == 1){
		float MaxTilesMoved = 0.f;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxTilesMoved < p->m_Stats.m_NumTilesMoved){
				MaxTilesMoved = p->m_Stats.m_NumTilesMoved;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' moved %5.2f tiles in this round.", Server()->ClientName(PlayerID), MaxTilesMoved/32.f);
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//most hooks
	else if(r == 2){
		int MaxHooks = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxHooks < p->m_Stats.m_NumHooks){
				MaxHooks = p->m_Stats.m_NumHooks;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' hooked %d time%s in this round.", Server()->ClientName(PlayerID), MaxHooks, (MaxHooks == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//fastest player
	else if(r == 3){
		float MaxSpeed = 0.f;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxSpeed < p->m_Stats.m_MaxSpeed){
				MaxSpeed = p->m_Stats.m_MaxSpeed;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' was the fastest player with %4.2f tiles per second(no fallspeed).", Server()->ClientName(PlayerID), (MaxSpeed*(float)Server()->TickSpeed())/32.f);
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//most bounces
	else if(r == 4){
		int MaxTeeCols = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxTeeCols < p->m_Stats.m_NumTeeCollisions){
				MaxTeeCols = p->m_Stats.m_NumTeeCollisions;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' bounced %d time%s from other players.", Server()->ClientName(PlayerID), MaxTeeCols, (MaxTeeCols == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player longest freeze time
	else if(r == 5){
		int MaxFreezeTicks = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxFreezeTicks < p->m_Stats.m_NumFreezeTicks){
				MaxFreezeTicks = p->m_Stats.m_NumFreezeTicks;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' was frozen for %4.2f seconds total this round.", Server()->ClientName(PlayerID), (float)MaxFreezeTicks/(float)Server()->TickSpeed());
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player with most hammers to frozen teammates
	else if(r == 6){
		int MaxUnfreezeHammers = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxUnfreezeHammers < p->m_Stats.m_UnfreezingHammerHits){
				MaxUnfreezeHammers = p->m_Stats.m_UnfreezingHammerHits;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' hammered %d frozen teammate%s.", Server()->ClientName(PlayerID), MaxUnfreezeHammers, (MaxUnfreezeHammers == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player with most emotes
	else if(r == 7){
		int MaxEmotes = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxEmotes < p->m_Stats.m_NumEmotes){
				MaxEmotes = p->m_Stats.m_NumEmotes;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' emoted %d time%s.", Server()->ClientName(PlayerID), MaxEmotes, (MaxEmotes == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player that was hit most often
	else if(r == 8){
		int MaxHit = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxHit < p->m_Stats.m_Hits){
				MaxHit = p->m_Stats.m_Hits;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' was hitted %d time%s by the opponent's weapon.", Server()->ClientName(PlayerID), MaxHit, (MaxHit == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player that was thrown into spikes most often by opponents
	else if(r == 9){
		int MaxDeaths = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxDeaths < p->m_Stats.m_Deaths){
				MaxDeaths = p->m_Stats.m_Deaths;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' was thrown %d time%s into spikes by the opponents, while being frozen.", Server()->ClientName(PlayerID), MaxDeaths, (MaxDeaths == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		}
	}
	//player that threw most opponents into golden/colored spikes
	else if(r == 10){
		int MaxGold = 0;
		int PlayerID = -1;
		
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			CPlayer* p = m_apPlayers[i];
			if (!p || p->GetTeam() == TEAM_SPECTATORS) continue;
			if(MaxGold < p->m_Stats.m_GrabsGold){
				MaxGold = p->m_Stats.m_GrabsGold;
				PlayerID = i;
			}
		}
		
		if(PlayerID != -1){
			char buff[300];
			str_format(buff, sizeof(buff), "Trivia: '%s' threw %d time%s frozen opponents into golden spikes.", Server()->ClientName(PlayerID), MaxGold, (MaxGold == 1 ? "" : "s"));
			SendChat(-1, CGameContext::CHAT_ALL, buff);
			TriviaSent = true;
		} else {
			//send another trivia, bcs this is rare on maps without golden spikes
			SendRandomTrivia();			
			TriviaSent = true;
		}
	}
	
	if(!TriviaSent){
		SendChat(-1, CGameContext::CHAT_ALL, "Trivia: Press F1 and use PageUp and PageDown to scroll in the console window");
	}
}

int CGameContext::SendPackMsg(CNetMsg_Sv_KillMsg *pMsg, int Flags)
{
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		CPlayer* p = m_apPlayers[i];
		if (!p) continue;

		int id = pMsg->m_Killer;
		int id2 = pMsg->m_Victim;

		int originalId = pMsg->m_Killer;
		int originalId2 = pMsg->m_Victim;
		if (!p->IsSnappingClient(pMsg->m_Killer, p->m_ClientVersion, id) || !p->IsSnappingClient(pMsg->m_Victim, p->m_ClientVersion, id2)) continue;
		pMsg->m_Killer = id;
		pMsg->m_Victim = id2;
		Server()->SendPackMsg(pMsg, Flags, i);
		pMsg->m_Killer = originalId;
		pMsg->m_Victim = originalId2;
	}
	return 0;
}

int CGameContext::SendPackMsg(CNetMsg_Sv_Emoticon *pMsg, int Flags)
{
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		CPlayer* p = m_apPlayers[i];
		if (!p) continue;

		int id = pMsg->m_ClientID;
		int originalID = pMsg->m_ClientID;
		if (!p->IsSnappingClient(pMsg->m_ClientID, p->m_ClientVersion, id)) continue;
		pMsg->m_ClientID = id;
		Server()->SendPackMsg(pMsg, Flags, i);
		pMsg->m_ClientID = originalID;
	}
	return 0;
}

char msgbuf[1000];

int CGameContext::SendPackMsg(CNetMsg_Sv_Chat *pMsg, int Flags)
{
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		CPlayer* p = m_apPlayers[i];
		if (!p) continue;

		int id = pMsg->m_ClientID;
		int originalID = pMsg->m_ClientID;
		const char* pOriginalText = pMsg->m_pMessage;
		if (id > -1 && id < MAX_CLIENTS && !p->IsSnappingClient(pMsg->m_ClientID, p->m_ClientVersion, id)) {
			str_format(msgbuf, sizeof(msgbuf), "%s: %s", Server()->ClientName(pMsg->m_ClientID), pMsg->m_pMessage);

			pMsg->m_ClientID = (p->m_ClientVersion == CPlayer::CLIENT_VERSION_DDNET) ? CPlayer::DDNET_CLIENT_MAX_CLIENTS - 1 : CPlayer::VANILLA_CLIENT_MAX_CLIENTS - 1;
			pMsg->m_pMessage = msgbuf;
		}
		else pMsg->m_ClientID = id;
		Server()->SendPackMsg(pMsg, Flags, i); 
		pMsg->m_ClientID = originalID;
		pMsg->m_pMessage = pOriginalText;
	}
	return 0;
}


int CGameContext::SendPackMsg(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
{
	CPlayer* p = m_apPlayers[ClientID];
	if (!p)
		return -1;

	int id = pMsg->m_ClientID;

	int originalID = pMsg->m_ClientID;
	const char* pOriginalText = pMsg->m_pMessage;

	bool ClientNotVisible = (id > -1 && id < MAX_CLIENTS && !p->IsSnappingClient(pMsg->m_ClientID, p->m_ClientVersion, id));

	if(ClientNotVisible) {
		pMsg->m_ClientID = (p->m_ClientVersion == CPlayer::CLIENT_VERSION_DDNET) ? CPlayer::DDNET_CLIENT_MAX_CLIENTS - 1 : CPlayer::VANILLA_CLIENT_MAX_CLIENTS - 1;
	}
	else
		pMsg->m_ClientID = id;

	//if not ddnet client, split the msg
	if(p->m_ClientVersion == CPlayer::CLIENT_VERSION_DDNET)
		Server()->SendPackMsg(pMsg, Flags, ClientID);
	else {
		int StrLen = str_length(pMsg->m_pMessage);

		int StrOffset = 0;
		while(StrLen >= (126 - MAX_NAME_LENGTH)) {
			if(ClientNotVisible)
				str_format(msgbuf, sizeof(msgbuf), "%s: %s", Server()->ClientName(originalID), (pMsg->m_pMessage + (intptr_t)StrOffset));
			else
				str_format(msgbuf, sizeof(msgbuf), "%s", (pMsg->m_pMessage + (intptr_t)StrOffset));
			
			msgbuf[126] = 0;
			pMsg->m_pMessage = msgbuf;
			Server()->SendPackMsg(pMsg, Flags, ClientID);

			StrLen -= (126 - MAX_NAME_LENGTH);
			StrOffset += (126 - MAX_NAME_LENGTH);
		}

		if(StrLen > 0) {
			if(ClientNotVisible)
				str_format(msgbuf, sizeof(msgbuf), "%s: %s", Server()->ClientName(originalID), (pMsg->m_pMessage + (intptr_t)StrOffset));
			else
				str_format(msgbuf, sizeof(msgbuf), "%s", (pMsg->m_pMessage + (intptr_t)StrOffset));
			pMsg->m_pMessage = msgbuf;
			Server()->SendPackMsg(pMsg, Flags, ClientID);
		}
	}

	pMsg->m_ClientID = originalID;
	pMsg->m_pMessage = pOriginalText;
	return 0;
}

CCharacter* CGameContext::GetClosestEnemyToAim(CCharacter* pShooter, float MaxDistance)
{
    if (!pShooter || !pShooter->GetPlayer())
        return nullptr;

    int ShooterTeam = pShooter->GetPlayer()->GetTeam();
    const CNetObj_PlayerInput& Input = pShooter->GetLatestInput();
    vec2 AimPos = pShooter->m_Pos + normalize(vec2(Input.m_TargetX, Input.m_TargetY)) * 64.0f;

    CCharacter* pClosest = nullptr;
    float ClosestDist = MaxDistance;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i == pShooter->GetPlayer()->GetCID())
            continue; // skip self

        CPlayer* pPlayer = m_apPlayers[i];
        if (!pPlayer || !pPlayer->GetCharacter() || !pPlayer->GetCharacter()->IsAlive())
            continue;

        // Team check for teamplay mode
        if (m_pController->IsTeamplay() && pPlayer->GetTeam() == ShooterTeam)
            continue; // skip teammates

        CCharacter* pTarget = pPlayer->GetCharacter();
        float Dist = distance(pTarget->m_Pos, AimPos);
        if (Dist < ClosestDist)
        {
            ClosestDist = Dist;
            pClosest = pTarget;
        }
    }

    return pClosest;
}

void CGameContext::AddFrozenLeaver(const NETADDR &Addr, int Seconds)
{
	ExpireFrozenLeavers();

	if(m_FrozenLeaverCount >= MAX_FROZEN_LEAVERS)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", "Frozen leaver list full!");
		return;
	}

	NETADDR AddrClean = Addr;
	AddrClean.port = 0;

	m_aFrozenLeavers[m_FrozenLeaverCount].m_Addr = AddrClean;
	m_aFrozenLeavers[m_FrozenLeaverCount].m_ExpireTick = Server()->Tick() + Seconds * Server()->TickSpeed();
	++m_FrozenLeaverCount;

	char aBuf[NETADDR_MAXSTRSIZE];
	net_addr_str(&AddrClean, aBuf, sizeof(aBuf), true);
	char aPrint[128];
	str_format(aPrint, sizeof(aPrint), "Added frozen leaver block for %s (%ds)", aBuf, Seconds);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", aPrint);
}

bool CGameContext::IsFrozenLeaverBlocked(const NETADDR &Addr)
{
	ExpireFrozenLeavers();

	NETADDR AddrClean = Addr;
	AddrClean.port = 0;

	for(int i = 0; i < m_FrozenLeaverCount; ++i)
	{
		NETADDR BanAddr = m_aFrozenLeavers[i].m_Addr;
		BanAddr.port = 0;

		if(net_addr_comp(&BanAddr, &AddrClean) == 0)
			return true;
	}
	return false;
}


void CGameContext::ExpireFrozenLeavers()
{
	int i = 0;
	while(i < m_FrozenLeaverCount)
	{
		if(m_aFrozenLeavers[i].m_ExpireTick < Server()->Tick())
		{
			char aBuf[NETADDR_MAXSTRSIZE];
			net_addr_str(&m_aFrozenLeavers[i].m_Addr, aBuf, sizeof(aBuf), true);
			char aPrint[128];
			str_format(aPrint, sizeof(aPrint), "Expired frozen leaver block for %s", aBuf);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "frozenban", aPrint);

			m_aFrozenLeavers[i] = m_aFrozenLeavers[m_FrozenLeaverCount - 1];
			--m_FrozenLeaverCount;
		}
		else
			++i;
	}
}

//  CUSTOM SERVER COMMANDS FOR CONSOLE AND CHAT
void CGameContext::ConToggleDyncam(IConsole::IResult *pResult, void *pUserData)
{
    CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
    int Enable = pResult->GetInteger(0);

    if(Enable)
    {
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dyncam", "Dynamic camera ENABLED");
        pSelf->m_Config->m_SvDrawDistanceX = 1000;
        pSelf->m_Config->m_SvDrawDistanceY = 900;
        pSelf->m_Config->m_SvDrawDistanceRadius = 1100;
        pSelf->m_DyncamEnabled = true;
    }
    else
    {
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dyncam", "Dynamic camera DISABLED");
        pSelf->m_Config->m_SvDrawDistanceX = 800;
        pSelf->m_Config->m_SvDrawDistanceY = 500;
        pSelf->m_Config->m_SvDrawDistanceRadius = 850;
        pSelf->m_DyncamEnabled = false;
    }
}

void CGameContext::CmdUseless(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	CPlayer* pPlayer = pContext->m_apPlayers[pClientID];
	if(pPlayer)
	{
		CCharacter* pChar = pPlayer->GetCharacter();
		if(pChar)
		{
			pChar->GiveWeapon(WEAPON_GUN, 10); // Give 10 ammo
		}
	}
}

void CGameContext::ConMute(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUser);
	int ClientID = pResult->GetInteger(0);
	int Seconds = pResult->GetInteger(1);

	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", "Invalid client ID.");
		return;
	}

	char aAddrStr[NETADDR_MAXSTRSIZE];
    pSelf->Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));
    pSelf->m_MutedIPs[aAddrStr] = pSelf->Server()->Tick() + Seconds * pSelf->Server()->TickSpeed();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "'%s' has been muted for %d seconds.",
		pSelf->Server()->ClientName(ClientID), Seconds);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	pSelf->SendChat(-1, CHAT_ALL, aBuf);
}

void CGameContext::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
    CGameContext *pSelf = (CGameContext *)pUserData;
    int ClientID = pResult->GetInteger(0);
    if(!pSelf->m_apPlayers[ClientID])
        return;

    char aAddrStr[NETADDR_MAXSTRSIZE];
    pSelf->Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));

    int Removed = pSelf->m_MutedIPs.erase(aAddrStr);
    if(Removed)
    {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), "'%s' has been unmuted", pSelf->Server()->ClientName(ClientID));
        pSelf->SendChatTarget(ClientID, aBuf);
    }
    else
    {
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "unmute", "player not muted");
    }
}

void CGameContext::ConChangeGamemode(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *currentGametype = pSelf->GameType();
	const char *targetGametype = pResult->GetString(0);

	if (pResult->NumArguments() == 0) {
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Current gamemode: %s", currentGametype);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "context", aBuf);

		const int length = sizeof(fng_gametypes) / sizeof(fng_gametypes[0]);
		char bBuf[21 + length * 11];
		int totalLen = 0;
		const char *str1 = "Available gamemodes:";
#define CONCAT(string) str_copy((bBuf+totalLen), string, sizeof(bBuf) - totalLen);\
totalLen += str_length(string);\
bBuf[totalLen++] = ' ';
		CONCAT(str1);
		for (int i = 0; i < length; i++) {
			CONCAT(fng_gametypes[i].name);
		}
#undef CONCAT
		bBuf[--totalLen] = '\0';
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "context", bBuf);

		return;
	}

	if (str_comp(currentGametype, targetGametype) == 0) {
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "The server's gamemode is already %s", currentGametype);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "context", aBuf);
		return;
	}

	if (!isValidFNGType(targetGametype)) {
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s is not a valid gamemode", targetGametype);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "context", aBuf);
		return;
	}

	char aBuf[48];
	str_format(aBuf, sizeof(aBuf), "sv_gametype %s; reload", targetGametype);
	pSelf->Console()->ExecuteLine(aBuf);
}


void CGameContext::ConMakeSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int victimID = pResult->GetInteger(0);

	// For messages with spaces, wrap them in double quotes
	// e.g., makesay 0 "Hello world!"
	const char *msg = pResult->GetString(1);

	if (victimID < 0 || victimID >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makesay", "The ID must be between 0 and the maximum number of clients");
		return;
	}

	if (!pSelf->m_apPlayers[victimID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makesay", "There is no client with that ID");
		return;
	}

	pSelf->SendChat(victimID, CHAT_ALL, msg);
}


// END OF CUSTOM SERVER COMMANDS
extern "C" void NormalizeChat(char *pStr, int Size)
{
    char aOut[256];
    int iWrite = 0;

    // Define replacement sets
    struct { const char *pFrom; char To; } aReplacements[] = {
        { "AÁÀÂÃÄÅĀĂĄǍȦǠÆⱯ∀🅰Ⓐⓐ@", 'a' },
        { "BḂḄḆƁƀƂƃℬ🅱Ⓑⓑ", 'b' },
        { "CÇĆĈĊČƇƈȻȼℂ🅲Ⓒⓒ", 'c' },
        { "DĎĐḊḌḎḐḒƊƋ🅳Ⓓⓓ", 'd' },
        { "EÉÈÊËĒĔĖĘĚȨℰ🅴Ⓔⓔ3", 'e' },
        { "FƑƒḞℱ🅵Ⓕⓕ", 'f' },
        { "GĜĞĠĢƓɠℊ🅶Ⓖⓖ9", 'g' },
        { "HĤĦḢḤḦḨḪℋ🅷Ⓗⓗ", 'h' },
        { "IÍÌÎÏĨĪĬĮİǏƗℐ🅸Ⓘⓘ1", 'i' },
        { "JĴ🅹Ⓙⓙ", 'j' },
        { "KĶƘḰḲḴ🅺Ⓚⓚ", 'k' },
        { "LĹĻĽĿŁ🅻Ⓛⓛ", 'l' },
        { "MḾṀṂℳ🅼Ⓜⓜ", 'm' },
        { "NŃŅŇƝṄṆṈṊℕ🅽Ⓝⓝ", 'n' },
        { "OÓÒÔÕÖŌŎŐǑƠØȮȰŒ🅾Ⓞⓞ", 'o' },
        { "PƤṔṖ🅿Ⓟⓟ", 'p' },
        { "Qℚ🆀Ⓠⓠ", 'q' },
        { "RŔŖŘṘṚṜṞℝ🆁Ⓡⓡ", 'r' },
        { "SŚŜŞŠṠṢṤṦṨ🆂Ⓢⓢ5", 's' },
        { "TŢŤṪṬṮṰ🆃Ⓣⓣ", 't' },
        { "UÚÙÛÜŨŪŬŮŰŲǓƯ🆄Ⓤⓤ", 'u' },
        { "VṼṾ🆅Ⓥⓥ", 'v' },
        { "WẀẂẄŴ🆆Ⓦⓦ", 'w' },
        { "XẊẌ🆇Ⓧⓧ", 'x' },
        { "YÝŶŸȲɎ🆈Ⓨⓨ¥", 'y' },
        { "ZŹŻŽƵẐẒẔ🆉Ⓩⓩ", 'z' },
    };

    // Characters to fully remove (symbols, punctuation, emoji, etc.)
    const char *aRemove =
        "⓪①②③④⑤⑥⑦⑧⑨"
        "ⓐⓑⓒⓓⓔⓕⓖⓗⓘⓙⓚⓛⓜⓝⓞⓟⓠⓡⓢⓣⓤⓥⓦⓧⓨⓩ"
        "ⒶⒷⒸⒹⒺⒻⒼⒽⒾⒿⓀⓁⓂⓃⓄⓅⓆⓇⓈⓉⓊⓋⓌⓍⓎⓏ"
        "⌀⌁⌂⍓⍔⍕⍖⍗⍘⍙⍚⌃⌄⌅⌆⌇⌈⌉⌊⌋⌌⌍⌎⌏⌐⌑⌒⌓⌔⌕⌖⌗⌘⌙⌚⌛⌜⌝"
        "⌞⌟⌠⌡⌢⌣⌤⌥⌦⌧⌫⌬⌭⌮⌯⌰⌱⌲⌳⌴⌵⌶⌿⍀⍁⍂⍃⍄⍅⍆⍇⍈⍉⍊⍋"
        "⍌⍍⍎⍏⍐⍑⍒⍚﹘｝～"
        "♔♚♕♛♗♝♘♞♙♟♖♜"
        "☼☽☾❅❆ϟ☀☁☂☃☄☼"
        "⋆✢✥✦✧❂❉✱✲✴✵✶✷✸❇✹✺✻✼❈✮✡"
        "¼½¾⅓⅔⅛⅜⅝≈>≥≧≩≫≳⋝÷∕±∓≂⊟⊞⨁⨤⨦%∟∠∡⊾⟀⦌⦋⦀√∛∜"
        "≡≢⧥⩧⅀◊⟠⨌⨏⨜⨛◜◝◞◟"
        "↑↓→←↔▲▼►◄△⇿⇾⇽⇼⇻⇺⇹⇸⇶⇵⇳"
        "⇲⇱⇪⇩⇨⇧⇦⇥⇤⇣⇢⇡⇠⇛⇙⇘⇗⇖⇕⇔⇓⇒⇑"
        "⇐⇌⇋⥊⥋⇆⇅⇄↻↺↹↷↶↵↴↳↲↱↰↮↬↫"
        "↨↧↦↥↤↛↚↙↘↗↖↕"
        "-=_!@#$%^&*()_+[]\\;'\",./{}|:<>? ";

    for(int i = 0; pStr[i] && iWrite < Size - 1; ++i)
    {
        unsigned char c = (unsigned char)pStr[i];
        bool bReplaced = false;

        // Try replacing from mapping table
        for(unsigned j = 0; j < sizeof(aReplacements)/sizeof(aReplacements[0]); ++j)
        {
            if(str_chr(aReplacements[j].pFrom, c))
            {
                c = aReplacements[j].To;
                bReplaced = true;
                break;
            }
        }

        // If still not replaced and needs removal, skip
        if(!bReplaced && str_chr(aRemove, c))
            continue;

        // Normalize to lowercase
        aOut[iWrite++] = tolower(c);
    }

    aOut[iWrite] = '\0';
    str_copy(pStr, aOut, Size);
}

void CGameContext::SavePlayerStatsToFile(CPlayer *pPlayer)
{
	//dbg_msg("stats", "running function to save stats.db");
	if (!pPlayer)
		return;
    if (!pPlayer || pPlayer->m_aSavedName[0] == '\0')
	{
		//dbg_msg("stats", "Player name is empty, skipping stat save");
		return;
	}
	std::vector<std::string> Lines;
	bool Replaced = false;

	IOHANDLE FileRead = io_open("stats.db", IOFLAG_READ);
	if (FileRead)
	{
		char aBuf[512] = {0};
		int Pos = 0;
		char c;

		while (io_read(FileRead, &c, 1))
		{
			if (c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				if (sscanf(aBuf, "%32[^:]", aName) == 1)
				{
					if (str_comp(aName, pPlayer->m_aSavedName) == 0)
					{
						// Replace with updated line
						char aNewLine[256];
						str_format(aNewLine, sizeof(aNewLine),
							"%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
							pPlayer->m_aSavedName,
							pPlayer->m_Kills,
							pPlayer->m_Deaths,
							pPlayer->m_Shots,
							pPlayer->m_Misses,
							pPlayer->m_Wallshots,
							pPlayer->m_Freezes,
							pPlayer->m_Frozen,
							pPlayer->m_TimeFrozen,
							pPlayer->m_Saves,
							pPlayer->m_SavedBy,
							pPlayer->m_GreenSpikeKills,
							pPlayer->m_GoldSpikeKills,
							pPlayer->m_PurpleSpikeKills,
							pPlayer->m_WrongShrineKills,
							pPlayer->m_Steals,
							pPlayer->m_StolenFrom,
							pPlayer->m_MaxSpree,
							pPlayer->m_MaxMulti
						);
						Lines.emplace_back(aNewLine);
						Replaced = true;
					}
					else
					{
						Lines.emplace_back(aBuf);
					}
				}
				else
				{
					Lines.emplace_back(aBuf);
				}
				Pos = 0;
			}
			else
			{
				aBuf[Pos++] = c;
			}
		}
		io_close(FileRead);
	}

	if (!Replaced)
	{
		char aNewLine[256];
		str_format(aNewLine, sizeof(aNewLine),
			"%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
			pPlayer->m_aSavedName,
			pPlayer->m_Kills,
			pPlayer->m_Deaths,
			pPlayer->m_Shots,
			pPlayer->m_Misses,
			pPlayer->m_Wallshots,
			pPlayer->m_Freezes,
			pPlayer->m_Frozen,
			pPlayer->m_TimeFrozen,
			pPlayer->m_Saves,
			pPlayer->m_SavedBy,
			pPlayer->m_GreenSpikeKills,
			pPlayer->m_GoldSpikeKills,
			pPlayer->m_PurpleSpikeKills,
			pPlayer->m_WrongShrineKills,
			pPlayer->m_Steals,
			pPlayer->m_StolenFrom,
			pPlayer->m_MaxSpree,
			pPlayer->m_MaxMulti
		);
		Lines.emplace_back(aNewLine);
	}

	IOHANDLE FileWrite = io_open("stats.db", IOFLAG_WRITE);
	if (FileWrite)
	{
		for (const auto &Line : Lines)
		{
			io_write(FileWrite, Line.c_str(), Line.length());
			io_write(FileWrite, "\n", 1);
		}
		io_close(FileWrite);
	}
	else
	{
		//dbg_msg("stats", "Failed to open stats.db for writing");
	}
}

void CGameContext::LoadPlayerStatsFromFile(CPlayer *pPlayer)
{
	//dbg_msg("stats", "running function to load stats.db");
	if (!pPlayer)
		return;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if (!File)
	{
		//dbg_msg("stats", "Failed to open or create stats.db for reading");
		return;
	}

	char aBuf[512] = {0};
	int Pos = 0;
	char c;

	while (io_read(File, &c, 1))
	{
		if (c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Kills, Deaths, Shots, Misses, Wallshots;
			int Freezes, Frozen, TimeFrozen;
			int Saves, SavedBy;
			int Green, Gold, Purple, Wrong;
			int Steals, StolenFrom, MaxSpree, MaxMulti;

			if (sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Kills, &Deaths, &Shots, &Misses, &Wallshots,
				&Freezes, &Frozen, &TimeFrozen,
				&Saves, &SavedBy,
				&Green, &Gold, &Purple, &Wrong,
				&Steals, &StolenFrom, &MaxSpree, &MaxMulti) == 19)
			{
				if (str_comp(aName, pPlayer->m_aSavedName) == 0)
				{
					pPlayer->m_Kills = Kills;
					pPlayer->m_Deaths = Deaths;
					pPlayer->m_Shots = Shots;
					pPlayer->m_Misses = Misses;
					pPlayer->m_Wallshots = Wallshots;

					pPlayer->m_Freezes = Freezes;
					pPlayer->m_Frozen = Frozen;
					pPlayer->m_TimeFrozen = TimeFrozen;

					pPlayer->m_Saves = Saves;
					pPlayer->m_SavedBy = SavedBy;

					pPlayer->m_GreenSpikeKills = Green;
					pPlayer->m_GoldSpikeKills = Gold;
					pPlayer->m_PurpleSpikeKills = Purple;
					pPlayer->m_WrongShrineKills = Wrong;

					pPlayer->m_Steals = Steals;
					pPlayer->m_StolenFrom = StolenFrom;

					if (MaxSpree > pPlayer->m_MaxSpree)
						pPlayer->m_MaxSpree = MaxSpree;
					if (MaxMulti > pPlayer->m_MaxMulti)
						pPlayer->m_MaxMulti = MaxMulti;
					break;
				}
			}
			Pos = 0;
		}
		else
		{
			aBuf[Pos++] = c;
		}
	}

	io_close(File);
}

void CGameContext::SaveRoundStatsToFile(CPlayer *pPlayer)
{
	//dbg_msg("roundstats", "running function to save roundstats.db");
	if (!pPlayer || pPlayer->m_aSavedName[0] == '\0')
	{
		//dbg_msg("roundstats", "Player name is empty, skipping round stat save");
		return;
	}

	std::vector<std::string> Lines;
	bool Replaced = false;

	IOHANDLE FileRead = io_open("roundstats.db", IOFLAG_READ);
	if (FileRead)
	{
		char aBuf[512] = {0};
		int Pos = 0;
		char c;

		while (io_read(FileRead, &c, 1))
		{
			if (c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				if (sscanf(aBuf, "%32[^:]", aName) == 1)
				{
					if (str_comp(aName, pPlayer->m_aSavedName) == 0)
					{
						char aNewLine[256];
						str_format(aNewLine, sizeof(aNewLine),
							"%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
							pPlayer->m_aSavedName,
							pPlayer->m_RoundKills,
							pPlayer->m_RoundDeaths,
							pPlayer->m_RoundShots,
							pPlayer->m_RoundMisses,
							pPlayer->m_RoundWallshots,
							pPlayer->m_RoundFreezes,
							pPlayer->m_RoundFrozen,
							pPlayer->m_RoundTimeFrozen,
							pPlayer->m_RoundSaves,
							pPlayer->m_RoundSavedBy,
							pPlayer->m_RoundGreenSpikeKills,
							pPlayer->m_RoundGoldSpikeKills,
							pPlayer->m_RoundPurpleSpikeKills,
							pPlayer->m_RoundWrongShrineKills,
							pPlayer->m_RoundSteals,
							pPlayer->m_RoundStolenFrom,
							pPlayer->m_RoundMaxSpree,
							pPlayer->m_RoundMaxMulti
						);
						Lines.emplace_back(aNewLine);
						Replaced = true;
					}
					else
					{
						Lines.emplace_back(aBuf);
					}
				}
				else
				{
					Lines.emplace_back(aBuf);
				}
				Pos = 0;
			}
			else
			{
				aBuf[Pos++] = c;
			}
		}
		io_close(FileRead);
	}

	if (!Replaced)
	{
		char aNewLine[256];
		str_format(aNewLine, sizeof(aNewLine),
			"%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
			pPlayer->m_aSavedName,
			pPlayer->m_RoundKills,
			pPlayer->m_RoundDeaths,
			pPlayer->m_RoundShots,
			pPlayer->m_RoundMisses,
			pPlayer->m_RoundWallshots,
			pPlayer->m_RoundFreezes,
			pPlayer->m_RoundFrozen,
			pPlayer->m_RoundTimeFrozen,
			pPlayer->m_RoundSaves,
			pPlayer->m_RoundSavedBy,
			pPlayer->m_RoundGreenSpikeKills,
			pPlayer->m_RoundGoldSpikeKills,
			pPlayer->m_RoundPurpleSpikeKills,
			pPlayer->m_RoundWrongShrineKills,
			pPlayer->m_RoundSteals,
			pPlayer->m_RoundStolenFrom,
			pPlayer->m_RoundMaxSpree,
			pPlayer->m_RoundMaxMulti
		);
		Lines.emplace_back(aNewLine);
	}

	IOHANDLE FileWrite = io_open("roundstats.db", IOFLAG_WRITE);
	if (FileWrite)
	{
		for (const auto &Line : Lines)
		{
			io_write(FileWrite, Line.c_str(), Line.length());
			io_write(FileWrite, "\n", 1);
		}
		io_close(FileWrite);
	}
	else
	{
		//dbg_msg("roundstats", "Failed to open roundstats.db for writing");
	}
}

void CGameContext::LoadRoundStatsFromFile(CPlayer *pPlayer)
{
	//dbg_msg("roundstats", "running function to load roundstats.db");
	if (!pPlayer)
		return;

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if (!File)
	{
		//dbg_msg("roundstats", "Failed to open roundstats.db for reading");
		return;
	}

	char aBuf[512] = {0};
	int Pos = 0;
	char c;

	while (io_read(File, &c, 1))
	{
		if (c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Kills, Deaths, Shots, Misses, Wallshots;
			int Freezes, Frozen, TimeFrozen;
			int Saves, SavedBy;
			int Green, Gold, Purple, Wrong;
			int Steals, StolenFrom, MaxSpree, MaxMulti;

			if (sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Kills, &Deaths, &Shots, &Misses, &Wallshots,
				&Freezes, &Frozen, &TimeFrozen,
				&Saves, &SavedBy,
				&Green, &Gold, &Purple, &Wrong,
				&Steals, &StolenFrom, &MaxSpree, &MaxMulti) == 19)
			{
				if (str_comp(aName, pPlayer->m_aSavedName) == 0)
				{
					pPlayer->m_RoundKills = Kills;
					pPlayer->m_RoundDeaths = Deaths;
					pPlayer->m_RoundShots = Shots;
					pPlayer->m_RoundMisses = Misses;
					pPlayer->m_RoundWallshots = Wallshots;

					pPlayer->m_RoundFreezes = Freezes;
					pPlayer->m_RoundFrozen = Frozen;
					pPlayer->m_RoundTimeFrozen = TimeFrozen;

					pPlayer->m_RoundSaves = Saves;
					pPlayer->m_RoundSavedBy = SavedBy;

					pPlayer->m_RoundGreenSpikeKills = Green;
					pPlayer->m_RoundGoldSpikeKills = Gold;
					pPlayer->m_RoundPurpleSpikeKills = Purple;
					pPlayer->m_RoundWrongShrineKills = Wrong;

					pPlayer->m_RoundSteals = Steals;
					pPlayer->m_RoundStolenFrom = StolenFrom;

					pPlayer->m_RoundMaxSpree = MaxSpree;
					pPlayer->m_RoundMaxMulti = MaxMulti;
					break;
				}
			}
			Pos = 0;
		}
		else
		{
			aBuf[Pos++] = c;
		}
	}

	io_close(File);
}

bool CGameContext::LoadRoundStatsByName(const char* pName, CPlayer* pTmp)
{
	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(!File)
		return false;

	char aBuf[512] = {0};
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Kills, Deaths, Shots, Misses, Wallshots;
			int Freezes, Frozen, TimeFrozen;
			int Saves, SavedBy;
			int Green, Gold, Purple, Wrong;
			int Steals, StolenFrom, MaxSpree, MaxMulti;

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Kills, &Deaths, &Shots, &Misses, &Wallshots,
				&Freezes, &Frozen, &TimeFrozen,
				&Saves, &SavedBy,
				&Green, &Gold, &Purple, &Wrong,
				&Steals, &StolenFrom, &MaxSpree, &MaxMulti) == 19)
			{
				if(str_comp_nocase(aName, pName) == 0)
				{
					// Populate dummy player stats
					pTmp->m_RoundKills = Kills;
					pTmp->m_RoundDeaths = Deaths;
					pTmp->m_RoundShots = Shots;
					pTmp->m_RoundMisses = Misses;
					pTmp->m_RoundWallshots = Wallshots;

					pTmp->m_RoundFreezes = Freezes;
					pTmp->m_RoundFrozen = Frozen;
					pTmp->m_RoundTimeFrozen = TimeFrozen;

					pTmp->m_RoundSaves = Saves;
					pTmp->m_RoundSavedBy = SavedBy;

					pTmp->m_RoundGreenSpikeKills = Green;
					pTmp->m_RoundGoldSpikeKills = Gold;
					pTmp->m_RoundPurpleSpikeKills = Purple;
					pTmp->m_RoundWrongShrineKills = Wrong;

					pTmp->m_RoundSteals = Steals;
					pTmp->m_RoundStolenFrom = StolenFrom;

					pTmp->m_RoundMaxSpree = MaxSpree;
					pTmp->m_RoundMaxMulti = MaxMulti;

					io_close(File);
					return true;
				}
			}
			Pos = 0;
		}
		else
		{
			aBuf[Pos++] = c;
		}
	}

	io_close(File);
	return false;
}

bool CGameContext::LoadTotalStatsByName(const char* pName, CPlayer* pTmp)
{
	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
		return false;

	char aBuf[512] = {0};
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Kills, Deaths, Shots, Misses, Wallshots;
			int Freezes, Frozen, TimeFrozen;
			int Saves, SavedBy;
			int Green, Gold, Purple, Wrong;
			int Steals, StolenFrom, MaxSpree, MaxMulti;

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Kills, &Deaths, &Shots, &Misses, &Wallshots,
				&Freezes, &Frozen, &TimeFrozen,
				&Saves, &SavedBy,
				&Green, &Gold, &Purple, &Wrong,
				&Steals, &StolenFrom, &MaxSpree, &MaxMulti) == 19)
			{
				if(str_comp_nocase(aName, pName) == 0)
				{
					pTmp->m_Kills = Kills;
					pTmp->m_Deaths = Deaths;
					pTmp->m_Shots = Shots;
					pTmp->m_Misses = Misses;
					pTmp->m_Wallshots = Wallshots;

					pTmp->m_Freezes = Freezes;
					pTmp->m_Frozen = Frozen;
					pTmp->m_TimeFrozen = TimeFrozen;

					pTmp->m_Saves = Saves;
					pTmp->m_SavedBy = SavedBy;

					pTmp->m_GreenSpikeKills = Green;
					pTmp->m_GoldSpikeKills = Gold;
					pTmp->m_PurpleSpikeKills = Purple;
					pTmp->m_WrongShrineKills = Wrong;

					pTmp->m_Steals = Steals;
					pTmp->m_StolenFrom = StolenFrom;

					pTmp->m_MaxSpree = MaxSpree;
					pTmp->m_MaxMulti = MaxMulti;

					io_close(File);
					return true;
				}
			}
			Pos = 0;
		}
		else
		{
			aBuf[Pos++] = c;
		}
	}

	io_close(File);
	return false;
}

void CGameContext::CmdFewSteals(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int NetSteals; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		int NetSteals = p->m_RoundSteals - p->m_RoundKills;
		if(NetSteals == 0)
			continue;

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.NetSteals = NetSteals;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					int NetSteals = Dummy[14] - Dummy[0];
					if(NetSteals == 0)
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.NetSteals = NetSteals;
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /fewsteals.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& e1, const Entry& e2) {
		return e1.NetSteals < e2.NetSteals;
	});

	int Best = List[0].NetSteals;

	std::vector<Entry> Winners;
	for(const auto& e : List)
	{
		if(e.NetSteals == Best)
			Winners.push_back(e);
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "- fewest net steals this round: %d (%s)", Best, Winners[0].Name);
	pContext->SendChatTarget(pClientID, aBuf);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].NetSteals, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].NetSteals, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].NetSteals, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdFewStealsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int NetSteals; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Steals = Dummy[14];
				int Kills = Dummy[0];
				int Net = Steals - Kills;

				if(Net != 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.NetSteals = Net;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.NetSteals < b.NetSteals;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /fewstealsa.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- fewest net steals: %d (%s)", List[0].NetSteals, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].NetSteals, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].NetSteals, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].NetSteals, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopSteals(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Steals; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);
		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Steals = p->m_RoundSteals;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Steals = Dummy[14];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topsteals.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Steals > b.Steals;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top steals this round: %d (%s)", List[0].Steals, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Steals, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Steals, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Steals, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopStealsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Steals; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Steals = Dummy[14];
				if(Steals > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Steals = Steals;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Steals > b.Steals;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topstealsa.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top steals: %d (%s)", List[0].Steals, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Steals, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Steals, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Steals, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopWalls(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Wallshots; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Wallshots = p->m_RoundWallshots;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Wallshots = Dummy[4];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topwalls.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Wallshots > b.Wallshots;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top wallshots this round: %d (%s)", List[0].Wallshots, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Wallshots, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Wallshots, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Wallshots, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopWallsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Wallshots; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Wallshots = Dummy[4];
				if(Wallshots > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Wallshots = Wallshots;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Wallshots > b.Wallshots;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topwallsa.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top wallshots: %d (%s)", List[0].Wallshots, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Wallshots, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Wallshots, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Wallshots, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopWrong(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Wrong; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Wrong = p->m_RoundWrongShrineKills;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Wrong = Dummy[13];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topwrong.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Wrong > b.Wrong;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top wrong kills this round: %d (%s)", List[0].Wrong, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Wrong, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Wrong, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Wrong, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}


void CGameContext::CmdTopWrongAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Wrong; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Wrong = Dummy[13];
				if(Wrong > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Wrong = Wrong;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Wrong > b.Wrong;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topwronga.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top wrong kills: %d (%s)", List[0].Wrong, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Wrong, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Wrong, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Wrong, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}


void CGameContext::CmdTopKills(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Kills; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Kills = p->m_RoundKills;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Kills = Dummy[0];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topkills.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Kills > b.Kills;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top kills this round: %d (%s)", List[0].Kills, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Kills, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Kills, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Kills, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}


void CGameContext::CmdTopKillsAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Kills; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Kills = Dummy[0];
				if(Kills > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Kills = Kills;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Kills > b.Kills;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topkillsa.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top kills: %d (%s)", List[0].Kills, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Kills, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Kills, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Kills, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}


void CGameContext::CmdTopAccuracy(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; float Accuracy; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	// In-game players
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		int Shots = p->m_RoundShots;
		int Freezes = p->m_RoundFreezes;

		if(Shots < 10 || Freezes <= 0)
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Accuracy = (float)Freezes / Shots * 100.0f;
		List.push_back(e);
	}

	// Load from roundstats.db
	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int D[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7],
					&D[8], &D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end() || D[2] < 10 || D[5] <= 0)
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Accuracy = (float)D[5] / D[2] * 100.0f;
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topaccuracy.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Accuracy > b.Accuracy;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top accuracy this round: %.3f%% (%s)", List[0].Accuracy, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%.3f%% (%s)", FirstInLine ? "" : ", ", List[i].Accuracy, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%.3f%% (%s)", List[i].Accuracy, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopAccuracyAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; float Accuracy; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int D[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7],
				&D[8], &D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
			{
				if(D[2] >= 500 && D[5] > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Accuracy = (float)D[5] / D[2] * 100.0f;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Accuracy > b.Accuracy;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topaccuracya.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top accuracy: %.3f%% (%s)", List[0].Accuracy, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%.3f%% (%s)", FirstInLine ? "" : ", ", List[i].Accuracy, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%.3f%% (%s)", List[i].Accuracy, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopFreeze(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Freezes; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		int Freezes = p->m_RoundFreezes;
		if(Freezes <= 0)
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Freezes = Freezes;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int D[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7],
					&D[8], &D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end() || D[5] <= 0)
					{
						Pos = 0;
						continue;
					}
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Freezes = D[5];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topfreeze.");
		return;
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Freezes > b.Freezes;
	});

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top freezes this round: %d (%s)", List[0].Freezes, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%d (%s)", FirstInLine ? "" : ", ", List[i].Freezes, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Freezes, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopFreezeAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Freezes; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int D[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7],
				&D[8], &D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
			{
				if(D[5] <= 0)
				{
					Pos = 0;
					continue;
				}
				Entry e;
				str_copy(e.Name, aName, sizeof(e.Name));
				e.Freezes = D[5];
				List.push_back(e);
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Freezes > b.Freezes;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topfreezea.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top freezes: %d (%s)", List[0].Freezes, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%d (%s)", FirstInLine ? "" : ", ", List[i].Freezes, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Freezes, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopGreen(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Green; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Green = Dummy[10];
				if(Green > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Green = Green;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Green > b.Green;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topgreen.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top green kills: %d (%s)", List[0].Green, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Green, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Green, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Green, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopGold(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Gold; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Gold = Dummy[11];
				if(Gold > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Gold = Gold;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Gold > b.Gold;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topgold.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top gold kills: %d (%s)", List[0].Gold, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Gold, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Gold, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Gold, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopPurple(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Purple; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Purple = Dummy[12];
				if(Purple > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Purple = Purple;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Purple > b.Purple;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /toppurple.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top purple kills: %d (%s)", List[0].Purple, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Purple, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Purple, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Purple, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopSpree(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Spree; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Spree = Dummy[16];
				if(Spree > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Spree = Spree;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Spree > b.Spree;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topspree.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top spree: %d (%s)", List[0].Spree, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Spree, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Spree, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Spree, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopMulti(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Multi; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Multi = Dummy[17];
				if(Multi > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Multi = Multi;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Multi > b.Multi;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topmulti.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top multi: %d (%s)", List[0].Multi, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Multi, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Multi, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Multi, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopSaves(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Saves; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		int Saves = p->m_RoundSaves;
		if(Saves <= 0)
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		Seen.insert(pName);

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.Saves = Saves;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int D[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName, &D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7],
					&D[8], &D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end() || D[8] <= 0)
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Saves = D[8];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Saves > b.Saves;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "- top saves this round: 0 (none)");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top saves this round: %d (%s)", List[0].Saves, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Saves, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Saves, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Saves, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopSavesAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Saves; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Saves = Dummy[8];
				if(Saves > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Saves = Saves;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Saves > b.Saves;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data for /topsavesa.");
		return;
	}

	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top saves: %d (%s)", List[0].Saves, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;
	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%d (%s)", List[i].Saves, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %d (%s)", List[i].Saves, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%d (%s)", List[i].Saves, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

bool CGameContext::CheckStatCommandCooldown(int ClientID)
{
	CPlayer* pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return true;

	int Now = Server()->Tick();
	int Cooldown = Server()->TickSpeed() * 5; // 5 seconds
	int Remaining = (pPlayer->m_LastCommandRequestTick + Cooldown - Now) / Server()->TickSpeed();

	if(Now < pPlayer->m_LastCommandRequestTick + Cooldown)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s", Remaining + 1, Remaining == 0 ? "" : "s");
		SendChatTarget(ClientID, aBuf);
		return false;
	}

	pPlayer->m_LastCommandRequestTick = Now;
	return true;
}

void CGameContext::CmdTopFrozen(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int TimeFrozen; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		int TimeFrozen = p->m_RoundTimeFrozen;
		if(TimeFrozen <= 0)
			continue;

		Seen.insert(pName);
		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.TimeFrozen = TimeFrozen;
		List.push_back(e);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;

		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int Dummy[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
					&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
					&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
					&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end() || Dummy[7] <= 0)
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.TimeFrozen = Dummy[7];
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.TimeFrozen > b.TimeFrozen;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "- top time frozen this round: 0s (none)");
		return;
	}

	char aTimeStr[32];
	FormatTime(List[0].TimeFrozen, aTimeStr, sizeof(aTimeStr));
	char aBufOut[128];
	str_format(aBufOut, sizeof(aBufOut), "- top time frozen this round: %s (%s)", aTimeStr, List[0].Name);
	pContext->SendChatTarget(pClientID, aBufOut);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		FormatTime(List[i].TimeFrozen, aTimeStr, sizeof(aTimeStr));

		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%s (%s)", aTimeStr, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %s (%s)", aTimeStr, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%s (%s)", aTimeStr, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopFrozenAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; int Seconds; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;
	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int Dummy[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&Dummy[0], &Dummy[1], &Dummy[2], &Dummy[3], &Dummy[4],
				&Dummy[5], &Dummy[6], &Dummy[7], &Dummy[8], &Dummy[9],
				&Dummy[10], &Dummy[11], &Dummy[12], &Dummy[13],
				&Dummy[14], &Dummy[15], &Dummy[16], &Dummy[17]) == 19)
			{
				int Seconds = Dummy[7];
				if(Seconds > 0)
				{
					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.Seconds = Seconds;
					List.push_back(e);
				}
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.Seconds > b.Seconds;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "- top time frozen: 0s (none)");
		return;
	}

	char aTimeStr[32];
	FormatTime(List[0].Seconds, aTimeStr, sizeof(aTimeStr));
	str_format(aBuf, sizeof(aBuf), "- top time frozen: %s (%s)", aTimeStr, List[0].Name);
	pContext->SendChatTarget(pClientID, aBuf);

	char Line[128] = "";
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		FormatTime(List[i].Seconds, aTimeStr, sizeof(aTimeStr));

		char aPart[64];
		if(FirstInLine)
			str_format(aPart, sizeof(aPart), "%s (%s)", aTimeStr, List[i].Name);
		else
			str_format(aPart, sizeof(aPart), ", %s (%s)", aTimeStr, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%s (%s)", aTimeStr, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopKD(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; float KD; };
	std::vector<Entry> List;
	std::set<std::string> Seen;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer* p = pContext->m_apPlayers[i];
		if(!p || !p->GetCharacter())
			continue;

		const char* pName = pContext->Server()->ClientName(i);
		int Kills = p->m_RoundKills;
		int Deaths = p->m_RoundDeaths;
		float KD = (float)Kills / (Deaths > 0 ? Deaths : 1);
		if(KD <= 0)
			continue;

		Entry e;
		str_copy(e.Name, pName, sizeof(e.Name));
		e.KD = KD;
		List.push_back(e);
		Seen.insert(pName);
	}

	IOHANDLE File = io_open("roundstats.db", IOFLAG_READ);
	if(File)
	{
		char aBuf[512];
		int Pos = 0;
		char c;
		while(io_read(File, &c, 1))
		{
			if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
			{
				aBuf[Pos] = 0;
				char aName[MAX_NAME_LENGTH];
				int D[18];

				if(sscanf(aBuf,
					"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
					aName,
					&D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7], &D[8],
					&D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
				{
					if(Seen.find(aName) != Seen.end())
					{
						Pos = 0;
						continue;
					}

					int Kills = D[0];
					int Deaths = D[1];
					float KD = (float)Kills / (Deaths > 0 ? Deaths : 1);
					if(KD <= 0)
					{
						Pos = 0;
						continue;
					}

					Entry e;
					str_copy(e.Name, aName, sizeof(e.Name));
					e.KD = KD;
					List.push_back(e);
				}
				Pos = 0;
			}
			else aBuf[Pos++] = c;
		}
		io_close(File);
	}

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.KD > b.KD;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "- top K/D: 0 (none)");
		return;
	}

	char Line[128];
	str_format(Line, sizeof(Line), "- top K/D: %.3f (%s)", List[0].KD, List[0].Name);
	pContext->SendChatTarget(pClientID, Line);

	str_copy(Line, "", sizeof(Line));
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%.3f (%s)", FirstInLine ? "" : ", ", List[i].KD, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%.3f (%s)", List[i].KD, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}

	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void CGameContext::CmdTopKDAll(CGameContext* pContext, int pClientID, const char** pArgs, int ArgNum)
{
	struct Entry { char Name[MAX_NAME_LENGTH]; float KD; };
	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if(!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;
	while(io_read(File, &c, 1))
	{
		if(c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int D[18];

			if(sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7], &D[8],
				&D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
			{
				int Kills = D[0];
				int Deaths = D[1];
				if(Kills < 100)
				{
					Pos = 0;
					continue;
				}

				float KD = (float)Kills / (Deaths > 0 ? Deaths : 1);
				if(KD <= 0)
				{
					Pos = 0;
					continue;
				}

				Entry e;
				str_copy(e.Name, aName, sizeof(e.Name));
				e.KD = KD;
				List.push_back(e);
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	std::sort(List.begin(), List.end(), [](const Entry& a, const Entry& b) {
		return a.KD > b.KD;
	});

	if(List.empty())
	{
		pContext->SendChatTarget(pClientID, "- top K/D: 0 (none)");
		return;
	}

	char Line[128];
	str_format(Line, sizeof(Line), "- top K/D: %.3f (%s)", List[0].KD, List[0].Name);
	pContext->SendChatTarget(pClientID, Line);

	str_copy(Line, "", sizeof(Line));
	bool FirstInLine = true;

	for(size_t i = 1; i < List.size() && i < 20; ++i)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s%.3f (%s)", FirstInLine ? "" : ", ", List[i].KD, List[i].Name);

		if(str_length(Line) + str_length(aPart) >= 56)
		{
			pContext->SendChatTarget(pClientID, Line);
			str_format(Line, sizeof(Line), "%.3f (%s)", List[i].KD, List[i].Name);
			FirstInLine = false;
		}
		else
		{
			str_append(Line, aPart, sizeof(Line));
			FirstInLine = false;
		}
	}
	if(Line[0])
		pContext->SendChatTarget(pClientID, Line);
}

void FormatTime(int Seconds, char* pBuf, int BufSize)
{
	int Hours = Seconds / 3600;
	int Minutes = (Seconds % 3600) / 60;
	int Secs = Seconds % 60;

	if(Hours > 0)
		str_format(pBuf, BufSize, "%dh %dm %ds", Hours, Minutes, Secs);
	else if(Minutes > 0)
		str_format(pBuf, BufSize, "%dm %ds", Minutes, Secs);
	else
		str_format(pBuf, BufSize, "%ds", Secs);
}

void CGameContext::CmdTopStats(CGameContext *pContext, int pClientID, const char **pArgs, int ArgNum)
{
	if (!pContext->CheckStatCommandCooldown(pClientID))
		return;

	struct Entry
	{
		char Name[MAX_NAME_LENGTH];
		int Kills, Deaths, Shots, Freezes, Spree, Multi, Steals, Wallshots, Saves, Green, Gold, Purple, Wrong, TimeFrozen;
	};

	std::vector<Entry> List;

	IOHANDLE File = io_open("stats.db", IOFLAG_READ);
	if (!File)
	{
		pContext->SendChatTarget(pClientID, "Could not open stats.db");
		return;
	}

	char aBuf[512];
	int Pos = 0;
	char c;

	while (io_read(File, &c, 1))
	{
		if (c == '\n' || Pos >= (int)(sizeof(aBuf) - 1))
		{
			aBuf[Pos] = 0;
			char aName[MAX_NAME_LENGTH];
			int D[18];

			if (sscanf(aBuf,
				"%32[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
				aName,
				&D[0], &D[1], &D[2], &D[3], &D[4], &D[5], &D[6], &D[7], &D[8],
				&D[9], &D[10], &D[11], &D[12], &D[13], &D[14], &D[15], &D[16], &D[17]) == 19)
			{
				Entry e;
				str_copy(e.Name, aName, sizeof(e.Name));
				e.Kills = D[0];
				e.Deaths = D[1];
				e.Shots = D[2];
				e.Freezes = D[5];
				e.Saves = D[8];
				e.Wallshots = D[4];
				e.Steals = D[14];
				e.Green = D[10];
				e.Gold = D[11];
				e.Purple = D[12];
				e.Wrong = D[13];
				e.Spree = D[16];
				e.Multi = D[17];
				e.TimeFrozen = D[7];
				List.push_back(e);
			}
			Pos = 0;
		}
		else aBuf[Pos++] = c;
	}
	io_close(File);

	if (List.empty())
	{
		pContext->SendChatTarget(pClientID, "No data found in stats.db");
		return;
	}

	auto FindBest = [](std::vector<Entry>& list, bool (*comp)(Entry&, Entry&)) -> Entry*
	{
		Entry* Best = nullptr;
		for (auto& e : list)
		{
			if (!Best || comp(e, *Best))
				Best = &e;
		}
		return Best;
	};

	char aLine[256];

	Entry* KDR = FindBest(List, [](Entry& a, Entry& b) {
		if(a.Kills < 100) return false;
		if(b.Kills < 100) return true;
		float akd = a.Deaths > 0 ? (float)a.Kills / a.Deaths : a.Kills;
		float bkd = b.Deaths > 0 ? (float)b.Kills / b.Deaths : b.Kills;
		return akd > bkd;
	});
	if (KDR && KDR->Kills >= 100) {
		float Ratio = KDR->Deaths > 0 ? (float)KDR->Kills / KDR->Deaths : (float)KDR->Kills;
		str_format(aLine, sizeof(aLine), "Best K/D: %.3f (%s)", Ratio, KDR->Name);
		pContext->SendChatTarget(pClientID, aLine);
	}

	Entry* Acc = FindBest(List, [](Entry& a, Entry& b) {
		float aa = (a.Shots >= 500) ? (float)a.Freezes / a.Shots : -1.0f;
		float bb = (b.Shots >= 500) ? (float)b.Freezes / b.Shots : -1.0f;
		return aa > bb;
	});
	if (Acc && Acc->Shots >= 500) {
		float Accuracy = (float)Acc->Freezes / Acc->Shots * 100.0f;
		str_format(aLine, sizeof(aLine), "Best Accuracy: %.3f%% (%s)", Accuracy, Acc->Name);
		pContext->SendChatTarget(pClientID, aLine);
	}

	#define BEST_STAT(stat, label) \
		{ Entry* e = FindBest(List, [](Entry& a, Entry& b) { return a.stat > b.stat; }); \
		if (e && e->stat > 0) { str_format(aLine, sizeof(aLine), label " %d (%s)", e->stat, e->Name); pContext->SendChatTarget(pClientID, aLine); } }

	BEST_STAT(Spree, "Best Spree:");
	BEST_STAT(Multi, "Best Multi:");
	BEST_STAT(Steals, "Most Steals:");
	BEST_STAT(Wallshots, "Most Wallshots:");
	BEST_STAT(Saves, "Most Saves:");
	BEST_STAT(Green, "Most Green Spikes:");
	BEST_STAT(Gold, "Most Gold Spikes:");
	BEST_STAT(Purple, "Most Purple Spikes:");
	BEST_STAT(Wrong, "Most Wrong Shrine Kills:");

	Entry* Frozen = FindBest(List, [](Entry& a, Entry& b) { return a.TimeFrozen > b.TimeFrozen; });
	if (Frozen && Frozen->TimeFrozen > 0) {
		int Hours = Frozen->TimeFrozen / 3600;
        int Minutes = (Frozen->TimeFrozen % 3600) / 60;
        int Remainder = Frozen->TimeFrozen % 60;
        str_format(aLine, sizeof(aLine), "Longest Time Frozen: %dh %dm %ds (%s)", Hours, Minutes, Remainder, Frozen->Name);
		pContext->SendChatTarget(pClientID, aLine);
	}
}

void CGameContext::CmdEarrape(CGameContext* pContext, int ClientID, const char** pArgs, int ArgNum)
{
	CPlayer* pPlayer = pContext->m_apPlayers[ClientID];
	if(!pPlayer || !pPlayer->GetCharacter())
		return;

	int64_t Now = pContext->Server()->Tick();
	int64_t CooldownTicks = 30 * 60 * pContext->Server()->TickSpeed(); // 30 minutes

	if(Now < pContext->m_LastEarrapeTick + CooldownTicks)
	{
		int SecondsLeft = (pContext->m_LastEarrapeTick + CooldownTicks - Now) / pContext->Server()->TickSpeed();
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Earrape cooldown: %d minute%s left", SecondsLeft / 60, (SecondsLeft / 60 == 1 ? "" : "s"));
		pContext->SendChatTarget(ClientID, aBuf);
		return;
	}

	pContext->m_LastEarrapeTick = Now;

	// Replace with actual menu music sound once confirmed
	vec2 Pos = pPlayer->GetCharacter()->m_Pos;
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
	pContext->CreateSound(Pos, SOUND_MENU, CmaskAll());
}

IGameServer *CreateGameServer() { return new CGameContext; }
