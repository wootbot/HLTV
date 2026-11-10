#pragma once
// SevenKewp's stable branch (version 0.4) uses older names for some of the server APIs that
// HLTV uses. This adds the newer names, so this branch builds against stable and master.
#ifdef HLCOOP_BUILD
#include "util.h"
#include "user_messages.h"

#if SEVENKEWP_VERSION_MAJOR == 0 && SEVENKEWP_VERSION_MINOR < 5

// Convert floats to/from signed fixed-point integers.
// Use this when the number of bits needed in the fixed-point representation is less than a standard type.
// For example, if you want 24 bits instead of 32 (part of int32), or 12 instead of 16 (part of int16).
// Simpler conversion logic can be used otherwise.
// result will be sign-extended to fit an int32_t.
// values that don't fit in the specified number of bits will be clamped.
inline int32_t FLOAT_TO_FIXED(float x, int whole_bits, int frac_bits) {
	int32_t maxVal = ((1 << (whole_bits + frac_bits - 1)) - 1);
	int32_t minVal = -(maxVal + 1);
	int32_t r = clampf(x, minVal, maxVal) * (1 << frac_bits);
	return r;
}
// sign extend a fixed point value that was stored in an unsigned integer, which may have 0 in its higher bits
inline int32_t SIGN_EXTEND_FIXED(uint32_t x, int total_bits) {
	uint32_t b = total_bits;
	int m = 1U << (b - 1); // sign bit mask
	x = x & ((1U << b) - 1); // remove sign bit
	return (x ^ m) - m; // sign extended version of x
}
// x should not be sign extended to fit the int32_t it was stored in. That will be done in this function
inline float FIXED_TO_FLOAT(int x, int whole_bits, int frac_bits) {
	// https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
	uint32_t b = whole_bits + frac_bits;
	int m = 1U << (b - 1); // sign bit mask

	x = x & ((1U << b) - 1); // remove sign bit
	int r = (x ^ m) - m; // sign extended version of x

	return r / (float)(1 << frac_bits);
}

// compares two fixed point integers for equality, where each may or may not be sign extended
inline bool FIXED_EQUALS(int x, int y, int totalBits) {
	int m = (1 << totalBits) - 1; // mask of value without extended sign
	return (x & m) == (y & m);
}

#define UTIL_CreateFolder createFolder
#define UTIL_FolderExists folderExists

// the user message ids HLTV uses, which stable keeps in gmsg* variables instead of g_umsg
struct HltvUserMessageIds {
	int SayText;
	int ScoreInfo;
	int TeamInfo;
	int DeathMsg;
	int StatusText;
	int StatusValue;
};

inline HltvUserMessageIds hltv_user_message_ids() {
	return { gmsgSayText, gmsgScoreInfo, gmsgTeamInfo, gmsgDeathMsg, gmsgStatusText, gmsgStatusValue };
}

#define g_umsg hltv_user_message_ids()

#endif
#endif
