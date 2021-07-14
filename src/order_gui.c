/* $Id: order_gui.c 3325 2005-12-21 01:14:01Z Darkvater $ */

#include "stdafx.h"
#include "openttd.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "functions.h"
#include "map.h"
#include "tile.h"
#include "window.h"
#include "gui.h"
#include "gfx.h"
#include "vehicle.h"
#include "station.h"
#include "town.h"
#include "command.h"
#include "viewport.h"
#include "depot.h"
#include "waypoint.h"
#include "m_airport.h"
#include "train.h"

static int OrderGetSel(const Window* w)
{
	const Vehicle* v = GetVehicle(w->window_number);
	int num = WP(w,order_d).sel;

	return (num >= 0 && num < v->num_orders) ? num : v->num_orders;
}

static StringID StationOrderStrings[] = {
	STR_8806_GO_TO,
	STR_8807_GO_TO_TRANSFER,
	STR_8808_GO_TO_UNLOAD,
	STR_8809_GO_TO_TRANSFER_UNLOAD,
	STR_880A_GO_TO_LOAD,
	STR_880B_GO_TO_TRANSFER_LOAD,
	STR_NULL,
	STR_NULL,
	STR_880C_GO_NON_STOP_TO,
	STR_880D_GO_TO_NON_STOP_TRANSFER,
	STR_880E_GO_NON_STOP_TO_UNLOAD,
	STR_880F_GO_TO_NON_STOP_TRANSFER_UNLOAD,
	STR_8810_GO_NON_STOP_TO_LOAD,
	STR_8811_GO_TO_NON_STOP_TRANSFER_LOAD,
	STR_NULL
};

static void DrawOrdersWindow(Window *w)
{
	const Vehicle *v;
	const Order *order;
	StringID str;
	int sel;
	int y, i;
	bool shared_orders;
	byte color;

	v = GetVehicle(w->window_number);

	w->disabled_state = (v->owner == _local_player) ? 0 : (
		1 << 4 |   //skip
		1 << 5 |   //delete
		1 << 6 |   //non-stop
		1 << 7 |   //go-to
		1 << 8 |   //full load
		1 << 9 |   //unload
		1 << 10    //transfer
		);

	if (v->type != VEH_Train)
		SETBIT(w->disabled_state, 6); //disable non-stop for non-trains

	shared_orders = IsOrderListShared(v);

	if ((uint)v->num_orders + (shared_orders?1:0) <= (uint)WP(w,order_d).sel)
		SETBIT(w->disabled_state, 5); /* delete */

	if (v->num_orders == 0)
		SETBIT(w->disabled_state, 4); /* skip */

	SetVScrollCount(w, v->num_orders + 1);

	sel = OrderGetSel(w);
	SetDParam(2, STR_8827_FULL_LOAD);

	order = GetVehicleOrder(v, sel);

	if (order != NULL) {
		switch (order->type) {
			case OT_GOTO_STATION:
				break;

			case OT_GOTO_DEPOT:
				SETBIT(w->disabled_state, 9);	/* unload */
				SETBIT(w->disabled_state, 10); /* transfer */
				SetDParam(2,STR_SERVICE);
				break;

			case OT_GOTO_WAYPOINT:
				SETBIT(w->disabled_state, 8); /* full load */
				SETBIT(w->disabled_state, 9); /* unload */
				SETBIT(w->disabled_state, 10); /* transfer */
				break;

			default:
				SETBIT(w->disabled_state, 6); /* nonstop */
				SETBIT(w->disabled_state, 8);	/* full load */
				SETBIT(w->disabled_state, 9);	/* unload */
		}
	} else {
		SETBIT(w->disabled_state, 6); /* nonstop */
		SETBIT(w->disabled_state, 8);	/* full load */
		SETBIT(w->disabled_state, 9);	/* unload */
		SETBIT(w->disabled_state, 10); /* transfer */
	}

	SetDParam(0, v->string_id);
	SetDParam(1, v->unitnumber);
	DrawWindowWidgets(w);

	y = 15;

	i = w->vscroll.pos;
	order = GetVehicleOrder(v, i);
	while (order != NULL) {
		str = (v->cur_order_index == i) ? STR_8805 : STR_8804;

		if (i - w->vscroll.pos < w->vscroll.cap) {
			SetDParam(1, 6);

			switch (order->type) {
				case OT_GOTO_STATION:
					SetDParam(1, StationOrderStrings[order->flags]);
					SetDParam(2, order->station);
					break;

				case OT_GOTO_DEPOT: {
					StringID s = STR_NULL;

					if (v->type == VEH_Aircraft) {
						s = STR_GO_TO_AIRPORT_HANGAR;
						SetDParam(2, order->station);
					} else {
						SetDParam(2, GetDepot(order->station)->town_index);

						switch (v->type) {
							case VEH_Train: s = (order->flags & OF_NON_STOP) ? STR_880F_GO_NON_STOP_TO_TRAIN_DEPOT : STR_GO_TO_TRAIN_DEPOT; break;
							case VEH_Road:  s = STR_9038_GO_TO_ROADVEH_DEPOT; break;
							case VEH_Ship:  s = STR_GO_TO_SHIP_DEPOT; break;
							default: break;
						}
					}

					if (order->flags & OF_FULL_LOAD) s++; /* service at */

					SetDParam(1, s);
					break;
				}

				case OT_GOTO_WAYPOINT:
					SetDParam(1, (order->flags & OF_NON_STOP) ? STR_GO_NON_STOP_TO_WAYPOINT : STR_GO_TO_WAYPOINT);
					SetDParam(2, order->station);
					break;
			}

			color = (i == WP(w,order_d).sel) ? 0xC : 0x10;
			SetDParam(0, i + 1);
			if (order->type != OT_DUMMY) {
				DrawString(2, y, str, color);
			} else {
				SetDParam(1, STR_INVALID_ORDER);
				SetDParam(2, order->station);
				DrawString(2, y, str, color);
			}
			y += 10;
		}

		i++;
		order = order->next;
	}

	if (i - w->vscroll.pos < w->vscroll.cap) {
		str = shared_orders ? STR_END_OF_SHARED_ORDERS : STR_882A_END_OF_ORDERS;
		color = (i == WP(w,order_d).sel) ? 0xC : 0x10;
		DrawString(2, y, str, color);
	}
}

static Order GetOrderCmdFromTile(const Vehicle *v, TileIndex tile)
{
	Order order;
	int st_index;

	// check depot first
	if (_patches.gotodepot) {
		switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (v->type == VEH_Train && IsTileOwner(tile, _local_player)) {
				if ((_m[tile].m5&0xFC)==0xC0) {
					order.type = OT_GOTO_DEPOT;
					order.flags = OF_PART_OF_ORDERS;
					order.station = GetDepotByTile(tile)->index;
					return order;
				}
			}
			break;

		case MP_STREET:
			if ((_m[tile].m5 & 0xF0) == 0x20 && v->type == VEH_Road && IsTileOwner(tile, _local_player)) {
				order.type = OT_GOTO_DEPOT;
				order.flags = OF_PART_OF_ORDERS;
				order.station = GetDepotByTile(tile)->index;
				return order;
			}
			break;

		case MP_STATION:
			if (v->type != VEH_Aircraft) break;
			if (IsAircraftHangarTile(tile) && IsTileOwner(tile, _local_player)) {
				order.type = OT_GOTO_DEPOT;
				order.flags = OF_PART_OF_ORDERS;
				order.station = _m[tile].m2;
				return order;
			}
			break;

		case MP_WATER:
			if (v->type != VEH_Ship) break;
			if (IsTileDepotType(tile, TRANSPORT_WATER) &&
					IsTileOwner(tile, _local_player)) {
				switch (_m[tile].m5) {
					case 0x81: tile -= TileDiffXY(1, 0); break;
					case 0x83: tile -= TileDiffXY(0, 1); break;
				}
				order.type = OT_GOTO_DEPOT;
				order.flags = OF_PART_OF_ORDERS;
				order.station = GetDepotByTile(tile)->index;
				return order;
			}

			default:
				break;
		}
	}

	// check waypoint
	if (IsTileType(tile, MP_RAILWAY) &&
			v->type == VEH_Train &&
			IsTileOwner(tile, _local_player) &&
			IsRailWaypoint(tile)) {
		order.type = OT_GOTO_WAYPOINT;
		order.flags = 0;
		order.station = GetWaypointByTile(tile)->index;
		return order;
	}

	if (IsTileType(tile, MP_STATION)) {
		const Station* st = GetStation(st_index = _m[tile].m2);
		
		if (st->owner == _current_player || st->owner == OWNER_NONE || MA_OwnerHandler(st->owner)) {
			byte facil;
			(facil=FACIL_DOCK, v->type == VEH_Ship) ||
			(facil=FACIL_TRAIN, v->type == VEH_Train) ||
			(facil=FACIL_AIRPORT, v->type == VEH_Aircraft) ||
			(facil=FACIL_BUS_STOP, v->type == VEH_Road && v->cargo_type == CT_PASSENGERS) ||
			(facil=FACIL_TRUCK_STOP, 1);
			if (st->facilities & facil) {
				order.type = OT_GOTO_STATION;
				order.flags = 0;
				order.station = st_index;
				return order;
			}
		}
	}

	// not found
	order.type = OT_NOTHING;
	order.flags = 0;
	return order;
}

static bool HandleOrderVehClick(const Vehicle* v, const Vehicle* u, Window* w)
{
	if (u->type != v->type) return false;

	if (u->type == VEH_Train && !IsFrontEngine(u)) {
		u = GetFirstVehicleInChain(u);
		if (!IsFrontEngine(u)) return false;
	}

	// v is vehicle getting orders. Only copy/clone orders if vehicle doesn't have any orders yet
	// obviously if you press CTRL on a non-empty orders vehicle you know what you are doing
	if (v->num_orders != 0 && _ctrl_pressed == 0) return false;

	if (DoCommandP(v->tile, v->index | (u->index << 16), _ctrl_pressed ? 0 : 1, NULL,
		_ctrl_pressed ? CMD_CLONE_ORDER | CMD_MSG(STR_CANT_SHARE_ORDER_LIST) : CMD_CLONE_ORDER | CMD_MSG(STR_CANT_COPY_ORDER_LIST))) {
		WP(w,order_d).sel = -1;
		ResetObjectToPlace();
	}

	return true;
}

static void OrdersPlaceObj(const Vehicle* v, TileIndex tile, Window* w)
{
	Order cmd;
	const Vehicle* u;

	// check if we're clicking on a vehicle first.. clone orders in that case.
	u = CheckMouseOverVehicle();
	if (u != NULL && HandleOrderVehClick(v, u, w)) return;

	cmd = GetOrderCmdFromTile(v, tile);
	if (cmd.type == OT_NOTHING) return;

	if (DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), PackOrder(&cmd), NULL, CMD_INSERT_ORDER | CMD_MSG(STR_8833_CAN_T_INSERT_NEW_ORDER))) {
		if (WP(w,order_d).sel != -1) WP(w,order_d).sel++;
		ResetObjectToPlace();
	}
}

static void OrderClick_Goto(Window* w, const Vehicle* v)
{
	InvalidateWidget(w, 7);
	TOGGLEBIT(w->click_state, 7);
	if (HASBIT(w->click_state, 7)) {
		_place_clicked_vehicle = NULL;
		SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, 1, w);
	} else {
		ResetObjectToPlace();
	}
}

static void OrderClick_FullLoad(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), OFB_FULL_LOAD, NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

static void OrderClick_Unload(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), OFB_UNLOAD,    NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

static void OrderClick_Nonstop(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), OFB_NON_STOP,  NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

static void OrderClick_Transfer(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index + (OrderGetSel(w) <<  16), OFB_TRANSFER, NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

static void OrderClick_Skip(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index, 0, NULL, CMD_SKIP_ORDER);
}

static void OrderClick_Delete(Window* w, const Vehicle* v)
{
	DoCommandP(v->tile, v->index, OrderGetSel(w), NULL, CMD_DELETE_ORDER | CMD_MSG(STR_8834_CAN_T_DELETE_THIS_ORDER));
}

typedef void OnButtonClick(Window* w, const Vehicle* v);

static OnButtonClick* const _order_button_proc[] = {
	OrderClick_Skip,
	OrderClick_Delete,
	OrderClick_Nonstop,
	OrderClick_Goto,
	OrderClick_FullLoad,
	OrderClick_Unload,
	OrderClick_Transfer
};

static const uint16 _order_keycodes[] = {
	'D', //skip order
	'F', //delete order
	'G', //non-stop
	'H', //goto order
	'J', //full load
	'K'  //unload
};

static void OrdersWndProc(Window *w, WindowEvent *e)
{
	switch (e->event) {
	case WE_PAINT:
		DrawOrdersWindow(w);
		break;

	case WE_CLICK: {
		Vehicle *v = GetVehicle(w->window_number);
		switch(e->click.widget) {
		case 2: { /* orders list */
			int sel;
			sel = (e->click.pt.y - 15) / 10;

			if ((uint)sel >= w->vscroll.cap)
				return;

			sel += w->vscroll.pos;

			if (_ctrl_pressed && sel < v->num_orders) {
				const Order *ord = GetVehicleOrder(v, sel);
				int xy = 0;
				switch (ord->type) {
				case OT_GOTO_STATION:			/* station order */
					xy = GetStation(ord->station)->xy ;
					break;
				case OT_GOTO_DEPOT:				/* goto depot order */
					xy = GetDepot(ord->station)->xy;
					break;
				case OT_GOTO_WAYPOINT:	/* goto waypoint order */
					xy = GetWaypoint(ord->station)->xy;
				}

				if (xy)
					ScrollMainWindowToTile(xy);

				return;
			}

			if (sel == WP(w,order_d).sel) sel = -1;
			WP(w,order_d).sel = sel;
			SetWindowDirty(w);
		}	break;

		case 4: /* skip button */
			OrderClick_Skip(w, v);
			break;

		case 5: /* delete button */
			OrderClick_Delete(w, v);
			break;

		case 6: /* non stop button */
			OrderClick_Nonstop(w, v);
			break;

		case 7: /* goto button */
			OrderClick_Goto(w, v);
			break;

		case 8: /* full load button */
			OrderClick_FullLoad(w, v);
			break;

		case 9: /* unload button */
			OrderClick_Unload(w, v);
			break;
		case 10: /* transfer button */
			OrderClick_Transfer(w, v);
			break;
		}
	} break;

	case WE_KEYPRESS: {
		Vehicle *v = GetVehicle(w->window_number);
		uint i;

		for(i = 0; i < lengthof(_order_keycodes); i++) {
			if (e->keypress.keycode == _order_keycodes[i]) {
				e->keypress.cont = false;
				//see if the button is disabled
				if (!(HASBIT(w->disabled_state, (i + 4)))) {
					_order_button_proc[i](w, v);
				}
				break;
			}
		}
		break;
	}

	case WE_RCLICK: {
		const Vehicle* v = GetVehicle(w->window_number);
		int s = OrderGetSel(w);

		if (e->click.widget != 8) break;
		if (s == v->num_orders || GetVehicleOrder(v, s)->type != OT_GOTO_DEPOT) {
			GuiShowTooltips(STR_8857_MAKE_THE_HIGHLIGHTED_ORDER);
		} else {
			GuiShowTooltips(STR_SERVICE_HINT);
		}
	} break;

	case WE_4: {
		if (FindWindowById(WC_VEHICLE_VIEW, w->window_number) == NULL)
			DeleteWindow(w);
	} break;

	case WE_PLACE_OBJ: {
		OrdersPlaceObj(GetVehicle(w->window_number), e->place.tile, w);
	} break;

	case WE_ABORT_PLACE_OBJ: {
		CLRBIT(w->click_state, 7);
		InvalidateWidget(w, 7);
	} break;

	// check if a vehicle in a depot was clicked..
	case WE_MOUSELOOP: {
		const Vehicle* v = _place_clicked_vehicle;
		/*
		 * Check if we clicked on a vehicle
		 * and if the GOTO button of this window is pressed
		 * This is because of all open order windows WE_MOUSELOOP is called
		 * and if you have 3 windows open, and this check is not done
		 * the order is copied to the last open window instead of the
		 * one where GOTO is enalbed
		 */
		if (v != NULL && HASBIT(w->click_state, 7)) {
			_place_clicked_vehicle = NULL;
			HandleOrderVehClick(GetVehicle(w->window_number), v, w);
		}
	} break;

	case WE_RESIZE:
		/* Update the scroll + matrix */
		w->vscroll.cap = (w->widget[2].bottom - w->widget[2].top) / 10;
		break;
	}

}

static const Widget _orders_train_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,									STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   384,     0,    13, STR_8829_ORDERS,					STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_RB,      14,     0,   372,    14,    75, 0x0,											STR_8852_ORDERS_LIST_CLICK_ON_ORDER},
{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   373,   384,    14,    75, 0x0,											STR_0190_SCROLL_BAR_SCROLLS_LIST},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,     0,    52,    76,    87, STR_8823_SKIP,						STR_8853_SKIP_THE_CURRENT_ORDER},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,    53,   105,    76,    87, STR_8824_DELETE,					STR_8854_DELETE_THE_HIGHLIGHTED},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   106,   158,    76,    87, STR_8825_NON_STOP,				STR_8855_MAKE_THE_HIGHLIGHTED_ORDER},
{WWT_NODISTXTBTN,   RESIZE_TB,      14,   159,   211,    76,    87, STR_8826_GO_TO,						STR_8856_INSERT_A_NEW_ORDER_BEFORE},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   212,   264,    76,    87, STR_FULLLOAD_OR_SERVICE,	STR_NULL},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   265,   319,    76,    87, STR_8828_UNLOAD,					STR_8858_MAKE_THE_HIGHLIGHTED_ORDER},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   320,   372,    76,    87, STR_886F_TRANSFER, STR_886D_MAKE_THE_HIGHLIGHTED_ORDER},
{      WWT_PANEL,   RESIZE_RTB,     14,   373,   372,    76,    87, 0x0,											STR_NULL},
{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   373,   384,    76,    87, 0x0,											STR_RESIZE_BUTTON},
{   WIDGETS_END},
};

static const WindowDesc _orders_train_desc = {
	-1,-1, 385, 88,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_orders_train_widgets,
	OrdersWndProc
};

static const Widget _orders_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,									STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   395,     0,    13, STR_8829_ORDERS,					STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_RB,      14,     0,   383,    14,    75, 0x0,											STR_8852_ORDERS_LIST_CLICK_ON_ORDER},
{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   384,   395,    14,    75, 0x0,											STR_0190_SCROLL_BAR_SCROLLS_LIST},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,     0,    63,    76,    87, STR_8823_SKIP,						STR_8853_SKIP_THE_CURRENT_ORDER},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,    64,   128,    76,    87, STR_8824_DELETE,					STR_8854_DELETE_THE_HIGHLIGHTED},
{      WWT_EMPTY,   RESIZE_TB,      14,     0,     0,    76,    87, 0x0,											0x0},
{WWT_NODISTXTBTN,   RESIZE_TB,      14,   129,   192,    76,    87, STR_8826_GO_TO,						STR_8856_INSERT_A_NEW_ORDER_BEFORE},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   193,   256,    76,    87, STR_FULLLOAD_OR_SERVICE,	STR_NULL},
{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   257,   319,    76,    87, STR_8828_UNLOAD,					STR_8858_MAKE_THE_HIGHLIGHTED_ORDER},
{ WWT_PUSHTXTBTN,   RESIZE_TB,    14,   320,   383,    76,    87, STR_886F_TRANSFER, STR_886D_MAKE_THE_HIGHLIGHTED_ORDER},
{      WWT_PANEL,   RESIZE_RTB,     14,   384,   383,    76,    87, 0x0,											STR_NULL},
{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   384,   395,    76,    87, 0x0,											STR_RESIZE_BUTTON},
{   WIDGETS_END},
};

static const WindowDesc _orders_desc = {
	-1,-1, 396, 88,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_orders_widgets,
	OrdersWndProc
};

static const Widget _other_orders_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,					STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   331,     0,    13, STR_A00B_ORDERS,	STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_RB,      14,     0,   319,    14,    75, 0x0,							STR_8852_ORDERS_LIST_CLICK_ON_ORDER},
{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   320,   331,    14,    75, 0x0,							STR_0190_SCROLL_BAR_SCROLLS_LIST},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_EMPTY,   RESIZE_NONE,    14,     0,   319,    76,    87, 0x0,                     STR_NULL},
{      WWT_PANEL,   RESIZE_RTB,     14,     0,   319,    76,    87, 0x0,							STR_NULL},
{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   320,   331,    76,    87, 0x0,							STR_RESIZE_BUTTON},
{   WIDGETS_END},
};

static const WindowDesc _other_orders_desc = {
	-1,-1, 332, 88,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_other_orders_widgets,
	OrdersWndProc
};

void ShowOrdersWindow(const Vehicle* v)
{
	Window *w;
	VehicleID veh = v->index;

	DeleteWindowById(WC_VEHICLE_ORDERS, veh);
	DeleteWindowById(WC_VEHICLE_DETAILS, veh);

	_alloc_wnd_parent_num = veh;

	if (v->owner != _local_player) {
		w = AllocateWindowDesc(&_other_orders_desc);
	} else {
		w = AllocateWindowDesc((v->type == VEH_Train) ? &_orders_train_desc : &_orders_desc);
	}

	if (w != NULL) {
		w->window_number = veh;
		w->caption_color = v->owner;
		w->vscroll.cap = 6;
		w->resize.step_height = 10;
		WP(w,order_d).sel = -1;
	}
}
