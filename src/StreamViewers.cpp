#include "StreamViewers.h"
#include <string.h>
#include <algorithm>

bool isSpecCamera(const edict_t& ed) {
	return !ed.free && ed.v.classname && !strcmp(STRING(ed.v.classname), SPEC_CAMERA_CLASS);
}

#ifdef HLCOOP_BUILD
#include "rehlds.h"
#include "SvenTV.h"
#include "PluginManager.h"
#include "playerdb.h"
#include <unordered_set>

#define WEB_SPEC_TEAM "Web Spectators"
#define WEB_SPEC_STATUS 2 // shown as SPEC on the SevenKewp scoreboard
#define WEB_SPEC_COLOR 4 // green on the scoreboard (FRIEND_TEAM_COLOR)
#define SCOREBOARD_UPDATE_SECONDS 1.0f
#define MAX_SLOT_NAME 31

extern SvenTV* g_sventv;
extern bool g_pause_message_logging;

PluginFuncHandle<const char*, uint64_t> g_func_web_name("HLCoop", "get_web_name_for_id");

class CHltvCamera : public CBaseEntity {
public:
	void Precache() {
		PRECACHE_MODEL(SPEC_CAMERA_MODEL);
	}

	void Spawn() {
		Precache();
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->effects |= EF_NODRAW;
		SET_MODEL(edict(), SPEC_CAMERA_MODEL);
		UTIL_SetSize(pev, Vector(-8, -8, -8), Vector(8, 8, 8));
	}

	int AddToFullPack(struct entity_state_s* state, CBasePlayer* player) {
		state->movetype = MOVETYPE_NOCLIP; // enable interpolation on the client
		return 1;
	}
};

LINK_ENTITY_TO_CLASS(hltv_camera, CHltvCamera)

// a stream viewer who logged in with a website token
struct WebViewer {
	uint64_t clientId; // stream client
	uint64_t steamid64;
	std::string name;
	uint8_t topColor = 0;
	uint8_t bottomColor = 0;
	uint32_t delaySec = 0;
	EHANDLE h_camera;
};

static std::vector<WebViewer> g_viewers;
static std::vector<StreamViewer> g_sentViewers; // last list given to the stream
static int g_passwordViewers = 0;

// players who turned the cameras on, by Steam ID so it lasts through map changes
static std::unordered_set<uint64_t> g_showSpecs;

// scoreboard names sent to each player for each empty slot. [receiver][slot], "" = nothing sent
static std::string g_sentSlotNames[MAX_PLAYERS + 1][MAX_PLAYERS + 1];
static int g_receiverUserIds[MAX_PLAYERS + 1];
static float g_lastScoreboardUpdate = 0;

void streamViewersMapInit() {
	UTIL_PrecacheOther(SPEC_CAMERA_CLASS);
	g_viewers.clear();
	g_sentViewers.clear();
	g_lastScoreboardUpdate = 0;

	// clients forget all player info when the map changes
	for (int i = 0; i <= MAX_PLAYERS; i++) {
		for (int k = 0; k <= MAX_PLAYERS; k++) {
			g_sentSlotNames[i][k].clear();
		}
	}
}

void streamViewersMapEnd() {
	g_viewers.clear();
}

// rounded, so names that include it don't change constantly
static uint32_t roundDelay(uint32_t delayMs) {
	uint32_t s = delayMs / 1000;
	if (s < 5) {
		return 0; // live
	}
	if (s < 60) {
		return s - s % 5;
	}
	if (s < 60 * 60) {
		return s - s % 60;
	}
	return s - s % (60 * 60);
}

static std::string delaySuffix(uint32_t delaySec) {
	if (!delaySec) {
		return "";
	}
	if (delaySec < 60) {
		return UTIL_VarArgs(" (%us ago)", delaySec);
	}
	if (delaySec < 60 * 60) {
		return UTIL_VarArgs(" (%um ago)", delaySec / 60);
	}
	return UTIL_VarArgs(" (%uh ago)", delaySec / (60 * 60));
}

static void loadViewerInfo(WebViewer& viewer) {
	const char* name = g_func_web_name.call(viewer.steamid64);
	viewer.name = name && name[0] ? name : steamid64_to_steamid(viewer.steamid64);

	player_data_t* data = playerdb_load_state(viewer.steamid64);
	if (data) {
		viewer.topColor = data->topcolor;
		viewer.bottomColor = data->bottomcolor;
	}
}

static void updateCamera(WebViewer& viewer, const StreamClientInfo& client, uint32_t showMask) {
	CBaseEntity* cam = viewer.h_camera.GetEntity();

	// viewers whose player hasn't sent where it's looking from don't get a camera
	if (!client.joined || !client.hasCamera) {
		if (cam) {
			UTIL_Remove(cam);
			viewer.h_camera = NULL;
		}
		return;
	}

	if (!cam) {
		cam = CBaseEntity::Create(SPEC_CAMERA_CLASS, g_vecZero, g_vecZero);
		if (!cam) {
			ALERT(at_error, "HLTV: failed to create a spectator camera\n");
			return;
		}
		cam->pev->colormap = viewer.topColor | (viewer.bottomColor << 8);
		viewer.h_camera = cam;
	}

	Vector origin(client.camOrigin[0], client.camOrigin[1], client.camOrigin[2]);
	UTIL_SetOrigin(cam->pev, origin);
	// model pitch is the opposite of view pitch
	cam->pev->angles = Vector(-client.camAngles[0], client.camAngles[1], 0);

	// nobody sees EF_NODRAW entities, but the demo writer records cameras anyway
	if (showMask) {
		cam->pev->effects &= ~EF_NODRAW;
	}
	else {
		cam->pev->effects |= EF_NODRAW;
	}
	cam->m_hidePlayers = ~showMask;
}

static bool isSlotEmpty(int idx) {
	if (UTIL_PlayerByIndex(idx)) {
		return false;
	}
	if (g_RehldsSvs) {
		// also catches players who are still connecting
		IGameClient* client = g_RehldsSvs->GetClient(idx - 1);
		return client && !client->IsConnected();
	}
	char* info = g_engfuncs.pfnGetInfoKeyBuffer(INDEXENT(idx));
	return !info || !info[0];
}

static std::string slotName(std::string name) {
	// these would break the userinfo string
	std::replace(name.begin(), name.end(), '\\', '/');
	std::replace(name.begin(), name.end(), '"', '\'');
	if (name.size() > MAX_SLOT_NAME) {
		name = name.substr(0, MAX_SLOT_NAME);
	}
	return name;
}

// shows a fake player in an empty slot on a player's scoreboard, or removes it if name is empty
static void sendSlot(CBasePlayer* to, int slot, const std::string& name) {
	g_pause_message_logging = true; // not part of the demo

	std::string info = name.empty() ? "" : "\\name\\" + name + "\\model\\player\\topcolor\\0\\bottomcolor\\0";
	UTIL_SendUserInfo(to->edict(), INDEXENT(slot), (char*)info.c_str());

	if (!name.empty()) {
		MESSAGE_BEGIN(MSG_ONE, g_umsg.ScoreInfo, NULL, to->edict());
		WRITE_BYTE(slot);
		WRITE_SHORT(0); // score
		WRITE_SHORT(0); // deaths
		WRITE_SHORT(WEB_SPEC_STATUS);
		WRITE_SHORT(WEB_SPEC_COLOR);
		MESSAGE_END();
	}

	MESSAGE_BEGIN(MSG_ONE, g_umsg.TeamInfo, NULL, to->edict());
	WRITE_BYTE(slot);
	WRITE_STRING(name.empty() ? "" : WEB_SPEC_TEAM);
	MESSAGE_END();

	g_pause_message_logging = false;
}

// Lists the viewers in empty player slots. The highest slots are used so that the team sorts below
// the players when scores are tied. If the server is full, nothing is shown.
static void updateScoreboard() {
	bool empty[MAX_PLAYERS + 1] = { false };
	std::vector<int> emptySlots;
	for (int i = 1; i <= gpGlobals->maxClients && i <= MAX_PLAYERS; i++) {
		empty[i] = isSlotEmpty(i);
		if (empty[i]) {
			emptySlots.push_back(i);
		}
	}

	std::vector<std::string> names;
	for (const WebViewer& viewer : g_viewers) {
		names.push_back(viewer.name + delaySuffix(viewer.delaySec));
	}

	std::string wanted[MAX_PLAYERS + 1];
	int numSlots = emptySlots.size();
	int numNamed = names.size();
	int total = numNamed + g_passwordViewers;

	if (numSlots && total) {
		bool allFit = numNamed <= numSlots && (g_passwordViewers == 0 || numNamed < numSlots);
		int shown = allFit ? numNamed : numSlots - 1;
		int more = total - shown;
		int first = numSlots - (shown + (more > 0 ? 1 : 0));

		for (int i = 0; i < shown; i++) {
			wanted[emptySlots[first + i]] = slotName(names[i]);
		}
		if (more > 0) {
			wanted[emptySlots[first + shown]] = UTIL_VarArgs("(+%d more viewer%s)", more, more == 1 ? "" : "s");
		}
	}

	for (int r = 1; r <= gpGlobals->maxClients && r <= MAX_PLAYERS; r++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(r);
		int userid = plr ? g_engfuncs.pfnGetPlayerUserId(plr->edict()) : -1;

		if (userid != g_receiverUserIds[r]) {
			// a different player, who hasn't been sent anything
			g_receiverUserIds[r] = userid;
			for (int s = 0; s <= MAX_PLAYERS; s++) {
				g_sentSlotNames[r][s].clear();
			}
		}

		if (!plr || (plr->pev->flags & FL_FAKECLIENT) || plr->m_fInitHUD) {
			continue;
		}

		for (int s = 1; s <= gpGlobals->maxClients && s <= MAX_PLAYERS; s++) {
			std::string& sent = g_sentSlotNames[r][s];
			if (!empty[s]) {
				sent.clear(); // a player took the slot, and the engine sent their info
				continue;
			}
			if (sent != wanted[s]) {
				sendSlot(plr, s, wanted[s]);
				sent = wanted[s];
			}
		}
	}
}

void updateStreamViewers() {
	std::vector<StreamClientInfo> clients;
	if (g_sventv && g_sventv->enableStream) {
		clients = g_sventv->getStreamClients();
	}

	// Viewers who used the stream password have no Steam ID, so they're only counted
	g_passwordViewers = 0;
	for (const StreamClientInfo& client : clients) {
		g_passwordViewers += client.authed && !client.steamid64;
	}
	clients.erase(std::remove_if(clients.begin(), clients.end(), [](const StreamClientInfo& c) {
		return !c.authed || !c.steamid64;
	}), clients.end());

	g_viewers.erase(std::remove_if(g_viewers.begin(), g_viewers.end(), [&](WebViewer& viewer) {
		bool watching = std::any_of(clients.begin(), clients.end(), [&](const StreamClientInfo& c) {
			return c.id == viewer.clientId;
		});
		if (!watching) {
			UTIL_Remove(viewer.h_camera.GetEntity());
		}
		return !watching;
	}), g_viewers.end());

	uint32_t showMask = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(i);
		if (plr && g_showSpecs.count(plr->GetSteamID64())) {
			showMask |= PLRBIT(i);
		}
	}

	for (const StreamClientInfo& client : clients) {
		WebViewer* viewer = NULL;
		for (WebViewer& v : g_viewers) {
			if (v.clientId == client.id) {
				viewer = &v;
				break;
			}
		}

		if (!viewer) {
			WebViewer newViewer;
			newViewer.clientId = client.id;
			newViewer.steamid64 = client.steamid64;
			loadViewerInfo(newViewer);
			g_viewers.push_back(newViewer);
			viewer = &g_viewers.back();
		}

		viewer->delaySec = client.hasCamera ? roundDelay(client.delayMs) : 0;
		updateCamera(*viewer, client, showMask);
	}

	std::vector<StreamViewer> list;
	for (WebViewer& viewer : g_viewers) {
		CBaseEntity* cam = viewer.h_camera.GetEntity();
		StreamViewer sv;
		sv.clientId = viewer.clientId;
		sv.entindex = cam ? cam->entindex() : 0;
		sv.topColor = viewer.topColor;
		sv.bottomColor = viewer.bottomColor;
		sv.delaySec = viewer.delaySec;
		sv.name = viewer.name;
		list.push_back(sv);
	}
	if (g_sventv && list != g_sentViewers) {
		g_sventv->setStreamViewers(list);
		g_sentViewers = list;
	}

	if (gpGlobals->time - g_lastScoreboardUpdate >= SCOREBOARD_UPDATE_SECONDS || g_lastScoreboardUpdate > gpGlobals->time) {
		g_lastScoreboardUpdate = gpGlobals->time;
		updateScoreboard();
	}
}

bool cmd_showspecs(CBasePlayer* plr, const CommandArgs& args) {
	if (!plr) {
		return false;
	}

	uint64_t steamid64 = plr->GetSteamID64();
	bool show = !g_showSpecs.count(steamid64);
	if (args.ArgC() > 1) {
		show = atoi(args.ArgV(1).c_str()) != 0;
	}

	if (show) {
		g_showSpecs.insert(steamid64);
		UTIL_ClientPrint(plr, print_chat, "[HLTV] Showing the cameras of stream viewers.\n");
	}
	else {
		g_showSpecs.erase(steamid64);
		UTIL_ClientPrint(plr, print_chat, "[HLTV] Hiding the cameras of stream viewers.\n");
	}

	return true;
}
#endif
