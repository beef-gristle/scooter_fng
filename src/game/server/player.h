/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"

#include <string.h>

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	virtual ~CPlayer();
    bool m_SentRagequitMessage = false;
    CCharacter *DetachCharacter()
    {
        CCharacter *p = m_pCharacter;
        m_pCharacter = nullptr;
        return p;
    }

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	bool m_SpawnFrozen = false; // Freeze on next spawn (e.g. for false spike)
	//doesnt kill the character
	void SetTeamSilent(int Team);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };
    
	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);

	void KillCharacter(int Weapon, bool forced = false);
	CCharacter *GetCharacter();

	virtual void ResetStats();

	//client version
	char m_ClientVersion;
	int m_DDNetVersion;

	int m_UnknownPlayerFlag;
	
	struct sPlayerStats{
		//core stats
		int m_NumJumped;
		float m_NumTilesMoved;
		int m_NumHooks;
		float m_MaxSpeed;
		int m_NumTeeCollisions;
		//other stats
		int m_NumFreezeTicks;
		int m_NumEmotes;		
	
		//score things
		int m_Kills; //kills made by weapon
		int m_GrabsNormal; //kills made by grabbing oponents into spikes - normal spikes
		int m_GrabsTeam; //kills made by grabbing oponents into spikes - team spikes
		int m_GrabsFalse; //kills made by grabbing oponents into spikes - oponents spikes
		int m_GrabsGold; //kills made by grabbing oponents into spikes - golden spikes
		int m_GrabsGreen; //kills made by grabbing oponents into spikes - for non 4-teams fng green spikes
		int m_GrabsPurple; //kills made by grabbing oponents into spikes - for non 4-teams fng purple spikes
		int m_Deaths; //death by spikes -- we don't make a difference of the spike types here
		int m_Hits; //hits by oponents weapon
		int m_Selfkills;
		int m_Teamkills;
		int m_Unfreezes; //number of actual unfreezes of teammates
		int m_UnfreezingHammerHits; //number of hammers to a frozen teammate
		
		int m_Shots; //the shots a player made
	} m_Stats;

	enum eClientVersion {
		CLIENT_VERSION_NORMAL,
		CLIENT_VERSION_DDNET,
	};

	enum {
		VANILLA_CLIENT_MAX_CLIENTS = 16,
		DDNET_CLIENT_MAX_CLIENTS = 64,
	};

	//adds or updates client this clients is snapping from
	bool AddSnappingClient(int RealID, float Distance, char ClientVersion, int& pId);
	//look if a snapped client is a client, this client is snapping from
	bool IsSnappingClient(int RealID, char ClientVersion, int& id);
	int GetRealIDFromSnappingClients(int SnapID);
	void FakeSnap(int PlayerID = (VANILLA_CLIENT_MAX_CLIENTS - 1));

	//the clients this clients is snapping from
	struct {
		float distance;
		int id;
	} m_SnappingClients[DDNET_CLIENT_MAX_CLIENTS];

	//A Player we are whispering to
	struct sWhisperPlayer {
		sWhisperPlayer() : PlayerID(-1) {
			memset(PlayerName, 0, sizeof(PlayerName));
		}
		int PlayerID;
		char PlayerName[16];
	} m_WhisperPlayer;

	//how often did we spam
	int m_ChatSpamCount;
	
	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;
    vec2 m_ViewVel;       // view velocity, updated every tick
    vec2 m_LastViewPos;   // last tick's view pos

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;
	
	int m_Emotion;
	long long m_EmotionDuration;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;

	// TODO: clean this up
	struct
	{
		char m_SkinName[64];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;
    
    inline void AttachCharacter(CCharacter *pChr)   { m_pCharacter = pChr; }
    bool  IsLocalDummy{false};
    
    int64 m_MuteTick;
    int m_ToxicStrikes;
    bool m_ResetStrikesAfterMute;
    int m_MultiCount = 0;      // current ongoing multi kill streak
    int m_LastKillTick = -1;   // tick when last kill occurred
    int m_MaxMulti = 0;        // highest multi during round
    int m_MultiEndAnnounceTick = -1; // future tick to announce "multi ended"
    int m_CurrentSpree = 0;
    int m_MaxSpree = 0;
    int m_Steals = 0;
    int m_StolenFrom = 0;
    int m_Kills = 0;
    int m_Deaths = 0;
    int m_Shots = 0;
    int m_Misses = 0;
    int m_Wallshots = 0;
    int m_Freezes = 0;
    int m_Frozen = 0;
    int m_TimeFrozen = 0;
    int m_Saves = 0;
    int m_SavedBy = 0;
    int m_GreenSpikeKills = 0;
    int m_GoldSpikeKills = 0;
    int m_PurpleSpikeKills = 0;
    int m_WrongShrineKills = 0;
    
    int m_RoundKills = 0;
	int m_RoundDeaths = 0;
	int m_RoundShots = 0;
	int m_RoundMisses = 0;
	int m_RoundWallshots = 0;
	int m_RoundFreezes = 0;
	int m_RoundFrozen = 0;
	int m_RoundTimeFrozen = 0;
	int m_RoundSaves = 0;
	int m_RoundSavedBy = 0;
	int m_RoundGreenSpikeKills = 0;
	int m_RoundGoldSpikeKills = 0;
	int m_RoundPurpleSpikeKills = 0;
	int m_RoundWrongShrineKills = 0;
	int m_RoundSteals = 0;
	int m_RoundStolenFrom = 0;
	int m_RoundMaxSpree = 0;
	int m_RoundMaxMulti = 0;
    
    char m_aSavedName[MAX_NAME_LENGTH];
    
    void ResetRoundStats();
    int m_LastCommandRequestTick;
    
    int m_EyeEmote = EMOTE_NORMAL;       // the selected eye emote
    int m_EyeEmoteDuration = -1;         // -1 means infinite
    int m_DefaultEyeEmote = EMOTE_NORMAL;
    char m_aOriginalName[MAX_NAME_LENGTH];
private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
	
	void CalcScore();
};

#endif
