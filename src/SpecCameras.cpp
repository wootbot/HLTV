#include "SpecCameras.h"
#include <string.h>
#include <algorithm>

bool isSpecCamera(const edict_t& ed) {
	return !ed.free && ed.v.classname && !strcmp(STRING(ed.v.classname), SPEC_CAMERA_CLASS);
}

#ifdef HLCOOP_BUILD
#include "SvenTV.h"
#include "PluginManager.h"
#include "playerdb.h"
#include <unordered_set>

extern SvenTV* g_sventv;

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

struct SpecCamera {
	uint64_t clientId; // stream client
	uint64_t steamid64;
	std::string name;
	EHANDLE h_ent;
};

static std::vector<SpecCamera> g_specCameras;

// players who turned the cameras on, by Steam ID so it lasts through map changes
static std::unordered_set<uint64_t> g_showSpecs;

void specCamerasMapInit() {
	UTIL_PrecacheOther(SPEC_CAMERA_CLASS);
	g_specCameras.clear();
}

void specCamerasMapEnd() {
	g_specCameras.clear();
}

static std::string getViewerName(uint64_t steamid64) {
	const char* name = g_func_web_name.call(steamid64);
	return name && name[0] ? name : steamid64_to_steamid(steamid64);
}

static CHltvCamera* createCamera(SpecCamera& cam) {
	CBaseEntity* ent = CBaseEntity::Create(SPEC_CAMERA_CLASS, g_vecZero, g_vecZero);
	if (!ent) {
		ALERT(at_error, "HLTV: failed to create a spectator camera\n");
		return NULL;
	}
	CHltvCamera* camEnt = (CHltvCamera*)ent;

	player_data_t* data = playerdb_load_state(cam.steamid64);
	if (data) {
		camEnt->pev->colormap = data->topcolor | (data->bottomcolor << 8);
	}

	cam.name = getViewerName(cam.steamid64);
	cam.h_ent = camEnt;
	return camEnt;
}

void updateSpecCameras() {
	std::vector<StreamClientInfo> clients;
	if (g_sventv && g_sventv->enableStream) {
		clients = g_sventv->getStreamClients();
	}

	// Viewers who used the stream password have no Steam ID, so they don't get a camera. Neither do
	// viewers whose player hasn't sent a camera position yet.
	clients.erase(std::remove_if(clients.begin(), clients.end(), [](const StreamClientInfo& c) {
		return !c.joined || !c.hasCamera || !c.steamid64;
	}), clients.end());

	bool changed = false;

	g_specCameras.erase(std::remove_if(g_specCameras.begin(), g_specCameras.end(), [&](SpecCamera& cam) {
		bool watching = std::any_of(clients.begin(), clients.end(), [&](const StreamClientInfo& c) {
			return c.id == cam.clientId;
		});
		if (watching && cam.h_ent) {
			return false;
		}
		UTIL_Remove(cam.h_ent.GetEntity());
		changed = true;
		return true;
	}), g_specCameras.end());

	uint32_t showMask = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(i);
		if (plr && g_showSpecs.count(plr->GetSteamID64())) {
			showMask |= PLRBIT(i);
		}
	}

	for (const StreamClientInfo& client : clients) {
		SpecCamera* cam = NULL;
		for (SpecCamera& c : g_specCameras) {
			if (c.clientId == client.id) {
				cam = &c;
				break;
			}
		}

		CBaseEntity* ent = cam ? cam->h_ent.GetEntity() : NULL;
		if (!cam) {
			SpecCamera newCam;
			newCam.clientId = client.id;
			newCam.steamid64 = client.steamid64;
			ent = createCamera(newCam);
			if (!ent) {
				continue;
			}
			g_specCameras.push_back(newCam);
			changed = true;
		}

		Vector origin(client.camOrigin[0], client.camOrigin[1], client.camOrigin[2]);
		UTIL_SetOrigin(ent->pev, origin);
		// model pitch is the opposite of view pitch
		ent->pev->angles = Vector(-client.camAngles[0], client.camAngles[1], 0);

		// nobody sees EF_NODRAW entities, but the demo writer records cameras anyway
		if (showMask) {
			ent->pev->effects &= ~EF_NODRAW;
		}
		else {
			ent->pev->effects |= EF_NODRAW;
		}
		ent->m_hidePlayers = ~showMask;
	}

	if (changed && g_sventv) {
		std::vector<StreamCamera> list;
		for (SpecCamera& cam : g_specCameras) {
			CBaseEntity* ent = cam.h_ent.GetEntity();
			if (!ent) {
				continue;
			}
			StreamCamera sc;
			sc.clientId = cam.clientId;
			sc.entindex = ent->entindex();
			sc.topColor = ent->pev->colormap & 0xFF;
			sc.bottomColor = (ent->pev->colormap >> 8) & 0xFF;
			sc.name = cam.name;
			list.push_back(sc);
		}
		g_sventv->setStreamCameras(list);
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
