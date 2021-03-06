#include <ultra64.h>
#ifndef TARGET_N64
#include <string.h>
#endif

#include "sm64.h"
#include "audio/external.h"
#include "buffers/framebuffers.h"
#include "buffers/zbuffer.h"
#include "game/area.h"
#include "game/display.h"
#include "game/mario.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/save_file.h"
#include "game/sound_init.h"
#include "geo_layout.h"
#include "graph_node.h"
#include "level_script.h"
#include "math_util.h"
#include "surface_collision.h"
#include "surface_load.h"
#include "game/game.h"
#include "level_table.h"
#include "hook_level.h"
#include "hook_geo.h"
#include "hook_macro.h"

//#define DEBUG

#ifndef __FUNCTION_NAME__
#ifdef _MSC_VER // WINDOWS
#define __FUNCTION_NAME__ __FUNCTION__
#else //*NIX
#define __FUNCTION_NAME__ __func__
#endif
#endif

#if defined(DEBUG) && defined(WIN65)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>
#define LOG_OP() OutputDebugString(__FUNCTION_NAME__ "\n")
#else
#define LOG_OP()
#endif

//#define CMD_SIZE_SHIFT (sizeof(void*) >> 3)
//#define CMD_PROCESS_OFFSET(offset) ((offset & 3) | ((offset & ~3) << CMD_SIZE_SHIFT))

u64 cmdSizeShift()
{
	return (sizeof(void*) >> 3);
}

u64 cmdProcessOffset(const u64 offset)
{
	return ((offset & 3) | ((offset & ~3) << cmdSizeShift()));
}

#define CMD_GET(type, offset) (*(type*)(CMD_PROCESS_OFFSET(offset) + (u8*)sCurrentCmd))

void* cmdGet(u8* sCurrentCmd, u64 offset)
{
	u8* r = sCurrentCmd + cmdProcessOffset(offset) + 6;
	return *(void**)r;
}

// These are equal
#define CMD_NEXT ((struct LevelCommand*)((u8*)sCurrentCmd + (sCurrentCmd->size << CMD_SIZE_SHIFT)))
#define NEXT_CMD ((struct LevelCommand*)((sCurrentCmd->size << CMD_SIZE_SHIFT) + (u8*)sCurrentCmd))

enum ScriptStatus
{
	SCRIPT_RUNNING = 1,
	SCRIPT_PAUSED  = 0,
	SCRIPT_PAUSED2 = -1
};

static uintptr_t sStack[32];

static struct AllocOnlyPool* sLevelPool = NULL;

static u16 sDelayFrames	 = 0;
static u16 sDelayFrames2 = 0;

static s16 sCurrAreaIndex = -1;

static uintptr_t* sStackTop  = sStack;
static uintptr_t* sStackBase = NULL;

static s16 sScriptStatus;
static s32 sRegister;
static struct LevelCommand* sCurrentCmd;

static s32 eval_script_op(s8 op, s32 arg)
{
	s32 result = 0;

	switch(op)
	{
		case 0:
			result = sRegister & arg;
			break;
		case 1:
			result = !(sRegister & arg);
			break;
		case 2:
			result = sRegister == arg;
			break;
		case 3:
			result = sRegister != arg;
			break;
		case 4:
			result = sRegister < arg;
			break;
		case 5:
			result = sRegister <= arg;
			break;
		case 6:
			result = sRegister > arg;
			break;
		case 7:
			result = sRegister >= arg;
			break;
	}

	return result;
}

static void level_cmd_load_and_execute(void)
{
#ifdef DEBUG
	LOG_OP();
#endif
	main_pool_push_state();

	uintptr_t next = (uintptr_t)NEXT_CMD;
	*sStackTop++   = next;
	*sStackTop++   = (uintptr_t)sStackBase;
	sStackBase     = sStackTop;

	// sCurrentCmd = cmdGet(sCurrentCmd, 12);
	sCurrentCmd = sm64::hook::level::apply((LevelCommand*)CMD_GET(void*, 12));
}

static void level_cmd_exit_and_execute(void)
{
	void* targetAddr = CMD_GET(void*, 12);

#ifdef DEBUG
	LOG_OP();
#endif

	main_pool_pop_state();
	main_pool_push_state();

	load_segment(CMD_GET(s16, 2), CMD_GET(void*, 4), CMD_GET(void*, 8), MEMORY_POOL_LEFT);

	sStackTop   = sStackBase;
	sCurrentCmd = sm64::hook::level::apply((LevelCommand*)segmented_to_virtual(targetAddr));
}

static void level_cmd_exit(void)
{
#ifdef DEBUG
	LOG_OP();
#endif
	main_pool_pop_state();

	sStackTop   = sStackBase;
	sStackBase  = (uintptr_t*)*(--sStackTop);
	sCurrentCmd = (struct LevelCommand*)*(--sStackTop);
}

static void level_cmd_sleep(void)
{
#ifdef DEBUG
	// LOG_OP();
#endif
	sScriptStatus = SCRIPT_PAUSED;

	if(sDelayFrames == 0)
	{
		sDelayFrames = CMD_GET(s16, 2) * FRAME_RATE_SCALER_INV;
	}
	else if(--sDelayFrames == 0)
	{
		sCurrentCmd   = CMD_NEXT;
		sScriptStatus = SCRIPT_RUNNING;
	}
}

static void level_cmd_sleep2(void)
{
#ifdef DEBUG
	// LOG_OP();
#endif
	sScriptStatus = SCRIPT_PAUSED2;

	if(sDelayFrames2 == 0)
	{
		sDelayFrames2 = CMD_GET(s16, 2) * FRAME_RATE_SCALER_INV;
	}
	else if(--sDelayFrames2 == 0)
	{
		sCurrentCmd   = CMD_NEXT;
		sScriptStatus = SCRIPT_RUNNING;
	}
}

static void level_cmd_jump(void)
{
#ifdef DEBUG
	LOG_OP();
#endif
	sCurrentCmd = sm64::hook::level::apply((LevelCommand*)segmented_to_virtual(CMD_GET(void*, 4)));
}

static void level_cmd_jump_and_link(void)
{
	LOG_OP();
	*sStackTop++ = (uintptr_t)NEXT_CMD;
	sCurrentCmd  = sm64::hook::level::apply((LevelCommand*)segmented_to_virtual(CMD_GET(void*, 4)));
}

static void level_cmd_return(void)
{
	sCurrentCmd = (struct LevelCommand*)*(--sStackTop);
}

static void level_cmd_jump_and_link_push_arg(void)
{
	LOG_OP();
	*sStackTop++ = (uintptr_t)NEXT_CMD;
	*sStackTop++ = CMD_GET(s16, 2);
	sCurrentCmd  = CMD_NEXT;
}

static void level_cmd_jump_repeat(void)
{
	LOG_OP();
	s32 val = *(sStackTop - 1);

	if(val == 0)
	{
		sCurrentCmd = (struct LevelCommand*)*(sStackTop - 2);
	}
	else if(--val != 0)
	{
		*(sStackTop - 1) = val;
		sCurrentCmd	 = (struct LevelCommand*)*(sStackTop - 2);
	}
	else
	{
		sCurrentCmd = CMD_NEXT;
		sStackTop -= 2;
	}
}

static void level_cmd_loop_begin(void)
{
	LOG_OP();
	*sStackTop++ = (uintptr_t)NEXT_CMD;
	*sStackTop++ = 0;
	sCurrentCmd  = CMD_NEXT;
}

static void level_cmd_loop_until(void)
{
	LOG_OP();
	if(eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0)
	{
		sCurrentCmd = CMD_NEXT;
		sStackTop -= 2;
	}
	else
	{
		sCurrentCmd = (struct LevelCommand*)*(sStackTop - 2);
	}
}

static void level_cmd_jump_if(void)
{
	LOG_OP();
	if(eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0)
	{
		sCurrentCmd = sm64::hook::level::apply((LevelCommand*)segmented_to_virtual(CMD_GET(void*, 8)));
	}
	else
	{
		sCurrentCmd = CMD_NEXT;
	}
}

static void level_cmd_jump_and_link_if(void)
{
	LOG_OP();
	if(eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0)
	{
		*sStackTop++ = (uintptr_t)NEXT_CMD;
		sCurrentCmd  = sm64::hook::level::apply((LevelCommand*)segmented_to_virtual(CMD_GET(void*, 8)));
	}
	else
	{
		sCurrentCmd = CMD_NEXT;
	}
}

static void level_cmd_skip_if(void)
{
	LOG_OP();
	if(eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) == 0)
	{
		do
		{
			sCurrentCmd = CMD_NEXT;
		} while(sCurrentCmd->type == 0x0F || sCurrentCmd->type == 0x10);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_skip(void)
{
	LOG_OP();
	do
	{
		sCurrentCmd = CMD_NEXT;
	} while(sCurrentCmd->type == 0x10);

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_skippable_nop(void)
{
#ifdef DEBUG
	LOG_OP();
#endif
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_call(void)
{
	LOG_OP();
	typedef s32 (*Func)(s16, s32);
	Func func   = CMD_GET(Func, 4);
	sRegister   = func(CMD_GET(s16, 2), sRegister);
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_call_loop(void)
{
	LOG_OP();
	typedef s32 (*Func)(s16, s32);
	Func func = CMD_GET(Func, 4);
	sRegister = func(CMD_GET(s16, 2), sRegister);

	if(sRegister == 0)
	{
		sScriptStatus = SCRIPT_PAUSED;
	}
	else
	{
		sScriptStatus = SCRIPT_RUNNING;
		sCurrentCmd   = CMD_NEXT;
	}
}

static void level_cmd_set_register(void)
{
	LOG_OP();
	sRegister   = CMD_GET(s16, 2);
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_push_pool_state(void)
{
	LOG_OP();
	main_pool_push_state();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_pop_pool_state(void)
{
	LOG_OP();
	main_pool_pop_state();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_to_fixed_address(void)
{
	LOG_OP();
	load_to_fixed_pool_addr(CMD_GET(void*, 4), CMD_GET(void*, 8), CMD_GET(void*, 12));
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_raw(void)
{
	LOG_OP();
	load_segment(CMD_GET(s16, 2), CMD_GET(void*, 4), CMD_GET(void*, 8), MEMORY_POOL_LEFT);
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_mio0(void)
{
	LOG_OP();
	load_segment_decompress(CMD_GET(s16, 2), CMD_GET(void*, 4), CMD_GET(void*, 8));
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_mario_head(void)
{
	LOG_OP();

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_mio0_texture(void)
{
	LOG_OP();
	func_80278304(CMD_GET(s16, 2), CMD_GET(void*, 4), CMD_GET(void*, 8));
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_init_level(void)
{
	LOG_OP();
	init_graph_node_start(NULL, (struct GraphNodeStart*)&gObjParentGraphNode);
	clear_objects();
	clear_areas();
	main_pool_push_state();

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_clear_level(void)
{
	sm64::config().camera().unsetLevelLoaded();
	clear_objects();
	func_8027A7C4();
	clear_areas();
	main_pool_pop_state();

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_alloc_level_pool(void)
{
	LOG_OP();
	if(sLevelPool == NULL)
	{
		sLevelPool = alloc_only_pool_init(main_pool_available() - sizeof(struct AllocOnlyPool), MEMORY_POOL_LEFT);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_free_level_pool(void)
{
	LOG_OP();
	s32 i;

	alloc_only_pool_resize(sLevelPool, sLevelPool->usedSpace);
	sLevelPool = NULL;

	for(i = 0; i < 8; i++)
	{
		if(gAreaData[i].terrainData != NULL)
		{
			alloc_surface_pools();
			break;
		}
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_begin_area(void)
{
	LOG_OP();
	u8 areaIndex	   = CMD_GET(u8, 2);
	auto geoLayoutAddr = (GeoLayout*)CMD_GET(void*, 4);

	if(areaIndex < 8)
	{
		struct GraphNodeRoot* screenArea = (struct GraphNodeRoot*)process_geo_layout(sLevelPool, geoLayoutAddr);
		struct GraphNodeCamera* node	 = (struct GraphNodeCamera*)screenArea->views[0];

		sCurrAreaIndex		= areaIndex;
		screenArea->areaIndex	= areaIndex;
		gAreas[areaIndex].unk04 = (struct GraphNode*)screenArea;

		if(node != NULL)
		{
			gAreas[areaIndex].camera = node->config.camera;
		}
		else
		{
			gAreas[areaIndex].camera = NULL;
		}
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_end_area(void)
{
	LOG_OP();
	sCurrAreaIndex = -1;
	sCurrentCmd    = CMD_NEXT;
}

static void level_cmd_load_model_from_dl(void)
{
	LOG_OP();
	s16 val1   = CMD_GET(s16, 2) & 0x0FFF;
	s16 val2   = CMD_GET(u16, 2) >> 12;
	void* val3 = CMD_GET(void*, 4);

	if(val1 < 256)
	{
		gLoadedGraphNodes[val1] = (struct GraphNode*)init_graph_node_display_list(sLevelPool, 0, val2, val3);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_model_from_geo(void)
{
	LOG_OP();
	s16 arg0  = CMD_GET(s16, 2);
	auto arg1 = (GeoLayout*)sm64::hook::geo::apply((GeoLayout*)nullptr, (sm64::hook::geo::Id)(u64)CMD_GET(void*, 4));

	if(arg0 < 256)
	{
		gLoadedGraphNodes[arg0] = process_geo_layout(sLevelPool, arg1);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_23(void)
{
	LOG_OP();
	union
	{
		s32 i;
		f32 f;
	} arg2;

	s16 model  = CMD_GET(s16, 2) & 0x0FFF;
	s16 arg0H  = CMD_GET(u16, 2) >> 12;
	void* arg1 = CMD_GET(void*, 4);
	// load an f32, but using an integer load instruction for some reason (hence the union)
	arg2.i = CMD_GET(s32, 8);

	if(model < 256)
	{
		// GraphNodeScale has a GraphNode at the top. This
		// is being stored to the array, so cast the pointer.
		gLoadedGraphNodes[model] = (struct GraphNode*)init_graph_node_scale(sLevelPool, 0, arg0H, arg1, arg2.f);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_init_mario(void)
{
	LOG_OP();
	vec3s_set(gMarioSpawnInfo->startPos, 0, 0, 0);
	vec3s_set(gMarioSpawnInfo->startAngle, 0, 0, 0);

	gMarioSpawnInfo->activeAreaIndex = -1;
	gMarioSpawnInfo->areaIndex	 = 0;
	gMarioSpawnInfo->behaviorArg	 = CMD_GET(u32, 4);
	gMarioSpawnInfo->behaviorScript	 = CMD_GET(void*, 8);
	gMarioSpawnInfo->unk18		 = gLoadedGraphNodes[CMD_GET(u8, 3)];
	gMarioSpawnInfo->next		 = NULL;

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_place_object(void)
{
	LOG_OP();
	u8 val7 = 1 << (gCurrActNum - 1);
	u16 model;
	struct SpawnInfo* spawnInfo;

	if(sCurrAreaIndex != -1 && ((CMD_GET(u8, 2) & val7) || CMD_GET(u8, 2) == 0x1F))
	{
		model	  = CMD_GET(u8, 3);
		spawnInfo = (SpawnInfo*)alloc_only_pool_alloc(sLevelPool, sizeof(struct SpawnInfo));

		spawnInfo->startPos[0] = CMD_GET(s16, 4);
		spawnInfo->startPos[1] = CMD_GET(s16, 6);
		spawnInfo->startPos[2] = CMD_GET(s16, 8);

		spawnInfo->startAngle[0] = CMD_GET(s16, 10) * 0x8000 / 180;
		spawnInfo->startAngle[1] = CMD_GET(s16, 12) * 0x8000 / 180;
		spawnInfo->startAngle[2] = CMD_GET(s16, 14) * 0x8000 / 180;

		spawnInfo->areaIndex	   = sCurrAreaIndex;
		spawnInfo->activeAreaIndex = sCurrAreaIndex;

		spawnInfo->behaviorArg	  = CMD_GET(u32, 16);
		spawnInfo->behaviorScript = CMD_GET(void*, 20);
		spawnInfo->unk18	  = gLoadedGraphNodes[model];
		spawnInfo->next		  = gAreas[sCurrAreaIndex].objectSpawnInfos;

		gAreas[sCurrAreaIndex].objectSpawnInfos = spawnInfo;
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_warp_node(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
		struct ObjectWarpNode* warpNode = (ObjectWarpNode*)alloc_only_pool_alloc(sLevelPool, sizeof(struct ObjectWarpNode));

		warpNode->node.id	 = CMD_GET(u8, 2);
		warpNode->node.destLevel = CMD_GET(u8, 3) + CMD_GET(u8, 6);
		warpNode->node.destArea	 = CMD_GET(u8, 4);
		warpNode->node.destNode	 = CMD_GET(u8, 5);

		warpNode->object = NULL;

		warpNode->next			 = gAreas[sCurrAreaIndex].warpNodes;
		gAreas[sCurrAreaIndex].warpNodes = warpNode;
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_instant_warp(void)
{
	LOG_OP();
	s32 i;
	struct InstantWarp* warp;

	if(sCurrAreaIndex != -1)
	{
		if(gAreas[sCurrAreaIndex].instantWarps == NULL)
		{
			gAreas[sCurrAreaIndex].instantWarps = (InstantWarp*)alloc_only_pool_alloc(sLevelPool, 4 * sizeof(struct InstantWarp));

			for(i = INSTANT_WARP_INDEX_START; i < INSTANT_WARP_INDEX_STOP; i++)
			{
				gAreas[sCurrAreaIndex].instantWarps[i].id = 0;
			}
		}

		warp = gAreas[sCurrAreaIndex].instantWarps + CMD_GET(u8, 2);

		warp[0].id   = 1;
		warp[0].area = CMD_GET(u8, 3);

		warp[0].displacement[0] = CMD_GET(s16, 4);
		warp[0].displacement[1] = CMD_GET(s16, 6);
		warp[0].displacement[2] = CMD_GET(s16, 8);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_terrain_type(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
		gAreas[sCurrAreaIndex].terrainType |= CMD_GET(s16, 2);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_painting_warp_node(void)
{
	LOG_OP();
	s32 i;
	struct WarpNode* node;

	if(sCurrAreaIndex != -1)
	{
		if(gAreas[sCurrAreaIndex].paintingWarpNodes == NULL)
		{
			gAreas[sCurrAreaIndex].paintingWarpNodes = (WarpNode*)alloc_only_pool_alloc(sLevelPool, 45 * sizeof(struct WarpNode));

			for(i = 0; i < 45; i++)
			{
				gAreas[sCurrAreaIndex].paintingWarpNodes[i].id = 0;
			}
		}

		node = &gAreas[sCurrAreaIndex].paintingWarpNodes[CMD_GET(u8, 2)];

		node->id	= 1;
		node->destLevel = CMD_GET(u8, 3) + CMD_GET(u8, 6);
		node->destArea	= CMD_GET(u8, 4);
		node->destNode	= CMD_GET(u8, 5);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_3A(void)
{
	LOG_OP();
	struct UnusedArea28* val4;

	if(sCurrAreaIndex != -1)
	{
		if((val4 = gAreas[sCurrAreaIndex].unused28) == NULL)
		{
			val4 = gAreas[sCurrAreaIndex].unused28 = (UnusedArea28*)alloc_only_pool_alloc(sLevelPool, sizeof(struct UnusedArea28));
		}

		val4->unk00 = CMD_GET(s16, 2);
		val4->unk02 = CMD_GET(s16, 4);
		val4->unk04 = CMD_GET(s16, 6);
		val4->unk06 = CMD_GET(s16, 8);
		val4->unk08 = CMD_GET(s16, 10);
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_whirlpool(void)
{
	LOG_OP();
	struct Whirlpool* whirlpool;
	s32 index	= CMD_GET(u8, 2);
	s32 beatBowser2 = (save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR)) != 0;

	if(CMD_GET(u8, 3) == 0 || (CMD_GET(u8, 3) == 1 && !beatBowser2) || (CMD_GET(u8, 3) == 2 && beatBowser2) || (CMD_GET(u8, 3) == 3 && gCurrActNum >= 2))
	{
		if(sCurrAreaIndex != -1 && index < 2)
		{
			if((whirlpool = gAreas[sCurrAreaIndex].whirlpools[index]) == NULL)
			{
				whirlpool				 = (Whirlpool*)alloc_only_pool_alloc(sLevelPool, sizeof(struct Whirlpool));
				gAreas[sCurrAreaIndex].whirlpools[index] = whirlpool;
			}

			vec3s_set(whirlpool->pos, CMD_GET(s16, 4), CMD_GET(s16, 6), CMD_GET(s16, 8));
			whirlpool->strength = CMD_GET(s16, 10);
		}
	}

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_blackout(void)
{
	LOG_OP();
	osViBlack(CMD_GET(u8, 2));
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_gamma(void)
{
	LOG_OP();
	osViSetSpecialFeatures(CMD_GET(u8, 2) == 0 ? OS_VI_GAMMA_OFF : OS_VI_GAMMA_ON);
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_terrain_data(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
#ifdef TARGET_N64
		gAreas[sCurrAreaIndex].terrainData = segmented_to_virtual(CMD_GET(void*, 4));
#else
		Collision* data;
		u32 size;

		data				   = (Collision*)segmented_to_virtual(CMD_GET(void*, 4));
		size				   = get_area_terrain_size(data) * sizeof(Collision);
		gAreas[sCurrAreaIndex].terrainData = (s16*)alloc_only_pool_alloc(sLevelPool, size);
		memcpy(gAreas[sCurrAreaIndex].terrainData, data, size);
#endif
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_rooms(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
		gAreas[sCurrAreaIndex].surfaceRooms = (s8*)segmented_to_virtual(CMD_GET(void*, 4));
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_macro_objects(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
#ifdef TARGET_N64
		gAreas[sCurrAreaIndex].macroObjects = segmented_to_virtual(CMD_GET(void*, 4));
#else
		const MacroObject* data = sm64::hook::macro::apply(CMD_GET(MacroObject*, 4), sm64::hook::macro::Id::NONE);
		s32 len			= 0;
		while(data[len++] != 0x001E)
		{
			len += 4;
		}
		gAreas[sCurrAreaIndex].macroObjects = (s16*)alloc_only_pool_alloc(sLevelPool, len * sizeof(MacroObject));
		memcpy(gAreas[sCurrAreaIndex].macroObjects, data, len * sizeof(MacroObject));
#endif
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_area(void)
{
	LOG_OP();
	s16 areaIndex	    = CMD_GET(u8, 2);
	UNUSED void* unused = (u8*)sCurrentCmd + 4;

	func_80320890();
	load_area(areaIndex);

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_2A(void)
{
	LOG_OP();
	func_8027A998();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_mario_start_pos(void)
{
	LOG_OP();
	gMarioSpawnInfo->areaIndex = CMD_GET(u8, 2);

#if IS_64_BIT
	vec3s_set(gMarioSpawnInfo->startPos, CMD_GET(s16, 6), CMD_GET(s16, 8), CMD_GET(s16, 10));
#else
	vec3s_copy(gMarioSpawnInfo->startPos, CMD_GET(Vec3s, 6));
#endif
	vec3s_set(gMarioSpawnInfo->startAngle, 0, CMD_GET(s16, 4) * 0x8000 / 180, 0);
	sm64::config().camera().setLevelLoaded();

	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_2C(void)
{
	LOG_OP();
	func_8027AA88();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_2D(void)
{
	LOG_OP();
	area_update_objects();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_transition(void)
{
	LOG_OP();
	if(gCurrentArea != NULL)
	{
		play_transition(CMD_GET(u8, 2), CMD_GET(u8, 3), CMD_GET(u8, 4), CMD_GET(u8, 5), CMD_GET(u8, 6));
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_nop(void)
{
	LOG_OP();
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_show_dialog(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
		if(CMD_GET(u8, 2) < 2)
		{
			gAreas[sCurrAreaIndex].dialog[CMD_GET(u8, 2)] = CMD_GET(u8, 3);
		}
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_music(void)
{
	LOG_OP();
	if(sCurrAreaIndex != -1)
	{
		gAreas[sCurrAreaIndex].musicParam  = CMD_GET(s16, 2);
		gAreas[sCurrAreaIndex].musicParam2 = CMD_GET(s16, 4);
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_menu_music(void)
{
	LOG_OP();
	set_background_music(0, CMD_GET(s16, 2), 0);
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_38(void)
{
	LOG_OP();
	func_802491FC(CMD_GET(s16, 2));
	sCurrentCmd = CMD_NEXT;
}

extern int gPressedStart;

static void level_cmd_get_or_set_var(void)
{
	LOG_OP();
	if(CMD_GET(u8, 2) == 0)
	{
		switch(CMD_GET(u8, 3))
		{
			case 0:
				gCurrSaveFileNum = sRegister;
				break;
			case 1:
				gCurrCourseNum = sRegister;
				break;
			case 2:
				gCurrActNum = sRegister;
				break;
			case 3:
				gCurrLevelNum = sRegister;
				break;
			case 4:
				gCurrAreaIndex = sRegister;
				break;
			case 5:
				gPressedStart = sRegister;
				break;
		}
	}
	else
	{
		switch(CMD_GET(u8, 3))
		{
			case 0:
				sRegister = gCurrSaveFileNum;
				break;
			case 1:
				sRegister = gCurrCourseNum;
				break;
			case 2:
				sRegister = gCurrActNum;
				break;
			case 3:
				sRegister = gCurrLevelNum;
				break;
			case 4:
				sRegister = gCurrAreaIndex;
				break;
			case 5:
				sRegister = gPressedStart;
				break;
		}
	}

	sCurrentCmd = CMD_NEXT;
}

int gDemoLevels[7] = {LEVEL_BOWSER_1, LEVEL_WF, LEVEL_CCM, LEVEL_BBH, LEVEL_JRB, LEVEL_HMC, LEVEL_PSS};

int gDemoLevelID       = 0;
int gDemoInputListID_2 = 0;

extern int start_demo(int timer);

static void level_cmd_advdemo(void)
{
	start_demo(0);
	if(gDemoLevelID == 6)
	{
		sRegister    = gDemoLevels[6];
		gDemoLevelID = 0;
	}
	else
	{
		sRegister = gDemoLevels[gDemoLevelID++];
	}
	sCurrentCmd = CMD_NEXT;
}

static void level_cmd_cleardemoptr(void)
{
	gCurrDemoInput = NULL;
	sCurrentCmd    = CMD_NEXT;
}

static void (*LevelScriptJumpTable[])(void) = {
    /*00*/ level_cmd_load_and_execute,
    /*01*/ level_cmd_exit_and_execute,
    /*02*/ level_cmd_exit,
    /*03*/ level_cmd_sleep,
    /*04*/ level_cmd_sleep2,
    /*05*/ level_cmd_jump,
    /*06*/ level_cmd_jump_and_link,
    /*07*/ level_cmd_return,
    /*08*/ level_cmd_jump_and_link_push_arg,
    /*09*/ level_cmd_jump_repeat,
    /*0A*/ level_cmd_loop_begin,
    /*0B*/ level_cmd_loop_until,
    /*0C*/ level_cmd_jump_if,
    /*0D*/ level_cmd_jump_and_link_if,
    /*0E*/ level_cmd_skip_if,
    /*0F*/ level_cmd_skip,
    /*10*/ level_cmd_skippable_nop,
    /*11*/ level_cmd_call,
    /*12*/ level_cmd_call_loop,
    /*13*/ level_cmd_set_register,
    /*14*/ level_cmd_push_pool_state,
    /*15*/ level_cmd_pop_pool_state,
    /*16*/ level_cmd_load_to_fixed_address,
    /*17*/ level_cmd_load_raw,
    /*18*/ level_cmd_load_mio0,
    /*19*/ level_cmd_load_mario_head,
    /*1A*/ level_cmd_load_mio0_texture,
    /*1B*/ level_cmd_init_level,
    /*1C*/ level_cmd_clear_level,
    /*1D*/ level_cmd_alloc_level_pool,
    /*1E*/ level_cmd_free_level_pool,
    /*1F*/ level_cmd_begin_area,
    /*20*/ level_cmd_end_area,
    /*21*/ level_cmd_load_model_from_dl,
    /*22*/ level_cmd_load_model_from_geo,
    /*23*/ level_cmd_23,
    /*24*/ level_cmd_place_object,
    /*25*/ level_cmd_init_mario,
    /*26*/ level_cmd_create_warp_node,
    /*27*/ level_cmd_create_painting_warp_node,
    /*28*/ level_cmd_create_instant_warp,
    /*29*/ level_cmd_load_area,
    /*2A*/ level_cmd_2A,
    /*2B*/ level_cmd_set_mario_start_pos,
    /*2C*/ level_cmd_2C,
    /*2D*/ level_cmd_2D,
    /*2E*/ level_cmd_set_terrain_data,
    /*2F*/ level_cmd_set_rooms,
    /*30*/ level_cmd_show_dialog,
    /*31*/ level_cmd_set_terrain_type,
    /*32*/ level_cmd_nop,
    /*33*/ level_cmd_set_transition,
    /*34*/ level_cmd_set_blackout,
    /*35*/ level_cmd_set_gamma,
    /*36*/ level_cmd_set_music,
    /*37*/ level_cmd_set_menu_music,
    /*38*/ level_cmd_38,
    /*39*/ level_cmd_set_macro_objects,
    /*3A*/ level_cmd_3A,
    /*3B*/ level_cmd_create_whirlpool,
    /*3C*/ level_cmd_get_or_set_var,
    /*3D*/ level_cmd_advdemo,
    /*3E*/ level_cmd_cleardemoptr,
};

struct LevelCommand* level_script_execute(struct LevelCommand* cmd)
{
	sScriptStatus = SCRIPT_RUNNING;
	sCurrentCmd   = cmd;

	while(sScriptStatus == SCRIPT_RUNNING)
	{
		LevelScriptJumpTable[sCurrentCmd->type]();
	}

	init_render_image();
	render_game();
	end_master_display_list();
	alloc_display_list(0);

	return sCurrentCmd;
}
