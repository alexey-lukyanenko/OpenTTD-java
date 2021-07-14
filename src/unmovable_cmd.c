/* $Id: unmovable_cmd.c 3066 2005-10-19 14:49:46Z tron $ */

#include "stdafx.h"
#include "openttd.h"
#include "table/strings.h"
#include "table/sprites.h"
#include "functions.h"
#include "map.h"
#include "tile.h"
#include "command.h"
#include "viewport.h"
#include "player.h"
#include "gui.h"
#include "station.h"
#include "economy.h"
#include "town.h"
#include "sprite.h"
#include "variables.h"

/** Destroy a HQ.
 * During normal gameplay you can only implicitely destroy a HQ when you are
 * rebuilding it. Otherwise, only water can destroy it.
 * @param tile tile coordinates where HQ is located to destroy
 * @param flags docommand flags of calling function
 */
int32 DestroyCompanyHQ(TileIndex tile, uint32 flags)
{
	Player *p;

	SET_EXPENSES_TYPE(EXPENSES_PROPERTY);

	/* Find player that has HQ flooded, and reset their location_of_house */
	if (_current_player == OWNER_WATER) {
		bool dodelete = false;

		FOR_ALL_PLAYERS(p) {
			if (p->location_of_house == tile) {
				dodelete = true;
				break;
			}
		}
		if (!dodelete) return CMD_ERROR;
	} else /* Destruction was initiated by player */
		p = GetPlayer(_current_player);

		if (p->location_of_house == 0) return CMD_ERROR;

		if (flags & DC_EXEC) {
			DoClearSquare(p->location_of_house + TileDiffXY(0, 0));
			DoClearSquare(p->location_of_house + TileDiffXY(0, 1));
			DoClearSquare(p->location_of_house + TileDiffXY(1, 0));
			DoClearSquare(p->location_of_house + TileDiffXY(1, 1));
			p->location_of_house = 0; // reset HQ position
			InvalidateWindow(WC_COMPANY, (int)p->index);
		}

	// cost of relocating company is 1% of company value
		return CalculateCompanyValue(p) / 100;
}

/** Build or relocate the HQ. This depends if the HQ is already built or not
 * @param x,y the coordinates where the HQ will be built or relocated to
 * @param p1 unused
 * @param p2 unused
 */
extern int32 CheckFlatLandBelow(TileIndex tile, uint w, uint h, uint flags, uint invalid_dirs, int *);
int32 CmdBuildCompanyHQ(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	TileIndex tile = TileVirtXY(x, y);
	Player *p = GetPlayer(_current_player);
	int cost;

	SET_EXPENSES_TYPE(EXPENSES_PROPERTY);

	cost = CheckFlatLandBelow(tile, 2, 2, flags, 0, NULL);
	if (CmdFailed(cost)) return CMD_ERROR;

	if (p->location_of_house != 0) { /* Moving HQ */
		int32 ret = DestroyCompanyHQ(p->location_of_house, flags);
		if (CmdFailed(ret)) return CMD_ERROR;
		cost += ret;
	}

	if (flags & DC_EXEC) {
		int score = UpdateCompanyRatingAndValue(p, false);

		p->location_of_house = tile;

		ModifyTile(tile + TileDiffXY(0, 0), MP_SETTYPE(MP_UNMOVABLE) | MP_MAPOWNER_CURRENT | MP_MAP5, 0x80);
		ModifyTile(tile + TileDiffXY(0, 1), MP_SETTYPE(MP_UNMOVABLE) | MP_MAPOWNER_CURRENT | MP_MAP5, 0x81);
		ModifyTile(tile + TileDiffXY(1, 0), MP_SETTYPE(MP_UNMOVABLE) | MP_MAPOWNER_CURRENT | MP_MAP5, 0x82);
		ModifyTile(tile + TileDiffXY(1, 1), MP_SETTYPE(MP_UNMOVABLE) | MP_MAPOWNER_CURRENT | MP_MAP5, 0x83);
		UpdatePlayerHouse(p, score);
		InvalidateWindow(WC_COMPANY, (int)p->index);
	}

	return cost;
}

typedef struct DrawTileUnmovableStruct {
	uint16 image;
	byte subcoord_x;
	byte subcoord_y;
	byte width;
	byte height;
	byte z_size;
	byte unused;
} DrawTileUnmovableStruct;

#include "table/unmovable_land.h"

static void DrawTile_Unmovable(TileInfo *ti)
{
	uint32 image, ormod;

	if (!(ti->map5 & 0x80)) {
		if (ti->map5 == 2) {

			// statue
			DrawGroundSprite(SPR_STATUE_GROUND);

			image = PLAYER_SPRITE_COLOR(GetTileOwner(ti->tile));
			image += PALETTE_MODIFIER_COLOR | SPR_STATUE_COMPANY;
			if (_display_opt & DO_TRANS_BUILDINGS)
				MAKE_TRANSPARENT(image);
			AddSortableSpriteToDraw(image, ti->x, ti->y, 16, 16, 25, ti->z);
		} else if (ti->map5 == 3) {

			// "owned by" sign
			DrawClearLandTile(ti, 0);

			AddSortableSpriteToDraw(
				PLAYER_SPRITE_COLOR(GetTileOwner(ti->tile)) + PALETTE_MODIFIER_COLOR + SPR_BOUGHT_LAND,
				ti->x+8, ti->y+8,
				1, 1,
				10,
				GetSlopeZ(ti->x+8, ti->y+8)
			);
		} else {
			// lighthouse or transmitter

			const DrawTileUnmovableStruct *dtus;

			if (ti->tileh) DrawFoundation(ti, ti->tileh);
			DrawClearLandTile(ti, 2);

			dtus = &_draw_tile_unmovable_data[ti->map5];

			image = dtus->image;
			if (_display_opt & DO_TRANS_BUILDINGS)
				MAKE_TRANSPARENT(image);

			AddSortableSpriteToDraw(image,
				ti->x | dtus->subcoord_x,
				ti->y | dtus->subcoord_y,
				dtus->width, dtus->height,
				dtus->z_size, ti->z);
		}
	} else {
		const DrawTileSeqStruct *dtss;
		const DrawTileSprites *t;

		if (ti->tileh) DrawFoundation(ti, ti->tileh);

		ormod = PLAYER_SPRITE_COLOR(GetTileOwner(ti->tile));

		t = &_unmovable_display_datas[ti->map5 & 0x7F];
		DrawGroundSprite(t->ground_sprite | ormod);

		foreach_draw_tile_seq(dtss, t->seq) {
			image = dtss->image;
			if (_display_opt & DO_TRANS_BUILDINGS) {
				MAKE_TRANSPARENT(image);
			} else {
				image |= ormod;
			}
			AddSortableSpriteToDraw(image, ti->x + dtss->delta_x, ti->y + dtss->delta_y,
				dtss->width, dtss->height, dtss->unk, ti->z + dtss->delta_z);
		}
	}
}

static uint GetSlopeZ_Unmovable(const TileInfo* ti)
{
	return GetPartialZ(ti->x & 0xF, ti->y & 0xF, ti->tileh) + ti->z;
}

static uint GetSlopeTileh_Unmovable(const TileInfo *ti)
{
	return 0;
}

static int32 ClearTile_Unmovable(TileIndex tile, byte flags)
{
	byte m5 = _m[tile].m5;

	if (m5 & 0x80) {
		if (_current_player == OWNER_WATER) return DestroyCompanyHQ(tile, DC_EXEC);
		return_cmd_error(STR_5804_COMPANY_HEADQUARTERS_IN);
	}

	if (m5 == 3) // company owned land
		return DoCommandByTile(tile, 0, 0, flags, CMD_SELL_LAND_AREA);

	// checks if you're allowed to remove unmovable things
	if (_game_mode != GM_EDITOR && _current_player != OWNER_WATER && ((flags & DC_AUTO || !_cheats.magic_bulldozer.value)) )
		return_cmd_error(STR_5800_OBJECT_IN_THE_WAY);

	if (flags & DC_EXEC) {
		DoClearSquare(tile);
	}

	return 0;
}

static void GetAcceptedCargo_Unmovable(TileIndex tile, AcceptedCargo ac)
{
	byte m5 = _m[tile].m5;
	uint level; // HQ level (depends on company performance) in the range 1..5.

	if (!(m5 & 0x80)) {
		/* not used */
		return;
	}

	/* HQ accepts passenger and mail; but we have to divide the values
	 * between 4 tiles it occupies! */

	level = (m5 & ~0x80) / 4 + 1;

	// Top town building generates 10, so to make HQ interesting, the top
	// type makes 20.
	ac[CT_PASSENGERS] = max(1, level);

	// Top town building generates 4, HQ can make up to 8. The
	// proportion passengers:mail is different because such a huge
	// commercial building generates unusually high amount of mail
	// correspondence per physical visitor.
	ac[CT_MAIL] = max(1, level / 2);
}

static const StringID _unmovable_tile_str[] = {
	STR_5803_COMPANY_HEADQUARTERS,
	STR_5801_TRANSMITTER,
	STR_5802_LIGHTHOUSE,
	STR_2016_STATUE,
	STR_5805_COMPANY_OWNED_LAND,
};

static void GetTileDesc_Unmovable(TileIndex tile, TileDesc *td)
{
	int i = _m[tile].m5;
	if (i & 0x80) i = -1;
	td->str = _unmovable_tile_str[i + 1];
	td->owner = GetTileOwner(tile);
}

static void AnimateTile_Unmovable(TileIndex tile)
{
	/* not used */
}

static void TileLoop_Unmovable(TileIndex tile)
{
	byte m5 = _m[tile].m5;
	uint level; // HQ level (depends on company performance) in the range 1..5.
	uint32 r;

	if (!(m5 & 0x80)) {
		/* not used */
		return;
	}

	/* HQ accepts passenger and mail; but we have to divide the values
	 * between 4 tiles it occupies! */

	level = GB(m5, 0, 7) / 4 + 1;
	assert(level < 6);

	r = Random();
	// Top town buildings generate 250, so the top HQ type makes 256.
	if (GB(r, 0, 8) < (256 / 4 / (6 - level))) {
		uint amt = GB(r, 0, 8) / 8 / 4 + 1;
		if (_economy.fluct <= 0) amt = (amt + 1) >> 1;
		MoveGoodsToStation(tile, 2, 2, CT_PASSENGERS, amt);
	}

	// Top town building generates 90, HQ can make up to 196. The
	// proportion passengers:mail is about the same as in the acceptance
	// equations.
	if (GB(r, 8, 8) < (196 / 4 / (6 - level))) {
		uint amt = GB(r, 8, 8) / 8 / 4 + 1;
		if (_economy.fluct <= 0) amt = (amt + 1) >> 1;
		MoveGoodsToStation(tile, 2, 2, CT_MAIL, amt);
	}
}


static uint32 GetTileTrackStatus_Unmovable(TileIndex tile, TransportType mode)
{
	return 0;
}

static void ClickTile_Unmovable(TileIndex tile)
{
	if (_m[tile].m5 & 0x80) {
		ShowPlayerCompany(GetTileOwner(tile));
	}
}

static const TileIndexDiffC _tile_add[] = {
	{ 1,  0},
	{ 0,  1},
	{-1,  0},
	{ 0, -1}
};

/* checks, if a radio tower is within a 9x9 tile square around tile */
static bool checkRadioTowerNearby(TileIndex tile)
{
	TileIndex tile_s = tile - TileDiffXY(4, 4);

	BEGIN_TILE_LOOP(tile, 9, 9, tile_s)
		// already a radio tower here?
		if (IsTileType(tile, MP_UNMOVABLE) && _m[tile].m5 == 0)
			return false;
	END_TILE_LOOP(tile, 9, 9, tile_s)
	return true;
}

void GenerateUnmovables(void)
{
	int i,j;
	TileIndex tile;
	uint32 r;
	int dir;
	uint h;

	if (_opt.landscape == LT_CANDY)
		return;

	/* add radio tower */
	i = ScaleByMapSize(1000);
	j = ScaleByMapSize(40); // maximum number of radio towers on the map
	do {
		tile = RandomTile();
		if (IsTileType(tile, MP_CLEAR) && GetTileSlope(tile, &h) == 0 && h >= 32) {
			if(!checkRadioTowerNearby(tile))
				continue;
			SetTileType(tile, MP_UNMOVABLE);
			_m[tile].m5 = 0;
			SetTileOwner(tile, OWNER_NONE);
			if (--j == 0)
				break;
		}
	} while (--i);

	if (_opt.landscape == LT_DESERT)
		return;

	/* add lighthouses */
	i = ScaleByMapSize1D((Random() & 3) + 7);
	do {
restart:
		r = Random();
		dir = r >> 30;
		r %= (dir == 0 || dir == 2) ? MapMaxY() : MapMaxX();
		tile =
			(dir == 0) ? TileXY(0, r)         : 0 + // left
			(dir == 1) ? TileXY(r, 0)         : 0 + // top
			(dir == 2) ? TileXY(MapMaxX(), r) : 0 + // right
			(dir == 3) ? TileXY(r, MapMaxY()) : 0;  // bottom
		j = 20;
		do {
			if (--j == 0)
				goto restart;
			tile = TILE_MASK(tile + ToTileIndexDiff(_tile_add[dir]));
		} while (!(IsTileType(tile, MP_CLEAR) && GetTileSlope(tile, &h) == 0 && h <= 16));

		assert(tile == TILE_MASK(tile));

		SetTileType(tile, MP_UNMOVABLE);
		_m[tile].m5 = 1;
		SetTileOwner(tile, OWNER_NONE);
	} while (--i);
}

static void ChangeTileOwner_Unmovable(TileIndex tile, PlayerID old_player, PlayerID new_player)
{
	if (!IsTileOwner(tile, old_player)) return;

	if (_m[tile].m5 == 3 && new_player != OWNER_SPECTATOR) {
		SetTileOwner(tile, new_player);
	} else {
		DoClearSquare(tile);
	}
}

const TileTypeProcs _tile_type_unmovable_procs = {
	DrawTile_Unmovable,             /* draw_tile_proc */
	GetSlopeZ_Unmovable,            /* get_slope_z_proc */
	ClearTile_Unmovable,            /* clear_tile_proc */
	GetAcceptedCargo_Unmovable,     /* get_accepted_cargo_proc */
	GetTileDesc_Unmovable,          /* get_tile_desc_proc */
	GetTileTrackStatus_Unmovable,   /* get_tile_track_status_proc */
	ClickTile_Unmovable,            /* click_tile_proc */
	AnimateTile_Unmovable,          /* animate_tile_proc */
	TileLoop_Unmovable,             /* tile_loop_clear */
	ChangeTileOwner_Unmovable,      /* change_tile_owner_clear */
	NULL,                           /* get_produced_cargo_proc */
	NULL,                           /* vehicle_enter_tile_proc */
	NULL,                           /* vehicle_leave_tile_proc */
	GetSlopeTileh_Unmovable,        /* get_slope_tileh_proc */
};
