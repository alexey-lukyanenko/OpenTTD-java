/* $Id: rail_gui.c 3270 2005-12-07 15:48:52Z peter1138 $ */

/** @file rail_gui.c File for dealing with rail construction user interface */

#include "stdafx.h"
#include "openttd.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "functions.h"
#include "map.h"
#include "tile.h"
#include "window.h"
#include "gui.h"
#include "viewport.h"
#include "gfx.h"
#include "sound.h"
#include "command.h"
#include "vehicle.h"
#include "station.h"
#include "waypoint.h"
#include "debug.h"
#include "variables.h"

static RailType _cur_railtype;
static bool _remove_button_clicked;
static byte _build_depot_direction;
static byte _waypoint_count = 1;
static byte _cur_waypoint_type;
static byte _cur_signal_type;
static byte _cur_presig_type;
static bool _cur_autosig_compl;
 
static const StringID _presig_types_dropdown[] = {
	STR_SIGNAL_NORMAL,
	STR_SIGNAL_ENTRANCE,
	STR_SIGNAL_EXIT,
	STR_SIGNAL_COMBO,
	STR_SIGNAL_PBS,
	INVALID_STRING_ID
};

static struct {
	byte orientation;
	byte numtracks;
	byte platlength;
	bool dragdrop;
} _railstation;


static void HandleStationPlacement(TileIndex start, TileIndex end);
static void ShowBuildTrainDepotPicker(void);
static void ShowBuildWaypointPicker(void);
static void ShowStationBuilder(void);
static void ShowSignalBuilder(void);

void CcPlaySound1E(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) SndPlayTileFx(SND_20_SPLAT_2, tile);
}

static void GenericPlaceRail(TileIndex tile, int cmd)
{
	DoCommandP(tile, _cur_railtype, cmd, CcPlaySound1E,
		_remove_button_clicked ?
		CMD_REMOVE_SINGLE_RAIL | CMD_MSG(STR_1012_CAN_T_REMOVE_RAILROAD_TRACK) | CMD_AUTO | CMD_NO_WATER :
		CMD_BUILD_SINGLE_RAIL | CMD_MSG(STR_1011_CAN_T_BUILD_RAILROAD_TRACK) | CMD_AUTO | CMD_NO_WATER
	);
}

static void PlaceRail_N(TileIndex tile)
{
	int cmd = _tile_fract_coords.x > _tile_fract_coords.y ? 4 : 5;
	GenericPlaceRail(tile, cmd);
}

static void PlaceRail_NE(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_Y);
}

static void PlaceRail_E(TileIndex tile)
{
	int cmd = _tile_fract_coords.x + _tile_fract_coords.y <= 15 ? 2 : 3;
	GenericPlaceRail(tile, cmd);
}

static void PlaceRail_NW(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_X);
}

static void PlaceRail_AutoRail(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_RAILDIRS);
}

static void PlaceExtraDepotRail(TileIndex tile, uint16 extra)
{
	byte b = _m[tile].m5;

	if (GB(b, 6, 2) != RAIL_TYPE_NORMAL >> 6) return;
	if (!(b & (extra >> 8))) return;

	DoCommandP(tile, _cur_railtype, extra & 0xFF, NULL, CMD_BUILD_SINGLE_RAIL | CMD_AUTO | CMD_NO_WATER);
}

static const uint16 _place_depot_extra[12] = {
	0x604,		0x2102,		0x1202,		0x505,
	0x2400,		0x2801,		0x1800,		0x1401,
	0x2203,		0x904,		0x0A05,		0x1103,
};


void CcRailDepot(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		int dir = p2;

		SndPlayTileFx(SND_20_SPLAT_2, tile);
		ResetObjectToPlace();

		tile += TileOffsByDir(dir);

		if (IsTileType(tile, MP_RAILWAY)) {
			PlaceExtraDepotRail(tile, _place_depot_extra[dir]);
			PlaceExtraDepotRail(tile, _place_depot_extra[dir + 4]);
			PlaceExtraDepotRail(tile, _place_depot_extra[dir + 8]);
		}
	}
}

static void PlaceRail_Depot(TileIndex tile)
{
	DoCommandP(tile, _cur_railtype, _build_depot_direction, CcRailDepot,
		CMD_BUILD_TRAIN_DEPOT | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_100E_CAN_T_BUILD_TRAIN_DEPOT));
}

static void PlaceRail_Waypoint(TileIndex tile)
{
	if (!_remove_button_clicked) {
		DoCommandP(tile, _cur_waypoint_type, 0, CcPlaySound1E, CMD_BUILD_TRAIN_WAYPOINT | CMD_MSG(STR_CANT_BUILD_TRAIN_WAYPOINT));
	} else {
		DoCommandP(tile, 0, 0, CcPlaySound1E, CMD_REMOVE_TRAIN_WAYPOINT | CMD_MSG(STR_CANT_REMOVE_TRAIN_WAYPOINT));
	}
}

void CcStation(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_20_SPLAT_2, tile);
		ResetObjectToPlace();
	}
}

static void PlaceRail_Station(TileIndex tile)
{
	if(_remove_button_clicked)
		DoCommandP(tile, 0, 0, CcPlaySound1E, CMD_REMOVE_FROM_RAILROAD_STATION | CMD_MSG(STR_CANT_REMOVE_PART_OF_STATION));
	else if (_railstation.dragdrop) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED);
		VpSetPlaceSizingLimit(_patches.station_spread);
	} else {
		// TODO: Custom station selector GUI. Now we just try using first custom station
		// (and fall back to normal stations if it isn't available).
		DoCommandP(tile, _railstation.orientation | (_railstation.numtracks<<8) | (_railstation.platlength<<16),_cur_railtype|1<<4, CcStation,
				CMD_BUILD_RAILROAD_STATION | CMD_NO_WATER | CMD_AUTO | CMD_MSG(STR_100F_CAN_T_BUILD_RAILROAD_STATION));
	}
}

static void GenericPlaceSignals(TileIndex tile)
{
	uint trackstat;
	uint i;

	trackstat = (byte)GetTileTrackStatus(tile, TRANSPORT_RAIL);

	if ((trackstat & 0x30)) // N-S direction
		trackstat = (_tile_fract_coords.x <= _tile_fract_coords.y) ? 0x20 : 0x10;

	if ((trackstat & 0x0C)) // E-W direction
		trackstat = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? 4 : 8;

	// Lookup the bit index
	i = 0;
	if (trackstat != 0) {
		for (; !(trackstat & 1); trackstat >>= 1) i++;
	}

	if (!_remove_button_clicked) {
		DoCommandP(tile, i + (_ctrl_pressed ? 8 : 0) +
		                 (!HASBIT(_cur_signal_type, 0) != !_ctrl_pressed ? 16 : 0) +
		                 (_cur_presig_type << 5) ,
		                 0, CcPlaySound1E, CMD_BUILD_SIGNALS | CMD_AUTO | CMD_MSG(STR_1010_CAN_T_BUILD_SIGNALS_HERE));
	} else {
		DoCommandP(tile, i, 0, CcPlaySound1E,
			CMD_REMOVE_SIGNALS | CMD_AUTO | CMD_MSG(STR_1013_CAN_T_REMOVE_SIGNALS_FROM));
	}
}

static void PlaceRail_Bridge(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_OR_Y);
}

void CcBuildRailTunnel(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_20_SPLAT_2, tile);
		ResetObjectToPlace();
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

static void PlaceRail_Tunnel(TileIndex tile)
{
	DoCommandP(tile, _cur_railtype, 0, CcBuildRailTunnel,
		CMD_BUILD_TUNNEL | CMD_AUTO | CMD_MSG(STR_5016_CAN_T_BUILD_TUNNEL_HERE));
}

void PlaceProc_BuyLand(TileIndex tile)
{
	DoCommandP(tile, 0, 0, CcPlaySound1E, CMD_PURCHASE_LAND_AREA | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_5806_CAN_T_PURCHASE_THIS_LAND));
}

static void PlaceRail_ConvertRail(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_AND_Y | GUI_PlaceProc_ConvertRailArea);
}

static void PlaceRail_AutoSignals(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_SIGNALDIRS);
}

static void BuildRailClick_N(Window *w)
{
	HandlePlacePushButton(w, 4, GetRailTypeInfo(_cur_railtype)->cursor.rail_ns, 1, PlaceRail_N);
}

static void BuildRailClick_NE(Window *w)
{
	HandlePlacePushButton(w, 5, GetRailTypeInfo(_cur_railtype)->cursor.rail_swne, 1, PlaceRail_NE);
}

static void BuildRailClick_E(Window *w)
{
	HandlePlacePushButton(w, 6, GetRailTypeInfo(_cur_railtype)->cursor.rail_ew, 1, PlaceRail_E);
}

static void BuildRailClick_NW(Window *w)
{
	HandlePlacePushButton(w, 7, GetRailTypeInfo(_cur_railtype)->cursor.rail_nwse, 1, PlaceRail_NW);
}

static void BuildRailClick_AutoRail(Window *w)
{
	HandlePlacePushButton(w, 8, GetRailTypeInfo(_cur_railtype)->cursor.autorail, VHM_RAIL, PlaceRail_AutoRail);
}

static void BuildRailClick_Demolish(Window *w)
{
	HandlePlacePushButton(w, 9, ANIMCURSOR_DEMOLISH, 1, PlaceProc_DemolishArea);
}

static void BuildRailClick_Depot(Window *w)
{
	if (HandlePlacePushButton(w, 10, GetRailTypeInfo(_cur_railtype)->cursor.depot, 1, PlaceRail_Depot)) {
		ShowBuildTrainDepotPicker();
	}
}

static void BuildRailClick_Waypoint(Window *w)
{
	_waypoint_count = GetNumCustomStations(STAT_CLASS_WAYP);
	if (HandlePlacePushButton(w, 11, SPR_CURSOR_WAYPOINT, 1, PlaceRail_Waypoint) &&
			_waypoint_count > 1) {
		ShowBuildWaypointPicker();
	}
}

static void BuildRailClick_Station(Window *w)
{
	if (HandlePlacePushButton(w, 12, SPR_CURSOR_RAIL_STATION, 1, PlaceRail_Station)) ShowStationBuilder();
}

static void BuildRailClick_AutoSignals(Window *w)
{
	if (HandlePlacePushButton(w, 13, ANIMCURSOR_BUILDSIGNALS, VHM_RECT, PlaceRail_AutoSignals))
		ShowSignalBuilder();
}

static void BuildRailClick_Bridge(Window *w)
{
	HandlePlacePushButton(w, 14, SPR_CURSOR_BRIDGE, 1, PlaceRail_Bridge);
}

static void BuildRailClick_Tunnel(Window *w)
{
	HandlePlacePushButton(w, 15, GetRailTypeInfo(_cur_railtype)->cursor.tunnel, 3, PlaceRail_Tunnel);
}

static void BuildRailClick_Remove(Window *w)
{
	if (HASBIT(w->disabled_state, 16)) return;
	SetWindowDirty(w);
	SndPlayFx(SND_15_BEEP);

	TOGGLEBIT(w->click_state, 16);
	_remove_button_clicked = HASBIT(w->click_state, 16) != 0;
	SetSelectionRed(HASBIT(w->click_state, 16) != 0);

	// handle station builder
	if (HASBIT(w->click_state, 16)) {
		if (_remove_button_clicked) {
			SetTileSelectSize(1, 1);
		} else {
			BringWindowToFrontById(WC_BUILD_STATION, 0);
		}
	}
}

static void BuildRailClick_Convert(Window *w)
{
	HandlePlacePushButton(w, 17, GetRailTypeInfo(_cur_railtype)->cursor.convert, 1, PlaceRail_ConvertRail);
}

static void BuildRailClick_Landscaping(Window *w)
{
	ShowTerraformToolbar();
}

static void DoRailroadTrack(int mode)
{
	DoCommandP(TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), _cur_railtype | (mode << 4), NULL,
		_remove_button_clicked ?
		CMD_REMOVE_RAILROAD_TRACK | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1012_CAN_T_REMOVE_RAILROAD_TRACK) :
		CMD_BUILD_RAILROAD_TRACK  | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1011_CAN_T_BUILD_RAILROAD_TRACK)
	);
}

static void HandleAutodirPlacement(void)
{
	TileHighlightData *thd = &_thd;
	int trackstat = thd->drawstyle & 0xF; // 0..5

	if (thd->drawstyle & HT_RAIL) { // one tile case
		GenericPlaceRail(TileVirtXY(thd->selend.x, thd->selend.y), trackstat);
		return;
	}

	DoRailroadTrack(trackstat);
}

static void HandleAutoSignalPlacement(void)
{
	TileHighlightData *thd = &_thd;
	byte trackstat = thd->drawstyle & 0xF; // 0..5

	if (thd->drawstyle == HT_RECT) { // one tile case
		GenericPlaceSignals(TileVirtXY(thd->selend.x, thd->selend.y));
		return;
	}

	// _patches.drag_signals_density is given as a parameter such that each user in a network
	// game can specify his/her own signal density
	DoCommandP(
		TileVirtXY(thd->selstart.x, thd->selstart.y),
		TileVirtXY(thd->selend.x, thd->selend.y),
		(_ctrl_pressed ? 1 << 3 : 0) | (trackstat << 4) | (_patches.drag_signals_density << 24),
		CcPlaySound1E,
		_remove_button_clicked ?
			CMD_REMOVE_SIGNAL_TRACK | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1013_CAN_T_REMOVE_SIGNALS_FROM) :
			CMD_BUILD_SIGNAL_TRACK  | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1010_CAN_T_BUILD_SIGNALS_HERE)
	);
	(!HASBIT(_cur_signal_type, 0) != !_ctrl_pressed ? 1 << 3 : 0) | 
	(trackstat << 4) |
	(_patches.drag_signals_density << 24) | 
	(_cur_autosig_compl ? 2 : 0),
	CcPlaySound1E,
	(_remove_button_clicked ? CMD_REMOVE_SIGNAL_TRACK | CMD_AUTO | CMD_NO_WATER | 
	CMD_MSG(STR_1013_CAN_T_REMOVE_SIGNALS_FROM) :
        CMD_BUILD_SIGNAL_TRACK  | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1010_CAN_T_BUILD_SIGNALS_HERE) );
}


typedef void OnButtonClick(Window *w);

static OnButtonClick * const _build_railroad_button_proc[] = {
	BuildRailClick_N,
	BuildRailClick_NE,
	BuildRailClick_E,
	BuildRailClick_NW,
	BuildRailClick_AutoRail,
	BuildRailClick_Demolish,
	BuildRailClick_Depot,
	BuildRailClick_Waypoint,
	BuildRailClick_Station,
	BuildRailClick_AutoSignals,
	BuildRailClick_Bridge,
	BuildRailClick_Tunnel,
	BuildRailClick_Remove,
	BuildRailClick_Convert,
	BuildRailClick_Landscaping,
};

static const uint16 _rail_keycodes[] = {
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7', // depot
	'8', // waypoint
	'9', // station
	'S', // signals
	'B', // bridge
	'T', // tunnel
	'R', // remove
	'C', // convert rail
	'L', // landscaping
};


static void BuildRailToolbWndProc(Window *w, WindowEvent *e)
{
	switch (e->event) {
	case WE_PAINT:
		w->disabled_state &= ~(1 << 16);
		if (!(w->click_state & ((1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<11)|(1<<12)|(1<<13)))) {
			w->disabled_state |= (1 << 16);
			w->click_state &= ~(1<<16);
		}
		DrawWindowWidgets(w);
		break;

	case WE_CLICK:
		if (e->click.widget >= 4) {
			_remove_button_clicked = false;
			_build_railroad_button_proc[e->click.widget - 4](w);
		}
	break;

	case WE_KEYPRESS: {
		uint i;

		for (i = 0; i != lengthof(_rail_keycodes); i++) {
			if (e->keypress.keycode == _rail_keycodes[i]) {
				e->keypress.cont = false;
				_remove_button_clicked = false;
				_build_railroad_button_proc[i](w);
				break;
			}
		}
		MarkTileDirty(_thd.pos.x, _thd.pos.y); // redraw tile selection
		break;
	}

	case WE_PLACE_OBJ:
		_place_proc(e->place.tile);
		return;

	case WE_PLACE_DRAG: {
		VpSelectTilesWithMethod(e->place.pt.x, e->place.pt.y, e->place.userdata & 0xF);
		return;
	}

	case WE_PLACE_MOUSEUP:
		if (e->click.pt.x != -1) {
			TileIndex start_tile = e->place.starttile;
			TileIndex end_tile = e->place.tile;

			if (e->place.userdata == VPM_X_OR_Y) {
				ResetObjectToPlace();
				ShowBuildBridgeWindow(start_tile, end_tile, _cur_railtype);
			} else if (e->place.userdata == VPM_RAILDIRS) {
				bool old = _remove_button_clicked;
				if (_ctrl_pressed) _remove_button_clicked = true;
				HandleAutodirPlacement();
				_remove_button_clicked = old;
			} else if (e->place.userdata == VPM_SIGNALDIRS) {
				HandleAutoSignalPlacement();
			} else if ((e->place.userdata & 0xF) == VPM_X_AND_Y) {
				if (GUIPlaceProcDragXY(e)) break;

				if ((e->place.userdata >> 4) == GUI_PlaceProc_ConvertRailArea >> 4)
					DoCommandP(end_tile, start_tile, _cur_railtype, CcPlaySound10, CMD_CONVERT_RAIL | CMD_MSG(STR_CANT_CONVERT_RAIL));
			} else if (e->place.userdata == VPM_X_AND_Y_LIMITED) {
				HandleStationPlacement(start_tile, end_tile);
			} else
				DoRailroadTrack(e->place.userdata & 1);
		}
		break;

	case WE_ABORT_PLACE_OBJ:
		UnclickWindowButtons(w);
		SetWindowDirty(w);

		w = FindWindowById(WC_BUILD_STATION, 0);
		if (w != NULL) WP(w,def_d).close = true;
		w = FindWindowById(WC_BUILD_DEPOT, 0);
		if (w != NULL) WP(w,def_d).close = true;
		w = FindWindowById(WC_BUILD_SIGNALS, 0);
		if (w != NULL) WP(w,def_d).close=true;

		break;

	case WE_PLACE_PRESIZE: {
		TileIndex tile = e->place.tile;

		DoCommandByTile(tile, 0, 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	} break;

	case WE_DESTROY:
		if (_patches.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0);
		break;
	}
}


static const Widget _build_rail_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   359,     0,    13, STR_100A_RAILROAD_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,     7,   360,   371,     0,    13, 0x0,     STR_STICKY_BUTTON},

{      WWT_PANEL,   RESIZE_NONE,     7,   110,   113,    14,    35, 0x0,			STR_NULL},

{      WWT_PANEL,   RESIZE_NONE,     7,    0,     21,    14,    35, 0x4E3,		STR_1018_BUILD_RAILROAD_TRACK},
{      WWT_PANEL,   RESIZE_NONE,     7,    22,    43,    14,    35, 0x4E4,		STR_1018_BUILD_RAILROAD_TRACK},
{      WWT_PANEL,   RESIZE_NONE,     7,    44,    65,    14,    35, 0x4E5,		STR_1018_BUILD_RAILROAD_TRACK},
{      WWT_PANEL,   RESIZE_NONE,     7,    66,    87,    14,    35, 0x4E6,		STR_1018_BUILD_RAILROAD_TRACK},
{      WWT_PANEL,   RESIZE_NONE,     7,    88,   109,    14,    35, SPR_IMG_AUTORAIL, STR_BUILD_AUTORAIL_TIP},

{      WWT_PANEL,   RESIZE_NONE,     7,   114,   135,    14,    35, 0x2BF,		STR_018D_DEMOLISH_BUILDINGS_ETC},
{      WWT_PANEL,   RESIZE_NONE,     7,   136,   157,    14,    35, 0x50E,		STR_1019_BUILD_TRAIN_DEPOT_FOR_BUILDING},
{      WWT_PANEL,   RESIZE_NONE,     7,   158,   179,    14,    35, SPR_IMG_WAYPOINT, STR_CONVERT_RAIL_TO_WAYPOINT_TIP},

{      WWT_PANEL,   RESIZE_NONE,     7,   180,   221,    14,    35, 0x512,		STR_101A_BUILD_RAILROAD_STATION},
{      WWT_PANEL,   RESIZE_NONE,     7,   222,   243,    14,    35, 0x50B,		STR_101B_BUILD_RAILROAD_SIGNALS},
{      WWT_PANEL,   RESIZE_NONE,     7,   244,   285,    14,    35, 0xA22,		STR_101C_BUILD_RAILROAD_BRIDGE},
{      WWT_PANEL,   RESIZE_NONE,     7,   286,   305,    14,    35, SPR_IMG_TUNNEL_RAIL, STR_101D_BUILD_RAILROAD_TUNNEL},
{      WWT_PANEL,   RESIZE_NONE,     7,   306,   327,    14,    35, 0x2CA,		STR_101E_TOGGLE_BUILD_REMOVE_FOR},
{      WWT_PANEL,   RESIZE_NONE,     7,   328,   349,    14,    35, SPR_IMG_CONVERT_RAIL, STR_CONVERT_RAIL_TIP},

{      WWT_PANEL,   RESIZE_NONE,     7,   350,   371,    14,    35, SPR_IMG_LANDSCAPING,	STR_LANDSCAPING_TOOLBAR_TIP},

{   WIDGETS_END},
};

static const WindowDesc _build_rail_desc = {
	640-372, 22, 372, 36,
	WC_BUILD_TOOLBAR,0,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON,
	_build_rail_widgets,
	BuildRailToolbWndProc
};

/** Enum referring to the widgets of the build rail toolbar
 */
typedef enum {
	RTW_CAPTION = 1,
	RTW_BUILD_NS = 4,
	RTW_BUILD_X = 5,
	RTW_BUILD_EW = 6,
	RTW_BUILD_Y = 7,
	RTW_AUTORAIL = 8,
	RTW_BUILD_DEPOT = 10,
	RTW_BUILD_TUNNEL = 15,
	RTW_CONVERT_RAIL = 17
} RailToolbarWidgets;

/** Configures the rail toolbar for railtype given
 * @param railtype the railtype to display
 * @param w the window to modify
 */
static void SetupRailToolbar(RailType railtype, Window* w)
{
	const RailtypeInfo *rti = GetRailTypeInfo(railtype);

	assert(railtype < RAILTYPE_END);
	w->widget[RTW_CAPTION].unkA = rti->strings.toolbar_caption;
	w->widget[RTW_BUILD_NS].unkA = rti->gui_sprites.build_ns_rail;
	w->widget[RTW_BUILD_X].unkA = rti->gui_sprites.build_x_rail;
	w->widget[RTW_BUILD_EW].unkA = rti->gui_sprites.build_ew_rail;
	w->widget[RTW_BUILD_Y].unkA = rti->gui_sprites.build_y_rail;
	w->widget[RTW_AUTORAIL].unkA = rti->gui_sprites.auto_rail;
	w->widget[RTW_BUILD_DEPOT].unkA = rti->gui_sprites.build_depot;
	w->widget[RTW_CONVERT_RAIL].unkA = rti->gui_sprites.convert_rail;
	w->widget[RTW_BUILD_TUNNEL].unkA = rti->gui_sprites.build_tunnel;
}

void ShowBuildRailToolbar(RailType railtype, int button)
{
	Window *w;

	if (_current_player == OWNER_SPECTATOR) return;

	// don't recreate the window if we're clicking on a button and the window exists.
	if (button < 0 || !(w = FindWindowById(WC_BUILD_TOOLBAR, 0)) || w->wndproc != BuildRailToolbWndProc) {
		DeleteWindowById(WC_BUILD_TOOLBAR, 0);
		_cur_railtype = railtype;
		w = AllocateWindowDesc(&_build_rail_desc);
		SetupRailToolbar(railtype, w);
	}

	_remove_button_clicked = false;
	if (w != NULL && button >= 0) _build_railroad_button_proc[button](w);
	if (_patches.link_terraform_toolbar) ShowTerraformToolbar();
}

/* TODO: For custom stations, respect their allowed platforms/lengths bitmasks!
 * --pasky */

static void HandleStationPlacement(TileIndex start, TileIndex end)
{
	uint sx = TileX(start);
	uint sy = TileY(start);
	uint ex = TileX(end);
	uint ey = TileY(end);
	uint w,h;

	if (sx > ex) uintswap(sx,ex);
	if (sy > ey) uintswap(sy,ey);
	w = ex - sx + 1;
	h = ey - sy + 1;
	if (!_railstation.orientation) uintswap(w,h);

	// TODO: Custom station selector GUI. Now we just try using first custom station
	// (and fall back to normal stations if it isn't available).
	DoCommandP(TileXY(sx, sy), _railstation.orientation | (w << 8) | (h << 16), _cur_railtype | 1 << 4, CcStation,
		CMD_BUILD_RAILROAD_STATION | CMD_NO_WATER | CMD_AUTO | CMD_MSG(STR_100F_CAN_T_BUILD_RAILROAD_STATION));
}

static void StationBuildWndProc(Window *w, WindowEvent *e)
{
	switch (e->event) {
	case WE_PAINT: {
		int rad;
		uint bits;

		if (WP(w,def_d).close) return;

		bits = (1<<3) << ( _railstation.orientation);
		if (_railstation.dragdrop) {
			bits |= (1<<19);
		} else {
			bits |= (1<<(5-1)) << (_railstation.numtracks);
			bits |= (1<<(12-1)) << (_railstation.platlength);
		}
		bits |= (1<<20) << (_station_show_coverage);
		w->click_state = bits;

		if (_railstation.dragdrop) {
			SetTileSelectSize(1, 1);
		} else {
			int x = _railstation.numtracks;
			int y = _railstation.platlength;
			if (_railstation.orientation == 0) intswap(x,y);
			if(!_remove_button_clicked)
				SetTileSelectSize(x, y);
		}

		rad = (_patches.modified_catchment) ? CA_TRAIN : 4;

		if (_station_show_coverage)
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);

		/* Update buttons for correct spread value */
		w->disabled_state = 0;
		for (bits = _patches.station_spread; bits < 7; bits++) {
			SETBIT(w->disabled_state, bits + 5);
			SETBIT(w->disabled_state, bits + 12);
		}

		DrawWindowWidgets(w);

		StationPickerDrawSprite(39, 42, _cur_railtype, 2);
		StationPickerDrawSprite(107, 42, _cur_railtype, 3);

		DrawStringCentered(74, 15, STR_3002_ORIENTATION, 0);
		DrawStringCentered(74, 76, STR_3003_NUMBER_OF_TRACKS, 0);
		DrawStringCentered(74, 101, STR_3004_PLATFORM_LENGTH, 0);
		DrawStringCentered(74, 141, STR_3066_COVERAGE_AREA_HIGHLIGHT, 0);

		DrawStationCoverageAreaText(2, 166, (uint)-1, rad);
	} break;

	case WE_CLICK: {
		switch (e->click.widget) {
		case 3:
		case 4:
			_railstation.orientation = e->click.widget - 3;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;

		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			_railstation.numtracks = (e->click.widget - 5) + 1;
			_railstation.dragdrop = false;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;

		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
			_railstation.platlength = (e->click.widget - 12) + 1;
			_railstation.dragdrop = false;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;

		case 19:
			_railstation.dragdrop ^= true;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;

		case 20:
		case 21:
			_station_show_coverage = e->click.widget - 20;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;
		}
	} break;

	case WE_MOUSELOOP:
		if (WP(w,def_d).close) {
			DeleteWindow(w);
			return;
		}
		CheckRedrawStationCoverage(w);
		break;

	case WE_DESTROY:
		if (!WP(w,def_d).close) ResetObjectToPlace();
		break;
	}
}

static const Widget _station_builder_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,		STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   147,     0,    13, STR_3000_RAIL_STATION_SELECTION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   147,    14,   199, 0x0,					STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,    14,     7,    72,    26,    73, 0x0,					STR_304E_SELECT_RAILROAD_STATION},
{      WWT_PANEL,   RESIZE_NONE,    14,    75,   140,    26,    73, 0x0,					STR_304E_SELECT_RAILROAD_STATION},

{    WWT_TEXTBTN,   RESIZE_NONE,    14,    22,    36,    87,    98, STR_00CB_1,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    37,    51,    87,    98, STR_00CC_2,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    52,    66,    87,    98, STR_00CD_3,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    67,    81,    87,    98, STR_00CE_4,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    82,    96,    87,    98, STR_00CF_5,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    97,   111,    87,    98, STR_0335_6,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,   112,   126,    87,    98, STR_0336_7,	STR_304F_SELECT_NUMBER_OF_PLATFORMS},

{    WWT_TEXTBTN,   RESIZE_NONE,    14,    22,    36,   112,   123, STR_00CB_1,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    37,    51,   112,   123, STR_00CC_2,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    52,    66,   112,   123, STR_00CD_3,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    67,    81,   112,   123, STR_00CE_4,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    82,    96,   112,   123, STR_00CF_5,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    97,   111,   112,   123, STR_0335_6,	STR_3050_SELECT_LENGTH_OF_RAILROAD},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,   112,   126,   112,   123, STR_0336_7,	STR_3050_SELECT_LENGTH_OF_RAILROAD},

{    WWT_TEXTBTN,   RESIZE_NONE,    14,    37,   111,   126,   137, STR_DRAG_DROP, STR_STATION_DRAG_DROP},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    14,    73,   152,   163, STR_02DB_OFF, STR_3065_DON_T_HIGHLIGHT_COVERAGE},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    74,   133,   152,   163, STR_02DA_ON, STR_3064_HIGHLIGHT_COVERAGE_AREA},
{   WIDGETS_END},
};

static const WindowDesc _station_builder_desc = {
	-1, -1, 148, 200,
	WC_BUILD_STATION,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_station_builder_widgets,
	StationBuildWndProc
};

static void ShowStationBuilder(void)
{
	AllocateWindowDesc(&_station_builder_desc);
}

static void BuildTrainDepotWndProc(Window *w, WindowEvent *e)
{
	switch(e->event) {
	case WE_PAINT: {
		RailType r;

		w->click_state = (1 << 3) << _build_depot_direction;
		DrawWindowWidgets(w);

		r = _cur_railtype;
		DrawTrainDepotSprite(70, 17, 0, r);
		DrawTrainDepotSprite(70, 69, 1, r);
		DrawTrainDepotSprite( 2, 69, 2, r);
		DrawTrainDepotSprite( 2, 17, 3, r);
		break;
		}

	case WE_CLICK:
		switch (e->click.widget) {
			case 3:
			case 4:
			case 5:
			case 6:
				_build_depot_direction = e->click.widget - 3;
				SndPlayFx(SND_15_BEEP);
				SetWindowDirty(w);
				break;
		}
		break;

	case WE_MOUSELOOP:
		if (WP(w,def_d).close) DeleteWindow(w);
		return;

	case WE_DESTROY:
		if (!WP(w,def_d).close) ResetObjectToPlace();
		break;
	}
}

static const Widget _build_depot_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   139,     0,    13, STR_1014_TRAIN_DEPOT_ORIENTATION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   139,    14,   121, 0x0,			STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    17,    66, 0x0,			STR_1020_SELECT_RAILROAD_DEPOT_ORIENTATIO},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    69,   118, 0x0,			STR_1020_SELECT_RAILROAD_DEPOT_ORIENTATIO},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    69,   118, 0x0,			STR_1020_SELECT_RAILROAD_DEPOT_ORIENTATIO},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    17,    66, 0x0,			STR_1020_SELECT_RAILROAD_DEPOT_ORIENTATIO},
{   WIDGETS_END},
};

static const WindowDesc _build_depot_desc = {
	-1,-1, 140, 122,
	WC_BUILD_DEPOT,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_build_depot_widgets,
	BuildTrainDepotWndProc
};

static void ShowBuildTrainDepotPicker(void)
{
	AllocateWindowDesc(&_build_depot_desc);
}


static void BuildWaypointWndProc(Window *w, WindowEvent *e)
{
	switch (e->event) {
	case WE_PAINT: {
		uint i;

		w->click_state = (1 << 3) << (_cur_waypoint_type - w->hscroll.pos);
		DrawWindowWidgets(w);

		for (i = 0; i < 5; i++) {
			if (w->hscroll.pos + i < _waypoint_count) {
				DrawWaypointSprite(2 + i * 68, 25, w->hscroll.pos + i, _cur_railtype);
			}
		}
		break;
	}
	case WE_CLICK: {
		switch (e->click.widget) {
		case 3: case 4: case 5: case 6: case 7:
			_cur_waypoint_type = e->click.widget - 3 + w->hscroll.pos;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;
		}
		break;
	}

	case WE_MOUSELOOP:
		if (WP(w,def_d).close) DeleteWindow(w);
		break;

	case WE_DESTROY:
		if (!WP(w,def_d).close) ResetObjectToPlace();
		break;
	}
}

static const Widget _build_waypoint_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5, STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   343,     0,    13, STR_WAYPOINT,STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   343,    14,    91, 0x0, 0},

{      WWT_PANEL,   RESIZE_NONE,     7,     3,    68,    17,    76, 0x0, STR_WAYPOINT_GRAPHICS_TIP},
{      WWT_PANEL,   RESIZE_NONE,     7,    71,   136,    17,    76, 0x0, STR_WAYPOINT_GRAPHICS_TIP},
{      WWT_PANEL,   RESIZE_NONE,     7,   139,   204,    17,    76, 0x0, STR_WAYPOINT_GRAPHICS_TIP},
{      WWT_PANEL,   RESIZE_NONE,     7,   207,   272,    17,    76, 0x0, STR_WAYPOINT_GRAPHICS_TIP},
{      WWT_PANEL,   RESIZE_NONE,     7,   275,   340,    17,    76, 0x0, STR_WAYPOINT_GRAPHICS_TIP},

{ WWT_HSCROLLBAR,   RESIZE_NONE,    7,     1,   343,     80,    91, 0x0, STR_0190_SCROLL_BAR_SCROLLS_LIST},
{    WIDGETS_END},
};

static const WindowDesc _build_waypoint_desc = {
	-1,-1, 344, 92,
	WC_BUILD_DEPOT,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_build_waypoint_widgets,
	BuildWaypointWndProc
};

static void ShowBuildWaypointPicker(void)
{
	Window *w = AllocateWindowDesc(&_build_waypoint_desc);
	w->hscroll.cap = 5;
	w->hscroll.count = _waypoint_count;
}

static void BuildSignalWndProc(Window *w, WindowEvent *e)
{
	switch(e->event) {
	case WE_PAINT: {
		/* XXX TODO: dont always hide the buttons when more than 2 signal types are available */
		w->hidden_state = (1 << 3) | (1 << 6);

		/* XXX TODO: take into account the scroll position for setting the click state */
		w->click_state = ((1 << 4) << _cur_signal_type) | (_cur_autosig_compl ? 1 << 9 : 0);

		SetDParam(10, _presig_types_dropdown[_cur_presig_type]);
		DrawWindowWidgets(w);

		// Draw the string for current signal type
		DrawStringCentered(69, 49, STR_SIGNAL_TYPE_STANDARD + _cur_signal_type, 0);

		// Draw the strings for drag density
		DrawStringCentered(69, 60, STR_SIGNAL_DENSITY_DESC, 0);
		SetDParam(0, _patches.drag_signals_density);
		DrawString( 50, 71, STR_SIGNAL_DENSITY_TILES , 0);

		// Draw the '<' and '>' characters for the decrease/increase buttons
		DrawStringCentered(30, 72, STR_6819, 0);
		DrawStringCentered(40, 72, STR_681A, 0);

		break;
		}
	case WE_CLICK: {
		switch(e->click.widget) {
			case 3: case 6: // scroll signal types
				/* XXX TODO: implement scrolling */
				break;
			case 4: case 5: // select signal type
				/* XXX TODO: take into account the scroll position for changing selected type */
				_cur_signal_type = e->click.widget - 4;
				SndPlayFx(SND_15_BEEP);
				SetWindowDirty(w);
				break;
			case 7: // decrease drag density
				if (_patches.drag_signals_density > 1) {
					_patches.drag_signals_density--;
					SndPlayFx(SND_15_BEEP);
					SetWindowDirty(w);
				};
				break;
			case 8: // increase drag density
				if (_patches.drag_signals_density < 20) {
					_patches.drag_signals_density++;
					SndPlayFx(SND_15_BEEP);
					SetWindowDirty(w);
				};
				break;
			case 9: // autosignal mode toggle button
				_cur_autosig_compl ^= 1;
				SndPlayFx(SND_15_BEEP);
				SetWindowDirty(w);
				break;
			case 10: case 11: // presignal-type dropdown list
				ShowDropDownMenu(w, _presig_types_dropdown, _cur_presig_type, 11, 0, 0);
				break;
		}
		break;
	case WE_DROPDOWN_SELECT: // change presignal type
		_cur_presig_type = e->dropdown.index;
		SetWindowDirty(w);
		break;
	}

	case WE_MOUSELOOP:
		if (WP(w,def_d).close)
			DeleteWindow(w);
		return;

	case WE_DESTROY:
		if (!WP(w,def_d).close)
			ResetObjectToPlace();
		break;
	}
}

static const Widget _build_signal_widgets[] = {
{   WWT_CLOSEBOX, RESIZE_NONE,    7,    0,   10,    0,   13, STR_00C5                 , STR_018B_CLOSE_WINDOW},
{   WWT_CAPTION,  RESIZE_NONE,    7,   11,  139,    0,   13, STR_SIGNAL_SELECTION     , STR_018C_WINDOW_TITLE_DRAG_THIS},
{   WWT_PANEL,    RESIZE_NONE,    7,    0,  139,   14,  114, 0x0                      , STR_NULL},
{   WWT_PANEL,    RESIZE_NONE,    7,   22,   30,   29,   39, SPR_ARROW_LEFT           , STR_SIGNAL_TYPE_TIP},
{   WWT_PANEL,    RESIZE_NONE,    7,   43,   64,   24,   45, 0x50B                    , STR_SIGNAL_TYPE_TIP},
{   WWT_PANEL,    RESIZE_NONE,    7,   75,   96,   24,   45, SPR_SEMA                 , STR_SIGNAL_TYPE_TIP},
{   WWT_PANEL,    RESIZE_NONE,    7,  109,  117,   29,   39, SPR_ARROW_RIGHT          , STR_SIGNAL_TYPE_TIP},
{   WWT_IMGBTN,   RESIZE_NONE,    3,   25,   34,   72,   80, 0x0                      , STR_SIGNAL_DENSITY_TIP},
{   WWT_IMGBTN,   RESIZE_NONE,    3,   35,   44,   72,   80, 0x0                      , STR_SIGNAL_DENSITY_TIP},
{   WWT_TEXTBTN,  RESIZE_NONE,    7,   20,  119,   84,   95, STR_SIGNAL_COMPLETION    , STR_SIGNAL_COMPLETION_TIP},
{   WWT_6,        RESIZE_NONE,    7,   10,  129,   99,  110, STR_SIGNAL_PRESIG_COMBO  , STR_SIGNAL_PRESIG_TIP},
{   WWT_CLOSEBOX, RESIZE_NONE,    7,  118,  128,  100,  109, STR_0225                 , STR_SIGNAL_PRESIG_TIP},
{   WIDGETS_END},
};

static const WindowDesc _build_signal_desc = {
	-1,-1, 140, 115,
	WC_BUILD_SIGNALS,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_build_signal_widgets,
	BuildSignalWndProc
};

static void ShowSignalBuilder(void)
{
	_cur_presig_type = 0;
	AllocateWindowDesc(&_build_signal_desc);
}



void InitializeRailGui(void)
{
	_build_depot_direction = 3;
	_railstation.numtracks = 1;
	_railstation.platlength = 1;
	_railstation.dragdrop = true;
	_cur_signal_type = 0;
	_cur_presig_type = 0;
	_cur_autosig_compl = false;
}
