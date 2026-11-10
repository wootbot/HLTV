#pragma once
#include "extdll.h"
#include "util.h"
#include "PluginManager.h"
#include <unordered_set>

// either one of these conditions will trigger a "map played" stat
#define MIN_MAP_PLAY_TIME 3.0f // minutes needed to trigger a map play stat
#define MIN_MAP_PLAY_PERCENT 0.5f // percentage of map time needed to trigger a map play stat
#define MIN_MAP_DURATION 10.0f // map needs to run at least this many seconds to collect stats

#define MAX_ALIASES 10

// Increment whenever structures or functions change, then copy this header to every dependent plugin
#define PLAYERDB_API_VERSION 1

// cross-plugin data
#pragma pack(push,1)
enum MAP_RATING {
	RATING_NONE,
	RATING_LIKE,
	RATING_DISLIKE,
	RATING_FAVORITE
};
struct map_play_stat_t {
	uint32_t lastPlay;
	uint16_t totalPlays;
	uint8_t rating;
};
struct mute_item_t {
	uint64_t steamid;
	uint16_t muteflags;
};
struct alias_t {
	char name[32];
	uint32_t timeUsed; // seconds this name was used for
	uint16_t firstUsed; // days since epoch
	uint16_t lastUsed; // days since epoch
};
// POD shared with database and memory cache
struct player_data_t {
	// general player details and statistics
	char currentname[32];
	alias_t aliases[MAX_ALIASES];
	char steamname[64];
	char steamavatar[64];
	uint32_t lastSteamDetailsFetch; // seconds since epoch
	uint32_t totalPlayTime; // seconds played on the server
	uint32_t recentPlayTime; // seconds played on the server since 2 weeks ago
	uint32_t firstSeen; // seconds since epoch
	uint32_t lastSeen; // seconds since epoch

	// map or sesion related data
	uint32_t currentPlayTime; // seconds played on current map
	uint32_t lastJoinTime; // seconds since epoch
	int lastSeriesPlay; // last map series a play stat was recorded for (for series maps stats)

	// Speedometer plugin
	bool speedometerEnabled;

	// Translator plugin
	char language[16];
	bool showTranslations; // show what your chat messages are being translated to

	// ChatSounds plugin
	int cs_vol; // cs volume
	bool cs_reliableMode;
	bool cs_globalMode;

	// PlayerModel plugin
	char currentmodel[23];
	uint8_t topcolor;
	uint8_t bottomcolor;
	bool disablePlayerModelDownloads;

	// misc
	uint8_t autoskip; // 0 = disabled, 1 = skip all cutscenes automatically
	uint8_t likeMapDelay; // how many days before a liked map can be replayed (0 = server default)
};
#pragma pack(pop)

#define PLAYERDB_PLUGIN_NAME "PlayerDB"

// The following funcs fetch a state from the database if it exists, else initializes a new state.
// Subsuquent calls for the same ID are cached and don't read from the database.
// Returned data can be modified only within the calling plugin's function.
// Functions are not thread safe.

inline player_data_t* playerdb_load_state(uint64_t steamid64) {
	static PluginFuncHandle<player_data_t*, uint64_t, int> g_api_playerdb_load_state(PLAYERDB_PLUGIN_NAME, "api_playerdb_load_state_data");
	return g_api_playerdb_load_state.call(steamid64, PLAYERDB_API_VERSION);
}

inline map_play_stat_t* playerdb_load_map_stats(uint64_t steamid64, const char* mapname) {
	static PluginFuncHandle<map_play_stat_t*, uint64_t, const char*, int> g_api_playerdb_load_map_stats(PLAYERDB_PLUGIN_NAME, "api_playerdb_load_map_stats");
	return g_api_playerdb_load_map_stats.call(steamid64, mapname, PLAYERDB_API_VERSION);
}

inline void playerdb_load_map_stats_multi(uint64_t steamid64, const char** mapnames, int numMaps, map_play_stat_t* outStats) {
	static PluginFuncHandle<void, uint64_t, const char**, int, map_play_stat_t*, int> g_api_playerdb_load_map_stats_multi(PLAYERDB_PLUGIN_NAME, "api_playerdb_load_map_stats_multi");
	return g_api_playerdb_load_map_stats_multi.call(steamid64, mapnames, numMaps, outStats, PLAYERDB_API_VERSION);
}

inline uint16_t* playerdb_load_mute_bits(uint64_t listener, uint64_t speaker) {
	static PluginFuncHandle<uint16_t*, uint64_t, uint64_t> g_api_playerdb_load_mute_bits(PLAYERDB_PLUGIN_NAME, "api_playerdb_load_mute_bits");
	return g_api_playerdb_load_mute_bits.call(listener, speaker);
}

inline int playerdb_load_mutes(uint64_t listener, mute_item_t* items, int maxItems) {
	static PluginFuncHandle<int, uint64_t, mute_item_t*, int> g_api_playerdb_load_mutes(PLAYERDB_PLUGIN_NAME, "api_playerdb_load_mutes");
	return g_api_playerdb_load_mutes.call(listener, items, maxItems);
}

inline void playerdb_get_loaded_states(std::vector<uint64_t>& keys) {
	// TODO: Track which states were loaded in the calling plugin instead
	static PluginFuncHandle<void, std::vector<uint64_t>&> g_api_playerdb_get_loaded_states(PLAYERDB_PLUGIN_NAME, "api_playerdb_get_loaded_states");
	g_api_playerdb_get_loaded_states.call(keys);
}

inline void playerdb_get_steamids_for_ip(uint32_t ip, std::unordered_set<uint64_t>& steamids) {
	static PluginFuncHandle<void, uint32_t, std::unordered_set<uint64_t>&> g_api_playerdb_get_steamids_for_ip(PLAYERDB_PLUGIN_NAME, "api_playerdb_get_steamids_for_ip");
	g_api_playerdb_get_steamids_for_ip.call(ip, steamids);
}
