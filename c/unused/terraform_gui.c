/* $Id: terraform_gui.c 3338 2005-12-24 20:53:02Z tron $ */
#if 0
#include "stdafx.h"
#include "openttd.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "functions.h"
#include "player.h"
#include "tile.h"
#include "window.h"
#include "gui.h"
#include "viewport.h"
#include "gfx.h"
#include "sound.h"
#include "command.h"
#include "vehicle.h"
#include "signs.h"
#include "variables.h"

void CcTerraform(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_1F_SPLAT, tile);
	} else {
		SetRedErrorSquare(_terraform_err_tile);
	}
}

static void GenericRaiseLowerLand(TileIndex tile, int mode)
{
	if (mode) {
		DoCommandP(tile, 8, (uint32)mode, CcTerraform, CMD_TERRAFORM_LAND | CMD_AUTO | CMD_MSG(STR_0808_CAN_T_RAISE_LAND_HERE));
	} else {
		DoCommandP(tile, 8, (uint32)mode, CcTerraform, CMD_TERRAFORM_LAND | CMD_AUTO | CMD_MSG(STR_0809_CAN_T_LOWER_LAND_HERE));
	}
}

/** Scenario editor command that generates desert areas */
static void GenerateDesertArea(TileIndex end, TileIndex start)
{
	int size_x, size_y;
	int sx = TileX(start);
	int sy = TileY(start);
	int ex = TileX(end);
	int ey = TileY(end);

	if (_game_mode != GM_EDITOR) return;

	if (ex < sx) intswap(ex, sx);
	if (ey < sy) intswap(ey, sy);
	size_x = (ex - sx) + 1;
	size_y = (ey - sy) + 1;

	_generating_world = true;
	BEGIN_TILE_LOOP(tile, size_x, size_y, TileXY(sx, sy)) {
		if (GetTileType(tile) != MP_WATER) {
			SetMapExtraBits(tile, (_ctrl_pressed) ? 0 : 1);
			DoCommandP(tile, 0, 0, NULL, CMD_LANDSCAPE_CLEAR);
			MarkTileDirtyByTile(tile);
		}
	} END_TILE_LOOP(tile, size_x, size_y, 0);
	_generating_world = false;
}

/** Scenario editor command that generates desert areas */
static void GenerateRockyArea(TileIndex end, TileIndex start)
{
	int size_x, size_y;
	bool success = false;
	int sx = TileX(start);
	int sy = TileY(start);
	int ex = TileX(end);
	int ey = TileY(end);

	if (_game_mode != GM_EDITOR) return;

	if (ex < sx) intswap(ex, sx);
	if (ey < sy) intswap(ey, sy);
	size_x = (ex - sx) + 1;
	size_y = (ey - sy) + 1;

	BEGIN_TILE_LOOP(tile, size_x, size_y, TileXY(sx, sy)) {
		if (IsTileType(tile, MP_CLEAR) || IsTileType(tile, MP_TREES)) {
			ModifyTile(tile, MP_SETTYPE(MP_CLEAR) | MP_MAP5, (_m[tile].m5 & ~0x1C) | 0xB);
			success = true;
		}
	} END_TILE_LOOP(tile, size_x, size_y, 0);

	if (success) SndPlayTileFx(SND_1F_SPLAT, end);
}

/**
 * A central place to handle all X_AND_Y dragged GUI functions.
 * @param we @WindowEvent variable holding in its higher bits (excluding the lower
 * 4, since that defined the X_Y drag) the type of action to be performed
 * @return Returns true if the action was found and handled, and false otherwise. This
 * allows for additional implements that are more local. For example X_Y drag
 * of convertrail which belongs in rail_gui.c and not terraform_gui.c
 **/
bool GUIPlaceProcDragXY(const WindowEvent *we)
{
	TileIndex start_tile = we->place.starttile;
	TileIndex end_tile = we->place.tile;

	switch (we->place.userdata >> 4) {
	case GUI_PlaceProc_DemolishArea >> 4:
		DoCommandP(end_tile, start_tile, 0, CcPlaySound10, CMD_CLEAR_AREA | CMD_MSG(STR_00B5_CAN_T_CLEAR_THIS_AREA));
		break;
	case GUI_PlaceProc_LevelArea >> 4:
		DoCommandP(end_tile, start_tile, 0, CcPlaySound10, CMD_LEVEL_LAND | CMD_AUTO);
		break;
	case GUI_PlaceProc_RockyArea >> 4:
		GenerateRockyArea(end_tile, start_tile);
		break;
	case GUI_PlaceProc_DesertArea >> 4:
		GenerateDesertArea(end_tile, start_tile);
		break;
	case GUI_PlaceProc_WaterArea >> 4:
		DoCommandP(end_tile, start_tile, 0, CcBuildCanal, CMD_BUILD_CANAL | CMD_AUTO | CMD_MSG(STR_CANT_BUILD_CANALS));
		break;
	default: return false;
	}

	return true;
}

typedef void OnButtonClick(Window *w);

static const uint16 _terraform_keycodes[] = {
	'Q',
	'W',
	'E',
	'D',
	'U',
	'I',
	'O',
};

void PlaceProc_DemolishArea(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_AND_Y | GUI_PlaceProc_DemolishArea);
}

void PlaceProc_RaiseLand(TileIndex tile)
{
	GenericRaiseLowerLand(tile, 1);
}

void PlaceProc_LowerLand(TileIndex tile)
{
	GenericRaiseLowerLand(tile, 0);
}

void PlaceProc_LevelLand(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_AND_Y | GUI_PlaceProc_LevelArea);
}

static void PlaceProc_PlantTree(TileIndex tile) {}

static void TerraformClick_Lower(Window *w)
{
	HandlePlacePushButton(w, 4, ANIMCURSOR_LOWERLAND, 2, PlaceProc_LowerLand);
}

static void TerraformClick_Raise(Window *w)
{
	HandlePlacePushButton(w, 5, ANIMCURSOR_RAISELAND, 2, PlaceProc_RaiseLand);
}

static void TerraformClick_Level(Window *w)
{
	HandlePlacePushButton(w, 6, SPR_CURSOR_LEVEL_LAND, 2, PlaceProc_LevelLand);
}

static void TerraformClick_Dynamite(Window *w)
{
	HandlePlacePushButton(w, 7, ANIMCURSOR_DEMOLISH , 1, PlaceProc_DemolishArea);
}

static void TerraformClick_BuyLand(Window *w)
{
	HandlePlacePushButton(w, 8, SPR_CURSOR_BUY_LAND, 1, PlaceProc_BuyLand);
}

static void TerraformClick_Trees(Window *w)
{
	if (HandlePlacePushButton(w, 9, SPR_CURSOR_MOUSE, 1, PlaceProc_PlantTree)) ShowBuildTreesToolbar();
}

static void TerraformClick_PlaceSign(Window *w)
{
	HandlePlacePushButton(w, 10, SPR_CURSOR_SIGN, 1, PlaceProc_Sign);
}

static OnButtonClick * const _terraform_button_proc[] = {
	TerraformClick_Lower,
	TerraformClick_Raise,
	TerraformClick_Level,
	TerraformClick_Dynamite,
	TerraformClick_BuyLand,
	TerraformClick_Trees,
	TerraformClick_PlaceSign,
};

static void TerraformToolbWndProc(Window *w, WindowEvent *e)
{
	switch(e->event) {
	case WE_PAINT:
		DrawWindowWidgets(w);
		break;

	case WE_CLICK:
		if (e->click.widget >= 4) _terraform_button_proc[e->click.widget - 4](w);
		break;

	case WE_KEYPRESS: {
		uint i;

		for (i = 0; i != lengthof(_terraform_keycodes); i++) {
			if (e->keypress.keycode == _terraform_keycodes[i]) {
				e->keypress.cont = false;
				_terraform_button_proc[i](w);
				break;
			}
		}
		break;
	}

	case WE_PLACE_OBJ:
		_place_proc(e->place.tile);
		return;

	case WE_PLACE_DRAG:
		VpSelectTilesWithMethod(e->place.pt.x, e->place.pt.y, e->place.userdata & 0xF);
		break;

	case WE_PLACE_MOUSEUP:
		if (e->click.pt.x != -1) {
			if ((e->place.userdata & 0xF) == VPM_X_AND_Y) // dragged actions
				GUIPlaceProcDragXY(e);
		}
		break;

	case WE_ABORT_PLACE_OBJ:
		UnclickWindowButtons(w);
		SetWindowDirty(w);

		w = FindWindowById(WC_BUILD_STATION, 0);
		if (w != NULL) WP(w,def_d).close=true;
		w = FindWindowById(WC_BUILD_DEPOT, 0);
		if (w != NULL) WP(w,def_d).close=true;
		break;

	case WE_PLACE_PRESIZE: {
	} break;
	}
}

static const Widget _terraform_widgets[] = {
{ WWT_CLOSEBOX,   RESIZE_NONE,     7,   0,  10,   0,  13, STR_00C5,                STR_018B_CLOSE_WINDOW},
{  WWT_CAPTION,   RESIZE_NONE,     7,  11, 145,   0,  13, STR_LANDSCAPING_TOOLBAR, STR_018C_WINDOW_TITLE_DRAG_THIS},
{WWT_STICKYBOX,   RESIZE_NONE,     7, 146, 157,   0,  13, STR_NULL,                STR_STICKY_BUTTON},

{    WWT_PANEL,   RESIZE_NONE,     7,  66,  69,  14,  35, STR_NULL,                STR_NULL},
{    WWT_PANEL,   RESIZE_NONE,     7,   0,  21,  14,  35, SPR_IMG_TERRAFORM_DOWN,  STR_018E_LOWER_A_CORNER_OF_LAND},
{    WWT_PANEL,   RESIZE_NONE,     7,  22,  43,  14,  35, SPR_IMG_TERRAFORM_UP,    STR_018F_RAISE_A_CORNER_OF_LAND},
{    WWT_PANEL,   RESIZE_NONE,     7,  44,  65,  14,  35, SPR_IMG_LEVEL_LAND,      STR_LEVEL_LAND_TOOLTIP},
{    WWT_PANEL,   RESIZE_NONE,     7,  70,  91,  14,  35, SPR_IMG_DYNAMITE,        STR_018D_DEMOLISH_BUILDINGS_ETC},
{    WWT_PANEL,   RESIZE_NONE,     7,  92, 113,  14,  35, SPR_IMG_BUY_LAND,        STR_0329_PURCHASE_LAND_FOR_FUTURE},
{    WWT_PANEL,   RESIZE_NONE,     7, 114, 135,  14,  35, SPR_IMG_PLANTTREES,      STR_0185_PLANT_TREES_PLACE_SIGNS},
{    WWT_PANEL,   RESIZE_NONE,     7, 136, 157,  14,  35, SPR_IMG_PLACE_SIGN,      STR_0289_PLACE_SIGN},

{   WIDGETS_END},
};

static const WindowDesc _terraform_desc = {
	640-157, 22+36, 158, 36,
	WC_SCEN_LAND_GEN,0,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON,
	_terraform_widgets,
	TerraformToolbWndProc
};

void ShowTerraformToolbar(void)
{
	if (_current_player == OWNER_SPECTATOR) return;
	AllocateWindowDescFront(&_terraform_desc, 0);
}
