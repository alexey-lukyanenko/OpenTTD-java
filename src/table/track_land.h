/* $Id: track_land.h 3108 2005-10-31 10:23:58Z tron $ */

typedef struct DrawTrackSeqStruct {
	SpriteID image;
	byte subcoord_x;
	byte subcoord_y;
	byte width;
	byte height;
} DrawTrackSeqStruct;

#define TILE_SEQ_BEGIN(x) { x, 0, 0, 0, 0 },
#define TILE_SEQ_LINE(a, b, c, d, e) { a, b, c, d, e },
#define TILE_SEQ_END() { 0, 0, 0, 0, 0 }

static const DrawTrackSeqStruct _track_depot_layout_table_0[] = {
	TILE_SEQ_BEGIN(0xF8D)
	TILE_SEQ_LINE(0x42B | PALETTE_MODIFIER_COLOR, 2, 13, 13, 1)
	TILE_SEQ_END()
};

static const DrawTrackSeqStruct _track_depot_layout_table_1[] = {
	TILE_SEQ_BEGIN(0x3F3 | PALETTE_MODIFIER_COLOR)
	TILE_SEQ_LINE(0x427 | PALETTE_MODIFIER_COLOR, 2, 2, 1, 13)
	TILE_SEQ_LINE(0x428 | PALETTE_MODIFIER_COLOR, 13, 2, 1, 13)
	TILE_SEQ_END()
};

static const DrawTrackSeqStruct _track_depot_layout_table_2[] = {
	TILE_SEQ_BEGIN(0x3F4 | PALETTE_MODIFIER_COLOR)
	TILE_SEQ_LINE(0x429 | PALETTE_MODIFIER_COLOR, 2, 2, 13, 1)
	TILE_SEQ_LINE(0x42A | PALETTE_MODIFIER_COLOR, 2, 13, 13, 1)
	TILE_SEQ_END()
};

static const DrawTrackSeqStruct _track_depot_layout_table_3[] = {
	TILE_SEQ_BEGIN(0xF8D)
	TILE_SEQ_LINE(0x42C | PALETTE_MODIFIER_COLOR, 13, 2, 1, 13)
	TILE_SEQ_END()
};

static const DrawTrackSeqStruct _track_waypoint_table_0[] = {
	TILE_SEQ_BEGIN(0x3F4 | PALETTE_MODIFIER_COLOR)
	TILE_SEQ_LINE(PALETTE_MODIFIER_COLOR | SPR_WAYPOINT_X_1,  0,  0,  16,  5)
	TILE_SEQ_LINE(PALETTE_MODIFIER_COLOR | SPR_WAYPOINT_X_2,  0, 11,  16,  5)
	TILE_SEQ_END()
};

static const DrawTrackSeqStruct _track_waypoint_table_1[] = {
	TILE_SEQ_BEGIN(0x3F3 | PALETTE_MODIFIER_COLOR)
	TILE_SEQ_LINE(PALETTE_MODIFIER_COLOR | SPR_WAYPOINT_Y_1,   0,  0, 5, 16)
	TILE_SEQ_LINE(PALETTE_MODIFIER_COLOR | SPR_WAYPOINT_Y_2,  11,  0, 5, 16)
	TILE_SEQ_END()
};


static const DrawTrackSeqStruct* const _track_depot_layout_table[] = {
	_track_depot_layout_table_0,
	_track_depot_layout_table_1,
	_track_depot_layout_table_2,
	_track_depot_layout_table_3,

	_track_waypoint_table_0,
	_track_waypoint_table_1,
};
