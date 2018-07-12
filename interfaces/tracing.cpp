#include "../sdk/framework/interfaces/tracing.h"
#include "../core/fw_bridge.h"
#include "../core/engine.h"
#include "../core/spread.h"

static void TracePart1(vec3_t* eyePos, vec3_t point, trace_t* tr, C_BasePlayer* skipEnt)
{
	Ray_t ray;
	CTraceFilter filter;

	ray.Init(*eyePos, point);
	filter.pSkip = skipEnt;

	memset(tr, 0, sizeof(trace_t));

	engineTrace->TraceRay(ray, MASK_SHOT, &filter, tr);
}

static float TracePart2(LocalPlayer* localPlayer, Players* players, trace_t* tr, int eID)
{
	if ((void*)tr->m_pEnt != players->instance[eID])
		return -1.f;

	int hbID = FwBridge::hitboxIDs[tr->hitbox];

	if (hbID < 0)
		return -1.f;

	float distance = ((vec3)localPlayer->eyePos - tr->endpos).Length();
	float damage = localPlayer->weaponDamage * powf(localPlayer->weaponRangeModifier, distance * 0.002f);

	return (int)(damage * players->hitboxes[eID].damageMul[hbID]);
}

int Tracing::TracePlayers(LocalPlayer* localPlayer, Players* players, vec3_t point, int eID, int depth, bool skipLocal)
{
	trace_t tr;
	TracePart1(&localPlayer->eyePos, point, &tr, skipLocal ? FwBridge::localPlayer : nullptr);
	return TracePart2(localPlayer, players, &tr, eID);
}

template<size_t N>
void Tracing::TracePlayersSIMD(LocalPlayer* localPlayer, Players* players, vec3soa<float, N> point, int eID, int out[N], int depth, bool skipLocal)
{
	trace_t tr[N];
	C_BasePlayer* skipEnt = skipLocal ? FwBridge::localPlayer : nullptr;
	for (size_t i = 0; i < N; i++)
		TracePart1(&localPlayer->eyePos, (vec3_t)point.acc[i], tr + i, skipEnt);
	for (size_t i = 0; i < N; i++)
		out[i] = TracePart2(localPlayer, players, tr + i, eID);
}

//Template size definitions to make the linking successful
template void Tracing::TracePlayersSIMD<MULTIPOINT_COUNT>(LocalPlayer* localPlayer, Players* players, mvec3 point, int eID, int out[MULTIPOINT_COUNT], int depth, bool skipLocal);
//template void Tracing::TracePlayersSIMD<SIMD_COUNT>(LocalPlayer* localPlayer, Players* players, nvec3 point, int eID, int out[SIMD_COUNT], int depth, bool skipLocal);

enum BTMask
{
	NON_BACKTRACKABLE = (1 << 0)
};

bool Tracing::BacktrackPlayers(Players* players, Players* prevPlayers, char backtrackMask[MAX_PLAYERS])
{
	int count = players->count;
	for (int i = 0; i < count; i++)
		if (players->flags[i] & Flags::HITBOXES_UPDATED && players->time[i] < FwBridge::maxBacktrack)
			return false;

	bool validPlayer = false;

	for (int i = 0; i < count; i++) {
		int id = players->unsortIDs[i];
		int prevID = prevPlayers ? prevPlayers->sortIDs[id] : MAX_PLAYERS;
		if (players->flags[i] & Flags::HITBOXES_UPDATED &&
			~backtrackMask[id] & BTMask::NON_BACKTRACKABLE &&
			(!prevPlayers || prevID >= prevPlayers->count || ((vec3)players->origin[i] - (vec3)prevPlayers->origin[prevID]).LengthSqr() < 4096))
			validPlayer = true; //In CSGO 3D length square is used to check for lagcomp breakage
		else
			backtrackMask[id] |= NON_BACKTRACKABLE;
	}

	if (validPlayer) {
		for (int i = 0; i < count; i++) {
			int id = players->unsortIDs[i];
			if (players->flags[i] & Flags::HITBOXES_UPDATED && ~backtrackMask[id] & BTMask::NON_BACKTRACKABLE) {
				C_BasePlayer* ent = (C_BasePlayer*)players->instance[i];
				vec3 origin = (vec3)players->origin[i];
				SetAbsOrigin(ent, origin);
				CUtlVector<matrix3x4_t>& matrix = ent->m_nBoneMatrix();
				int bones = Engine::numBones[id];

				memcpy(*(void**)&matrix, players->bones[i], sizeof(matrix3x4_t) * bones);
			}
		}
	}

	return validPlayer;
}
