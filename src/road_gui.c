/* $Id: road_gui.c 3298 2005-12-14 06:28:48Z tron $ */

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
#include "variables.h"
//needed for catchments
#include "station.h"


static void ShowBusStationPicker(void);
static void ShowTruckStationPicker(void);
static void ShowRoadDepotPicker(void);

static bool _remove_button_clicked;

static byte _place_road_flag;

static byte _road_depot_orientation;
static byte _road_station_picker_orientation;

void CcPlaySound1D(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) SndPlayTileFx(SND_1F_SPLAT, tile);
}

static void PlaceRoad_NE(TileIndex tile)
{
	_place_road_flag = (_tile_fract_coords.y >= 8) + 4;
	VpStartPlaceSizing(tile, VPM_FIX_X);
}

static void PlaceRoad_NW(TileIndex tile)
{
	_place_road_flag = (_tile_fract_coords.x >= 8) + 0;
	VpStartPlaceSizing(tile, VPM_FIX_Y);
}

static void PlaceRoad_Bridge(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_OR_Y);
}


void CcBuildRoadTunnel(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_20_SPLAT_2, tile);
		ResetObjectToPlace();
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

static void PlaceRoad_Tunnel(TileIndex tile)
{
	DoCommandP(tile, 0x200, 0, CcBuildRoadTunnel, CMD_BUILD_TUNNEL | CMD_AUTO | CMD_MSG(STR_5016_CAN_T_BUILD_TUNNEL_HERE));
}

static void BuildRoadOutsideStation(TileIndex tile, int direction)
{
	static const byte _roadbits_by_dir[4] = {2,1,8,4};
	tile += TileOffsByDir(direction);
	// if there is a roadpiece just outside of the station entrance, build a connecting route
	if (IsTileType(tile, MP_STREET) && !(_m[tile].m5 & 0x20)) {
		DoCommandP(tile, _roadbits_by_dir[direction], 0, NULL, CMD_BUILD_ROAD);
	}
}

void CcRoadDepot(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_1F_SPLAT, tile);
		ResetObjectToPlace();
		BuildRoadOutsideStation(tile, (int)p1);
	}
}

static void PlaceRoad_Depot(TileIndex tile)
{
	DoCommandP(tile, _road_depot_orientation, 0, CcRoadDepot, CMD_BUILD_ROAD_DEPOT | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1807_CAN_T_BUILD_ROAD_VEHICLE));
}

static void PlaceRoad_BusStation(TileIndex tile)
{
	DoCommandP(tile, _road_station_picker_orientation, RS_BUS, CcRoadDepot, CMD_BUILD_ROAD_STOP | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1808_CAN_T_BUILD_BUS_STATION));
}

static void PlaceRoad_TruckStation(TileIndex tile)
{
	DoCommandP(tile, _road_station_picker_orientation, RS_TRUCK, CcRoadDepot, CMD_BUILD_ROAD_STOP | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1809_CAN_T_BUILD_TRUCK_STATION));
}

static void PlaceRoad_DemolishArea(TileIndex tile)
{
	VpStartPlaceSizing(tile, 4);
}

typedef void OnButtonClick(Window *w);

static void BuildRoadClick_NE(Window *w)
{
	HandlePlacePushButton(w, 3, SPR_CURSOR_ROAD_NESW, 1, PlaceRoad_NE);
}

static void BuildRoadClick_NW(Window *w)
{
	HandlePlacePushButton(w, 4, SPR_CURSOR_ROAD_NWSE, 1, PlaceRoad_NW);
}


static void BuildRoadClick_Demolish(Window *w)
{
	HandlePlacePushButton(w, 5, ANIMCURSOR_DEMOLISH, 1, PlaceRoad_DemolishArea);
}

static void BuildRoadClick_Depot(Window *w)
{
	if (_game_mode == GM_EDITOR) return;
	if (HandlePlacePushButton(w, 6, SPR_CURSOR_ROAD_DEPOT, 1, PlaceRoad_Depot)) ShowRoadDepotPicker();
}

static void BuildRoadClick_BusStation(Window *w)
{
	if (_game_mode == GM_EDITOR) return;
	if (HandlePlacePushButton(w, 7, SPR_CURSOR_BUS_STATION, 1, PlaceRoad_BusStation)) ShowBusStationPicker();
}

static void BuildRoadClick_TruckStation(Window *w)
{
	if (_game_mode == GM_EDITOR) return;
	if (HandlePlacePushButton(w, 8, SPR_CURSOR_TRUCK_STATION, 1, PlaceRoad_TruckStation)) ShowTruckStationPicker();
}

static void BuildRoadClick_Bridge(Window *w)
{
	HandlePlacePushButton(w, 9, SPR_CURSOR_BRIDGE, 1, PlaceRoad_Bridge);
}

static void BuildRoadClick_Tunnel(Window *w)
{
	HandlePlacePushButton(w, 10, SPR_CURSOR_ROAD_TUNNEL, 3, PlaceRoad_Tunnel);
}

static void BuildRoadClick_Remove(Window *w)
{
	if (HASBIT(w->disabled_state, 11)) return;
	SetWindowDirty(w);
	SndPlayFx(SND_15_BEEP);
	TOGGLEBIT(w->click_state, 11);
	SetSelectionRed(HASBIT(w->click_state, 11) != 0);
}

static void BuildRoadClick_Landscaping(Window *w)
{
	ShowTerraformToolbar();
}

static OnButtonClick* const _build_road_button_proc[] = {
	BuildRoadClick_NE,
	BuildRoadClick_NW,
	BuildRoadClick_Demolish,
	BuildRoadClick_Depot,
	BuildRoadClick_BusStation,
	BuildRoadClick_TruckStation,
	BuildRoadClick_Bridge,
	BuildRoadClick_Tunnel,
	BuildRoadClick_Remove,
	BuildRoadClick_Landscaping,
};

static void BuildRoadToolbWndProc(Window* w, WindowEvent* e)
{
	switch (e->event) {
	case WE_PAINT:
		w->disabled_state &= ~(1 << 11);
		if (!(w->click_state & ((1<<3)|(1<<4)))) {
			w->disabled_state |= (1 << 11);
			w->click_state &= ~(1<<11);
		}
		DrawWindowWidgets(w);
		break;

	case WE_CLICK: {
		if (e->click.widget >= 3) _build_road_button_proc[e->click.widget - 3](w);
	}	break;

	case WE_KEYPRESS:
		switch (e->keypress.keycode) {
			case '1': BuildRoadClick_NE(w);           break;
			case '2': BuildRoadClick_NW(w);           break;
			case '3': BuildRoadClick_Demolish(w);     break;
			case '4': BuildRoadClick_Depot(w);        break;
			case '5': BuildRoadClick_BusStation(w);   break;
			case '6': BuildRoadClick_TruckStation(w); break;
			case 'B': BuildRoadClick_Bridge(w);       break;
			case 'T': BuildRoadClick_Tunnel(w);       break;
			case 'R': BuildRoadClick_Remove(w);       break;
			case 'L': BuildRoadClick_Landscaping(w);  break;
			default: return;
		}
		MarkTileDirty(_thd.pos.x, _thd.pos.y); // redraw tile selection
		e->keypress.cont = false;
		break;

	case WE_PLACE_OBJ:
		_remove_button_clicked = (w->click_state & (1 << 11)) != 0;
		_place_proc(e->place.tile);
		break;

	case WE_ABORT_PLACE_OBJ:
		UnclickWindowButtons(w);
		SetWindowDirty(w);

		w = FindWindowById(WC_BUS_STATION, 0);
		if (w != NULL) WP(w,def_d).close = true;
		w = FindWindowById(WC_TRUCK_STATION, 0);
		if (w != NULL) WP(w,def_d).close = true;
		w = FindWindowById(WC_BUILD_DEPOT, 0);
		if (w != NULL) WP(w,def_d).close = true;
		break;

	case WE_PLACE_DRAG: {
		int sel_method;
		if (e->place.userdata == 1) {
			sel_method = VPM_FIX_X;
			_place_road_flag = (_place_road_flag&~2) | ((e->place.pt.y&8)>>2);
		} else if (e->place.userdata == 2) {
			sel_method = VPM_FIX_Y;
			_place_road_flag = (_place_road_flag&~2) | ((e->place.pt.x&8)>>2);
		} else if (e->place.userdata == 4) {
			sel_method = VPM_X_AND_Y;
		} else {
			sel_method = VPM_X_OR_Y;
		}

		VpSelectTilesWithMethod(e->place.pt.x, e->place.pt.y, sel_method);
		return;
	}

	case WE_PLACE_MOUSEUP:
		if (e->place.pt.x != -1) {
			TileIndex start_tile = e->place.starttile;
			TileIndex end_tile = e->place.tile;

			if (e->place.userdata == 0) {
				ResetObjectToPlace();
				ShowBuildBridgeWindow(start_tile, end_tile, 0x80);
			} else if (e->place.userdata != 4) {
				DoCommandP(end_tile, start_tile, _place_road_flag, CcPlaySound1D,
					_remove_button_clicked ?
					CMD_REMOVE_LONG_ROAD | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1805_CAN_T_REMOVE_ROAD_FROM) :
					CMD_BUILD_LONG_ROAD | CMD_AUTO | CMD_NO_WATER | CMD_MSG(STR_1804_CAN_T_BUILD_ROAD_HERE));
			} else {
				DoCommandP(end_tile, start_tile, _place_road_flag, CcPlaySound10, CMD_CLEAR_AREA | CMD_MSG(STR_00B5_CAN_T_CLEAR_THIS_AREA));
			}
		}
		break;

	case WE_PLACE_PRESIZE: {
		TileIndex tile = e->place.tile;

		DoCommandByTile(tile, 0x200, 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile==0?tile:_build_tunnel_endtile);
		break;
	}

	case WE_DESTROY:
		if (_patches.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0);
		break;
	}
}

static const Widget _build_road_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5, STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   227,     0,    13, STR_1802_ROAD_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,     7,   228,   239,     0,    13, 0x0,                   STR_STICKY_BUTTON},

{      WWT_PANEL,   RESIZE_NONE,     7,     0,    21,    14,    35, SPR_IMG_ROAD_NW,				STR_180B_BUILD_ROAD_SECTION},
{      WWT_PANEL,   RESIZE_NONE,     7,    22,    43,    14,    35, SPR_IMG_ROAD_NE,				STR_180B_BUILD_ROAD_SECTION},
{      WWT_PANEL,   RESIZE_NONE,     7,    44,    65,    14,    35, SPR_IMG_DYNAMITE,			STR_018D_DEMOLISH_BUILDINGS_ETC},
{      WWT_PANEL,   RESIZE_NONE,     7,    66,    87,    14,    35, SPR_IMG_ROAD_DEPOT,		STR_180C_BUILD_ROAD_VEHICLE_DEPOT},
{      WWT_PANEL,   RESIZE_NONE,     7,    88,   109,    14,    35, SPR_IMG_BUS_STATION,		STR_180D_BUILD_BUS_STATION},
{      WWT_PANEL,   RESIZE_NONE,     7,   110,   131,    14,    35, SPR_IMG_TRUCK_BAY,			STR_180E_BUILD_TRUCK_LOADING_BAY},
{      WWT_PANEL,   RESIZE_NONE,     7,   132,   173,    14,    35, SPR_IMG_BRIDGE,				STR_180F_BUILD_ROAD_BRIDGE},
{      WWT_PANEL,   RESIZE_NONE,     7,   174,   195,    14,    35, SPR_IMG_ROAD_TUNNEL,		STR_1810_BUILD_ROAD_TUNNEL},
{      WWT_PANEL,   RESIZE_NONE,     7,   196,   217,    14,    35, SPR_IMG_REMOVE, 				STR_1811_TOGGLE_BUILD_REMOVE_FOR},
{      WWT_PANEL,   RESIZE_NONE,     7,   218,   239,    14,    35, SPR_IMG_LANDSCAPING, STR_LANDSCAPING_TOOLBAR_TIP},
{   WIDGETS_END},
};

static const WindowDesc _build_road_desc = {
	640-240, 22, 240, 36,
	WC_BUILD_TOOLBAR,0,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON,
	_build_road_widgets,
	BuildRoadToolbWndProc
};

void ShowBuildRoadToolbar(void)
{
	if (_current_player == OWNER_SPECTATOR) return;
	DeleteWindowById(WC_BUILD_TOOLBAR, 0);
	AllocateWindowDesc(&_build_road_desc);
	if (_patches.link_terraform_toolbar) ShowTerraformToolbar();
}

static const Widget _build_road_scen_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,	STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   161,     0,    13, STR_1802_ROAD_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,     7,   162,   173,     0,    13, 0x0,                   STR_STICKY_BUTTON},

{     WWT_IMGBTN,   RESIZE_NONE,     7,     0,    21,    14,    35, 0x51D,			STR_180B_BUILD_ROAD_SECTION},
{     WWT_IMGBTN,   RESIZE_NONE,     7,    22,    43,    14,    35, 0x51E,			STR_180B_BUILD_ROAD_SECTION},
{     WWT_IMGBTN,   RESIZE_NONE,     7,    44,    65,    14,    35, 0x2BF,			STR_018D_DEMOLISH_BUILDINGS_ETC},
{      WWT_EMPTY,   RESIZE_NONE,     0,     0,     0,     0,     0, 0x0,				STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,     0,     0,     0,     0,     0, 0x0,				STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,     0,     0,     0,     0,     0, 0x0,				STR_NULL},
{     WWT_IMGBTN,   RESIZE_NONE,     7,    66,   107,    14,    35, 0xA22,			STR_180F_BUILD_ROAD_BRIDGE},
{     WWT_IMGBTN,   RESIZE_NONE,     7,   108,   129,    14,    35, 0x97D,			STR_1810_BUILD_ROAD_TUNNEL},
{     WWT_IMGBTN,   RESIZE_NONE,     7,   130,   151,    14,    35, 0x2CA,			STR_1811_TOGGLE_BUILD_REMOVE_FOR},
{      WWT_PANEL,   RESIZE_NONE,     7,   152,   173,    14,    35, SPR_IMG_LANDSCAPING, STR_LANDSCAPING_TOOLBAR_TIP},
{   WIDGETS_END},
};

static const WindowDesc _build_road_scen_desc = {
	-1, -1, 174, 36,
	WC_SCEN_BUILD_ROAD,0,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON,
	_build_road_scen_widgets,
	BuildRoadToolbWndProc
};

void ShowBuildRoadScenToolbar(void)
{
	AllocateWindowDescFront(&_build_road_scen_desc, 0);
}

static void BuildRoadDepotWndProc(Window* w, WindowEvent* e)
{
	switch (e->event) {
	case WE_PAINT:
		w->click_state = (1<<3) << _road_depot_orientation;
		DrawWindowWidgets(w);

		DrawRoadDepotSprite(70, 17, 0);
		DrawRoadDepotSprite(70, 69, 1);
		DrawRoadDepotSprite( 2, 69, 2);
		DrawRoadDepotSprite( 2, 17, 3);
		break;

	case WE_CLICK: {
		switch (e->click.widget) {
		case 3: case 4: case 5: case 6:
			_road_depot_orientation = e->click.widget - 3;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;
		}
	}	break;

	case WE_MOUSELOOP:
		if (WP(w,def_d).close) DeleteWindow(w);
		break;

	case WE_DESTROY:
		if (!WP(w,def_d).close) ResetObjectToPlace();
		break;
	}
}

static const Widget _build_road_depot_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   139,     0,    13, STR_1806_ROAD_DEPOT_ORIENTATION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   139,    14,   121, 0x0,			STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    17,    66, 0x0,			STR_1813_SELECT_ROAD_VEHICLE_DEPOT},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    69,   118, 0x0,			STR_1813_SELECT_ROAD_VEHICLE_DEPOT},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    69,   118, 0x0,			STR_1813_SELECT_ROAD_VEHICLE_DEPOT},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    17,    66, 0x0,			STR_1813_SELECT_ROAD_VEHICLE_DEPOT},
{   WIDGETS_END},
};

static const WindowDesc _build_road_depot_desc = {
	-1,-1, 140, 122,
	WC_BUILD_DEPOT,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_build_road_depot_widgets,
	BuildRoadDepotWndProc
};

static void ShowRoadDepotPicker(void)
{
	AllocateWindowDesc(&_build_road_depot_desc);
}

static void RoadStationPickerWndProc(Window *w, WindowEvent *e)
{
	switch(e->event) {
	case WE_PAINT: {
		int image;

		if (WP(w,def_d).close) return;

		w->click_state = ((1<<3) << _road_station_picker_orientation)	|
										 ((1<<7) << _station_show_coverage);
		DrawWindowWidgets(w);

		if (_station_show_coverage) {
			int rad = _patches.modified_catchment ? CA_TRUCK /* = CA_BUS */ : 4;
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else
			SetTileSelectSize(1, 1);

		image = (w->window_class == WC_BUS_STATION) ? 0x47 : 0x43;

		StationPickerDrawSprite(103, 35, 0, image);
		StationPickerDrawSprite(103, 85, 0, image+1);
		StationPickerDrawSprite(35, 85, 0, image+2);
		StationPickerDrawSprite(35, 35, 0, image+3);

		DrawStringCentered(70, 120, STR_3066_COVERAGE_AREA_HIGHLIGHT, 0);
		DrawStationCoverageAreaText(2, 146,
			((w->window_class == WC_BUS_STATION) ? (1<<CT_PASSENGERS) : ~(1<<CT_PASSENGERS)),
			3);

	} break;

	case WE_CLICK: {
		switch (e->click.widget) {
		case 3: case 4: case 5: case 6:
			_road_station_picker_orientation = e->click.widget - 3;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;
		case 7: case 8:
			_station_show_coverage = e->click.widget - 7;
			SndPlayFx(SND_15_BEEP);
			SetWindowDirty(w);
			break;
		}
	} break;

	case WE_MOUSELOOP: {
		if (WP(w,def_d).close) {
			DeleteWindow(w);
			return;
		}

		CheckRedrawStationCoverage(w);
	} break;

	case WE_DESTROY:
		if (!WP(w,def_d).close) ResetObjectToPlace();
		break;
	}
}

static const Widget _bus_station_picker_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,		STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   139,     0,    13, STR_3042_BUS_STATION_ORIENTATION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   139,    14,   176, 0x0,					STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    17,    66, 0x0,					STR_3051_SELECT_BUS_STATION_ORIENTATION},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    69,   118, 0x0,					STR_3051_SELECT_BUS_STATION_ORIENTATION},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    69,   118, 0x0,					STR_3051_SELECT_BUS_STATION_ORIENTATION},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    17,    66, 0x0,					STR_3051_SELECT_BUS_STATION_ORIENTATION},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    10,    69,   133,   144, STR_02DB_OFF,STR_3065_DON_T_HIGHLIGHT_COVERAGE},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    70,   129,   133,   144, STR_02DA_ON,	STR_3064_HIGHLIGHT_COVERAGE_AREA},
{   WIDGETS_END},
};

static const WindowDesc _bus_station_picker_desc = {
	-1,-1, 140, 177,
	WC_BUS_STATION,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_bus_station_picker_widgets,
	RoadStationPickerWndProc
};

static void ShowBusStationPicker(void)
{
	AllocateWindowDesc(&_bus_station_picker_desc);
}

static const Widget _truck_station_picker_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     7,     0,    10,     0,    13, STR_00C5,		STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     7,    11,   139,     0,    13, STR_3043_TRUCK_STATION_ORIENT, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     7,     0,   139,    14,   176, 0x0,					STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    17,    66, 0x0,					STR_3052_SELECT_TRUCK_LOADING_BAY},
{      WWT_PANEL,   RESIZE_NONE,    14,    71,   136,    69,   118, 0x0,					STR_3052_SELECT_TRUCK_LOADING_BAY},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    69,   118, 0x0,					STR_3052_SELECT_TRUCK_LOADING_BAY},
{      WWT_PANEL,   RESIZE_NONE,    14,     3,    68,    17,    66, 0x0,					STR_3052_SELECT_TRUCK_LOADING_BAY},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    10,    69,   133,   144, STR_02DB_OFF, STR_3065_DON_T_HIGHLIGHT_COVERAGE},
{    WWT_TEXTBTN,   RESIZE_NONE,    14,    70,   129,   133,   144, STR_02DA_ON,	STR_3064_HIGHLIGHT_COVERAGE_AREA},
{   WIDGETS_END},
};

static const WindowDesc _truck_station_picker_desc = {
	-1,-1, 140, 177,
	WC_TRUCK_STATION,WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_truck_station_picker_widgets,
	RoadStationPickerWndProc
};

static void ShowTruckStationPicker(void)
{
	AllocateWindowDesc(&_truck_station_picker_desc);
}

void InitializeRoadGui(void)
{
	_road_depot_orientation = 3;
	_road_station_picker_orientation = 3;
}
