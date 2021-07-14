/* $Id: players.c 3343 2005-12-26 15:41:24Z peter1138 $ */

/** @file players.c
  * @todo Cleanup the messy DrawPlayerFace function asap
  */
#include "stdafx.h"
#include "openttd.h"
#include "engine.h"
#include "functions.h"
#include "string.h"
#include "strings.h"
#include "table/strings.h"
#include "table/sprites.h"
#include "map.h"
#include "player.h"
#include "town.h"
#include "vehicle.h"
#include "station.h"
#include "gfx.h"
#include "news.h"
#include "saveload.h"
#include "command.h"
#include "sound.h"
#include "network.h"
#include "variables.h"
#include "ai/ai.h"

static const SpriteID cheeks_table[4] = {
	0x325, 0x326,
	0x390, 0x3B0,
};

static const SpriteID mouth_table[3] = {
	0x34C, 0x34D, 0x34F
};

void DrawPlayerFace(uint32 face, int color, int x, int y)
{
	byte flag = 0;

	if ( (int32)face < 0)
		flag |= 1;
	if ((((((face >> 7) ^ face) >> 7) ^ face) & 0x8080000) == 0x8000000)
		flag |= 2;

	/* draw the gradient */
	DrawSprite((color + 0x307) << PALETTE_SPRITE_START | PALETTE_MODIFIER_COLOR | SPR_GRADIENT, x, y);

	/* draw the cheeks */
	DrawSprite(cheeks_table[flag&3], x, y);

	/* draw the chin */
	/* FIXME: real code uses -2 in zoomlevel 1 */
	{
		uint val = GB(face, 4, 2);
		if (!(flag & 2)) {
			DrawSprite(0x327 + (flag&1?0:val), x, y);
		} else {
			DrawSprite((flag&1?0x3B1:0x391) + (val>>1), x, y);
		}
	}
	/* draw the eyes */
	{
		uint val1 = GB(face,  6, 4);
		uint val2 = GB(face, 20, 3);
		uint32 high = 0x314 << PALETTE_SPRITE_START;

		if (val2 >= 6) {
			high = 0x30F << PALETTE_SPRITE_START;
			if (val2 != 6)
				high = 0x30D << PALETTE_SPRITE_START;
		}

		if (!(flag & 2)) {
			if (!(flag & 1)) {
				DrawSprite(high+((val1 * 12 >> 4) + (0x32B | PALETTE_MODIFIER_COLOR)), x, y);
			} else {
				DrawSprite(high+(val1 + (0x337 | PALETTE_MODIFIER_COLOR)), x, y);
			}
		} else {
			if (!(flag & 1)) {
				DrawSprite(high+((val1 * 11 >> 4) + (0x39A | PALETTE_MODIFIER_COLOR)), x, y);
			} else {
				DrawSprite(high+(val1 + (0x3B8 | PALETTE_MODIFIER_COLOR)), x, y);
			}
		}
	}

	/* draw the mouth */
	{
		uint val = GB(face, 10, 6);
		uint val2;

		if (!(flag&1)) {
			val2 = ((val&0xF) * 15 >> 4);

			if (val2 < 3) {
				DrawSprite((flag&2 ? 0x397 : 0x367) + val2, x, y);
				/* skip the rest */
				goto skip_mouth;
			}

			val2 -= 3;
			if (flag & 2) {
				if (val2 > 8) val2 = 0;
				val2 += 0x3A5 - 0x35B;
			}
			DrawSprite(val2 + 0x35B, x, y);
		} else if (!(flag&2)) {
			DrawSprite(((val&0xF) * 10 >> 4) + 0x351, x, y);
		} else {
			DrawSprite(((val&0xF) * 9 >> 4) + 0x3C8, x, y);
		}

		val >>= 3;

		if (!(flag&2)) {
			if (!(flag&1)) {
				DrawSprite(0x349 + val, x, y);
			} else {
				DrawSprite( mouth_table[(val*3>>3)], x, y);
			}
		} else {
			if (!(flag&1)) {
				DrawSprite(0x393 + (val&3), x, y);
			} else {
				DrawSprite(0x3B3 + (val*5>>3), x, y);
			}
		}

		skip_mouth:;
	}


	/* draw the hair */
	{
		uint val = GB(face, 16, 4);
		if (flag & 2) {
			if (flag & 1) {
				DrawSprite(0x3D9 + (val * 5 >> 4), x, y);
			} else {
				DrawSprite(0x3D4 + (val * 5 >> 4), x, y);
			}
		} else {
			if (flag & 1) {
				DrawSprite(0x38B + (val * 5 >> 4), x, y);
			} else {
				DrawSprite(0x382 + (val * 9 >> 4), x, y);
			}
		}
	}

	/* draw the tie */
	{
		uint val = GB(face, 20, 8);

		if (!(flag&1)) {
			DrawSprite(0x36B + (GB(val, 0, 2) * 3 >> 2), x, y);
			DrawSprite(0x36E + (GB(val, 2, 2) * 4 >> 2), x, y);
			DrawSprite(0x372 + (GB(val, 4, 4) * 6 >> 4), x, y);
		} else {
			DrawSprite(0x378 + (GB(val, 0, 2) * 3 >> 2), x, y);
			DrawSprite(0x37B + (GB(val, 2, 2) * 4 >> 2), x, y);

			val >>= 4;
			if (val < 3) DrawSprite((flag & 2 ? 0x3D1 : 0x37F) + val, x, y);
		}
	}

	/* draw the glasses */
	{
		uint val = GB(face, 28, 3);

		if (flag & 2) {
			if (val <= 1) DrawSprite(0x3AE + val, x, y);
		} else {
			if (val <= 1) DrawSprite(0x347 + val, x, y);
		}
	}
}

void InvalidatePlayerWindows(const Player *p)
{
	PlayerID pid = p->index;

	if (pid == _local_player) InvalidateWindow(WC_STATUS_BAR, 0);
	InvalidateWindow(WC_FINANCES, pid);
}

bool CheckPlayerHasMoney(int32 cost)
{
	if (cost > 0) {
		PlayerID pid = _current_player;
		if (pid < MAX_PLAYERS && cost > GetPlayer(pid)->player_money) {
			SetDParam(0, cost);
			_error_message = STR_0003_NOT_ENOUGH_CASH_REQUIRES;
			return false;
		}
	}
	return true;
}

static void SubtractMoneyFromAnyPlayer(Player *p, int32 cost)
{
	p->money64 -= cost;
	UpdatePlayerMoney32(p);

	p->yearly_expenses[0][_yearly_expenses_type] += cost;

	if ( ( 1 << _yearly_expenses_type ) & (1<<7|1<<8|1<<9|1<<10))
		p->cur_economy.income -= cost;
	else if (( 1 << _yearly_expenses_type ) & (1<<2|1<<3|1<<4|1<<5|1<<6|1<<11))
		p->cur_economy.expenses -= cost;

	InvalidatePlayerWindows(p);
}

void SubtractMoneyFromPlayer(int32 cost)
{
	PlayerID pid = _current_player;
	if (pid < MAX_PLAYERS)
		SubtractMoneyFromAnyPlayer(GetPlayer(pid), cost);
}

void SubtractMoneyFromPlayerFract(PlayerID player, int32 cost)
{
	Player *p = GetPlayer(player);
	byte m = p->player_money_fraction;
	p->player_money_fraction = m - (byte)cost;
	cost >>= 8;
	if (p->player_money_fraction > m)
		cost++;
	if (cost != 0)
		SubtractMoneyFromAnyPlayer(p, cost);
}

// the player_money field is kept as it is, but money64 contains the actual amount of money.
void UpdatePlayerMoney32(Player *p)
{
	if (p->money64 < -2000000000)
		p->player_money = -2000000000;
	else if (p->money64 > 2000000000)
		p->player_money = 2000000000;
	else
		p->player_money = (int32)p->money64;
}

void GetNameOfOwner(PlayerID owner, TileIndex tile)
{
	SetDParam(2, owner);

	if (owner != OWNER_TOWN) {
		if (owner >= 8)
			SetDParam(0, STR_0150_SOMEONE);
		else {
			Player *p = GetPlayer(owner);
			SetDParam(0, p->name_1);
			SetDParam(1, p->name_2);
		}
	} else {
		Town *t = ClosestTownFromTile(tile, (uint)-1);
		SetDParam(0, STR_TOWN);
		SetDParam(1, t->index);
	}
}


bool CheckOwnership(PlayerID owner)
{
	assert(owner <= OWNER_WATER);

	if (owner == _current_player)
		return true;
	_error_message = STR_013B_OWNED_BY;
	GetNameOfOwner(owner, 0);
	return false;
}

bool CheckTileOwnership(TileIndex tile)
{
	PlayerID owner = GetTileOwner(tile);

	assert(owner <= OWNER_WATER);

	if (owner == _current_player)
		return true;
	_error_message = STR_013B_OWNED_BY;

	// no need to get the name of the owner unless we're the local player (saves some time)
	if (IsLocalPlayer()) GetNameOfOwner(owner, tile);
	return false;
}

static void GenerateCompanyName(Player *p)
{
	TileIndex tile;
	Town *t;
	StringID str;
	Player *pp;
	uint32 strp;
	char buffer[100];

	if (p->name_1 != STR_SV_UNNAMED)
		return;

	tile = p->last_build_coordinate;
	if (tile == 0)
		return;

	t = ClosestTownFromTile(tile, (uint)-1);

	if (IS_INT_INSIDE(t->townnametype, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_LAST+1)) {
		str = t->townnametype - SPECSTR_TOWNNAME_START + SPECSTR_PLAYERNAME_START;
		strp = t->townnameparts;

verify_name:;
		// No player must have this name already
		FOR_ALL_PLAYERS(pp) {
			if (pp->name_1 == str && pp->name_2 == strp)
				goto bad_town_name;
		}

		GetString(buffer, str);
		if (strlen(buffer) >= 32 || GetStringWidth(buffer) >= 150)
			goto bad_town_name;

set_name:;
		p->name_1 = str;
		p->name_2 = strp;

		MarkWholeScreenDirty();

		if (!IS_HUMAN_PLAYER(p->index)) {
			SetDParam(0, t->index);
			AddNewsItem(p->index + (4 << 4), NEWS_FLAGS(NM_CALLBACK, NF_TILE, NT_COMPANY_INFO, DNC_BANKRUPCY), p->last_build_coordinate, 0);
		}
		return;
	}
bad_town_name:;

	if (p->president_name_1 == SPECSTR_PRESIDENT_NAME) {
		str = SPECSTR_ANDCO_NAME;
		strp = p->president_name_2;
		goto set_name;
	} else {
		str = SPECSTR_ANDCO_NAME;
		strp = Random();
		goto verify_name;
	}
}

#define COLOR_SWAP(i,j) do { byte t=colors[i];colors[i]=colors[j];colors[j]=t; } while(0)

static const byte _color_sort[16] = {2, 2, 3, 2, 3, 2, 3, 2, 3, 2, 2, 2, 3, 1, 1, 1};
static const byte _color_similar_1[16] = {8, 6, 255, 12,  255, 0, 1, 1, 0, 13,  11,  10, 3,   9,  15, 14};
static const byte _color_similar_2[16] = {5, 7, 255, 255, 255, 8, 7, 6, 5, 12, 255, 255, 9, 255, 255, 255};

static byte GeneratePlayerColor(void)
{
	byte colors[16], pcolor, t2;
	int i,j,n;
	uint32 r;
	Player *p;

	// Initialize array
	for(i=0; i!=16; i++)
		colors[i] = i;

	// And randomize it
	n = 100;
	do {
		r = Random();
		COLOR_SWAP(GB(r, 0, 4), GB(r, 4, 4));
	} while (--n);

	// Bubble sort it according to the values in table 1
	i = 16;
	do {
		for(j=0; j!=15; j++) {
			if (_color_sort[colors[j]] < _color_sort[colors[j+1]]) {
				COLOR_SWAP(j,j+1);
			}
		}
	} while (--i);

	// Move the colors that look similar to each player's color to the side
	FOR_ALL_PLAYERS(p) if (p->is_active) {
		pcolor = p->player_color;
		for(i=0; i!=16; i++) if (colors[i] == pcolor) {
			colors[i] = 0xFF;

			t2 = _color_similar_1[pcolor];
			if (t2 == 0xFF) break;
			for(i=0; i!=15; i++) {
				if (colors[i] == t2) {
					do COLOR_SWAP(i,i+1); while (++i != 15);
					break;
				}
			}

			t2 = _color_similar_2[pcolor];
			if (t2 == 0xFF) break;
			for(i=0; i!=15; i++) {
				if (colors[i] == t2) {
					do COLOR_SWAP(i,i+1); while (++i != 15);
					break;
				}
			}
			break;
		}
	}

	// Return the first available color
	i = 0;
	for(;;) {
		if (colors[i] != 0xFF)
			return colors[i];
		i++;
	}
}

static void GeneratePresidentName(Player *p)
{
	Player *pp;
	char buffer[100], buffer2[40];

	for(;;) {
restart:;

		p->president_name_2 = Random();
		p->president_name_1 = SPECSTR_PRESIDENT_NAME;

		SetDParam(0, p->president_name_2);
		GetString(buffer, p->president_name_1);
		if (strlen(buffer) >= 32 || GetStringWidth(buffer) >= 94)
			continue;

		FOR_ALL_PLAYERS(pp) {
			if (pp->is_active && p != pp) {
				SetDParam(0, pp->president_name_2);
				GetString(buffer2, pp->president_name_1);
				if (strcmp(buffer2, buffer) == 0)
					goto restart;
			}
		}
		return;
	}
}

static Player *AllocatePlayer(void)
{
	Player *p;
	// Find a free slot
	FOR_ALL_PLAYERS(p) {
		if (!p->is_active) {
			int i = p->index;
			memset(p, 0, sizeof(Player));
			p->index = i;
			return p;
		}
	}
	return NULL;
}

Player *DoStartupNewPlayer(bool is_ai)
{
	Player *p;

	p = AllocatePlayer();
	if (p == NULL)
		return NULL;

	// Make a color
	p->player_color = GeneratePlayerColor();
	_player_colors[p->index] = p->player_color;
	p->name_1 = STR_SV_UNNAMED;
	p->is_active = true;

	p->money64 = p->player_money = p->current_loan = 100000;

	p->is_ai = is_ai;
	p->ai.state = 5; /* AIS_WANT_NEW_ROUTE */
	p->share_owners[0] = p->share_owners[1] = p->share_owners[2] = p->share_owners[3] = OWNER_SPECTATOR;

	p->avail_railtypes = GetPlayerRailtypes(p->index);
	p->inaugurated_year = _cur_year;
	p->face = Random();

	/* Engine renewal settings */
	InitialiseEngineReplacement(p);
	p->renew_keep_length = false;
	p->engine_renew = false;
	p->engine_renew_months = -6;
	p->engine_renew_money = 100000;

	GeneratePresidentName(p);

	InvalidateWindow(WC_GRAPH_LEGEND, 0);
	InvalidateWindow(WC_TOOLBAR_MENU, 0);
	InvalidateWindow(WC_CLIENT_LIST, 0);

	if (is_ai && (!_networking || _network_server) && _ai.enabled)
		AI_StartNewAI(p->index);

	return p;
}

void StartupPlayers(void)
{
	// The AI starts like in the setting with +2 month max
	_next_competitor_start = _opt.diff.competitor_start_time * 90 * DAY_TICKS + RandomRange(60 * DAY_TICKS) + 1;
}

static void MaybeStartNewPlayer(void)
{
	uint n;
	Player *p;

	// count number of competitors
	n = 0;
	FOR_ALL_PLAYERS(p) {
		if (p->is_active && p->is_ai)
			n++;
	}

	// when there's a lot of computers in game, the probability that a new one starts is lower
	if (n < (uint)_opt.diff.max_no_competitors)
		if (n < (_network_server ? InteractiveRandomRange(_opt.diff.max_no_competitors + 2) : RandomRange(_opt.diff.max_no_competitors + 2)) )
			/* Send a command to all clients to start  up a new AI. Works fine for Multiplayer and Singleplayer */
			DoCommandP(0, 1, 0, NULL, CMD_PLAYER_CTRL);

	// The next AI starts like the difficulty setting said, with +2 month max
	_next_competitor_start = _opt.diff.competitor_start_time * 90 * DAY_TICKS + 1;
	_next_competitor_start += _network_server ? InteractiveRandomRange(60 * DAY_TICKS) : RandomRange(60 * DAY_TICKS);
}

void InitializePlayers(void)
{
	int i;
	memset(_players, 0, sizeof(_players));
	for(i = 0; i != MAX_PLAYERS; i++)
		_players[i].index=i;
	_cur_player_tick_index = 0;
}

void OnTick_Players(void)
{
	Player *p;

	if (_game_mode == GM_EDITOR)
		return;

	p = GetPlayer(_cur_player_tick_index);
	_cur_player_tick_index = (_cur_player_tick_index + 1) % MAX_PLAYERS;
	if (p->name_1 != 0) GenerateCompanyName(p);

	if (AI_AllowNewAI() && _game_mode != GM_MENU && !--_next_competitor_start)
		MaybeStartNewPlayer();
}

// index is the next parameter in _decode_parameters to set up
StringID GetPlayerNameString(PlayerID player, uint index)
{
	if (IS_HUMAN_PLAYER(player) && player < MAX_PLAYERS) {
		SetDParam(index, player+1);
		return STR_7002_PLAYER;
	}
	return STR_EMPTY;
}

extern void ShowPlayerFinances(int player);

void PlayersYearlyLoop(void)
{
	Player *p;

	// Copy statistics
	FOR_ALL_PLAYERS(p) {
		if (p->is_active) {
			memmove(&p->yearly_expenses[1], &p->yearly_expenses[0], sizeof(p->yearly_expenses) - sizeof(p->yearly_expenses[0]));
			memset(&p->yearly_expenses[0], 0, sizeof(p->yearly_expenses[0]));
			InvalidateWindow(WC_FINANCES, p->index);
		}
	}

	if (_patches.show_finances && _local_player != OWNER_SPECTATOR) {
		ShowPlayerFinances(_local_player);
		p = GetPlayer(_local_player);
		if (p->num_valid_stat_ent > 5 && p->old_economy[0].performance_history < p->old_economy[4].performance_history) {
			SndPlayFx(SND_01_BAD_YEAR);
		} else {
			SndPlayFx(SND_00_GOOD_YEAR);
		}
	}
}

void DeletePlayerWindows(PlayerID pi)
{
	DeleteWindowById(WC_COMPANY, pi);
	DeleteWindowById(WC_FINANCES, pi);
	DeleteWindowById(WC_STATION_LIST, pi);
	DeleteWindowById(WC_TRAINS_LIST,   (INVALID_STATION << 16) | pi);
	DeleteWindowById(WC_ROADVEH_LIST,  (INVALID_STATION << 16) | pi);
	DeleteWindowById(WC_SHIPS_LIST,    (INVALID_STATION << 16) | pi);
	DeleteWindowById(WC_AIRCRAFT_LIST, (INVALID_STATION << 16) | pi);
	DeleteWindowById(WC_BUY_COMPANY, pi);
}

byte GetPlayerRailtypes(PlayerID p)
{
	byte rt = 0;
	EngineID i;

	for (i = 0; i != TOTAL_NUM_ENGINES; i++) {
		const Engine* e = GetEngine(i);

		if (e->type == VEH_Train &&
				(HASBIT(e->player_avail, p) || e->intro_date <= _date) &&
				!(RailVehInfo(i)->flags & RVI_WAGON)) {
			assert(e->railtype < RAILTYPE_END);
			SETBIT(rt, e->railtype);
		}
	}

	return rt;
}

static void DeletePlayerStuff(PlayerID pi)
{
	Player *p;

	DeletePlayerWindows(pi);
	p = GetPlayer(pi);
	DeleteName(p->name_1);
	DeleteName(p->president_name_1);
	p->name_1 = 0;
	p->president_name_1 = 0;
}

/** Change engine renewal parameters
 * @param x,y unused
 * @param p1 bits 0-3 command
 * - p1 = 0 - change auto renew bool
 * - p1 = 1 - change auto renew months
 * - p1 = 2 - change auto renew money
 * - p1 = 3 - change auto renew array
 * - p1 = 4 - change bool, months & money all together
 * - p1 = 5 - change renew_keep_length
 * @param p2 value to set
 * if p1 = 0, then:
 * - p2 = enable engine renewal
 * if p1 = 1, then:
 * - p2 = months left before engine expires to replace it
 * if p1 = 2, then
 * - p2 = minimum amount of money available
 * if p1 = 3, then:
 * - p2 bits  0-15 = old engine type
 * - p2 bits 16-31 = new engine type
 * if p1 = 4, then:
 * - p1 bit     15 = enable engine renewal
 * - p1 bits 16-31 = months left before engine expires to replace it
 * - p2 bits  0-31 = minimum amount of money available
 * if p1 = 5, then
 * - p2 = enable renew_keep_length
 */
int32 CmdReplaceVehicle(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	Player *p;
	if (!(_current_player < MAX_PLAYERS))
		return CMD_ERROR;

	p = GetPlayer(_current_player);
	switch (GB(p1, 0, 3)) {
		case 0:
			if (p->engine_renew == (bool)GB(p2, 0, 1))
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew = (bool)GB(p2, 0, 1);
				if (IsLocalPlayer()) {
					_patches.autorenew = p->engine_renew;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 1:
			if (p->engine_renew_months == (int16)p2)
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew_months = (int16)p2;
				if (IsLocalPlayer()) {
					_patches.autorenew_months = p->engine_renew_months;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 2:
			if (p->engine_renew_money == (uint32)p2)
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew_money = (uint32)p2;
				if (IsLocalPlayer()) {
					_patches.autorenew_money = p->engine_renew_money;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 3: {
			EngineID old_engine_type = GB(p2, 0, 16);
			EngineID new_engine_type = GB(p2, 16, 16);

			if (new_engine_type != INVALID_ENGINE) {
				/* First we make sure that it's a valid type the user requested
				 * check that it's an engine that is in the engine array */
				if(!IsEngineIndex(new_engine_type))
					return CMD_ERROR;

				// check that the new vehicle type is the same as the original one
				if (GetEngine(old_engine_type)->type != GetEngine(new_engine_type)->type)
					return CMD_ERROR;

				// make sure that we do not replace a plane with a helicopter or vise versa
				if (GetEngine(new_engine_type)->type == VEH_Aircraft && HASBIT(AircraftVehInfo(old_engine_type)->subtype, 0) != HASBIT(AircraftVehInfo(new_engine_type)->subtype, 0))
					return CMD_ERROR;

				// make sure that the player can actually buy the new engine
				if (!HASBIT(GetEngine(new_engine_type)->player_avail, _current_player))
					return CMD_ERROR;

				return AddEngineReplacement(p, old_engine_type, new_engine_type, flags);
			} else {
				return RemoveEngineReplacement(p, old_engine_type, flags);
			}
		}

		case 4:
			if (flags & DC_EXEC) {
				p->engine_renew = (bool)GB(p1, 15, 1);
				p->engine_renew_months = (int16)GB(p1, 16, 16);
				p->engine_renew_money = (uint32)p2;

				if (IsLocalPlayer()) {
					_patches.autorenew = p->engine_renew;
					_patches.autorenew_months = p->engine_renew_months;
					_patches.autorenew_money = p->engine_renew_money;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 5:
			if (p->renew_keep_length == (bool)GB(p2, 0, 1))
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->renew_keep_length = (bool)GB(p2, 0, 1);
				if (IsLocalPlayer()) {
					InvalidateWindow(WC_REPLACE_VEHICLE, VEH_Train);
				}
			}
		break;

	}
	return 0;
}

/** Control the players: add, delete, etc.
 * @param x,y unused
 * @param p1 various functionality
 * - p1 = 0 - create a new player, Which player (network) it will be is in p2
 * - p1 = 1 - create a new AI player
 * - p1 = 2 - delete a player. Player is identified by p2
 * - p1 = 3 - merge two companies together. Player to merge #1 with player #2. Identified by p2
 * @param p2 various functionality, dictated by p1
 * - p1 = 0 - ClientID of the newly created player
 * - p1 = 2 - PlayerID of the that is getting deleted
 * - p1 = 3 - #1 p2 = (bit  0-15) - player to merge (p2 & 0xFFFF)
 *          - #2 p2 = (bit 16-31) - player to be merged into ((p2>>16)&0xFFFF)
 * @todo In the case of p1=0, create new player, the clientID of the new player is in parameter
 * p2. This parameter is passed in at function DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND)
 * on the server itself. First of all this is unbelievably ugly; second of all, well,
 * it IS ugly! <b>Someone fix this up :)</b> So where to fix?@n
 * @arg - network_server.c:838 DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND)@n
 * @arg - network_client.c:536 DEF_CLIENT_RECEIVE_COMMAND(PACKET_SERVER_MAP) from where the map has been received
 */
int32 CmdPlayerCtrl(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	if (flags & DC_EXEC) _current_player = OWNER_NONE;

	switch (p1) {
	case 0: { /* Create a new player */
		Player *p;
		PlayerID pid = p2;

		if (!(flags & DC_EXEC) || pid >= MAX_PLAYERS) return 0;

		p = DoStartupNewPlayer(false);

#ifdef ENABLE_NETWORK
		if (_networking && !_network_server && _local_player == OWNER_SPECTATOR)
			/* In case we are a client joining a server... */
			DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);
#endif /* ENABLE_NETWORK */

		if (p != NULL) {
			if (_local_player == OWNER_SPECTATOR && (!_ai.network_client || _ai.network_playas == OWNER_SPECTATOR)) {
				/* Check if we do not want to be a spectator in network */
				if (!_networking || (_network_server && !_network_dedicated) || _network_playas != OWNER_SPECTATOR || _ai.network_client) {
					if (_ai.network_client) {
						/* As ai-network-client, we have our own rulez (disable GUI and stuff) */
						_ai.network_playas = p->index;
						_local_player      = OWNER_SPECTATOR;
						if (_ai.network_playas != OWNER_SPECTATOR) {
							/* If we didn't join the game as a spectator, activate the AI */
							AI_StartNewAI(_ai.network_playas);
						}
					} else {
						_local_player = p->index;
					}
					MarkWholeScreenDirty();
				}
			} else if (p->index == _local_player) {
				DoCommandP(0, (_patches.autorenew << 15 ) | (_patches.autorenew_months << 16) | 4, _patches.autorenew_money, NULL, CMD_REPLACE_VEHICLE);
			}
#ifdef ENABLE_NETWORK
			if (_network_server) {
				/* XXX - UGLY! p2 (pid) is mis-used to fetch the client-id, done at server-side
				 * in network_server.c:838, function DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND) */
				NetworkClientInfo *ci = &_network_client_info[pid];
				ci->client_playas = p->index + 1;
				NetworkUpdateClientInfo(ci->client_index);

				if (ci->client_playas != 0 && ci->client_playas <= MAX_PLAYERS) {
					PlayerID player_backup = _local_player;
					_network_player_info[p->index].months_empty = 0;

					/* XXX - When a client joins, we automatically set its name to the
					 * player's name (for some reason). As it stands now only the server
					 * knows the client's name, so it needs to send out a "broadcast" to
					 * do this. To achieve this we send a network command. However, it
					 * uses _local_player to execute the command as.  To prevent abuse
					 * (eg. only yourself can change your name/company), we 'cheat' by
					 * impersonation _local_player as the server. Not the best solution;
					 * but it works.
					 * TODO: Perhaps this could be improved by when the client is ready
					 * with joining to let it send itself the command, and not the server?
					 * For example in network_client.c:534? */
					_cmd_text = ci->client_name;
					_local_player = ci->client_playas - 1;
					NetworkSend_Command(0, 0, 0, CMD_CHANGE_PRESIDENT_NAME, NULL);
					_local_player = player_backup;
				}
			}
		} else if (_network_server) {
				/* XXX - UGLY! p2 (pid) is mis-used to fetch the client-id, done at server-side
				* in network_server.c:838, function DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND) */
			NetworkClientInfo *ci = &_network_client_info[pid];
			ci->client_playas = OWNER_SPECTATOR;
			NetworkUpdateClientInfo(ci->client_index);
		}
#else
		}
#endif /* ENABLE_NETWORK */
	} break;

	case 1: /* Make a new AI player */
		if (!(flags & DC_EXEC)) return 0;

		DoStartupNewPlayer(true);
		break;

	case 2: { /* Delete a player */
		Player *p;

		if (p2 >= MAX_PLAYERS) return CMD_ERROR;

		if (!(flags & DC_EXEC)) return 0;

		p = GetPlayer(p2);

		/* Only allow removal of HUMAN companies */
		if (IS_HUMAN_PLAYER(p->index)) {
			/* Delete any open window of the company */
			DeletePlayerWindows(p->index);

			/* Show the bankrupt news */
			SetDParam(0, p->name_1);
			SetDParam(1, p->name_2);
			AddNewsItem( (StringID)(p->index + 16*3), NEWS_FLAGS(NM_CALLBACK, 0, NT_COMPANY_INFO, DNC_BANKRUPCY),0,0);

			/* Remove the company */
			ChangeOwnershipOfPlayerItems(p->index, OWNER_SPECTATOR);
			p->money64 = p->player_money = 100000000; // XXX - wtf?
			p->is_active = false;
		}
	} break;

	case 3: { /* Merge a company (#1) into another company (#2), elimination company #1 */
		PlayerID pid_old = GB(p2,  0, 16);
		PlayerID pid_new = GB(p2, 16, 16);

		if (pid_old >= MAX_PLAYERS || pid_new >= MAX_PLAYERS) return CMD_ERROR;

		if (!(flags & DC_EXEC)) return CMD_ERROR;

		ChangeOwnershipOfPlayerItems(pid_old, pid_new);
		DeletePlayerStuff(pid_old);
	} break;
	default: return CMD_ERROR;
	}

	return 0;
}

static const StringID _endgame_perf_titles[16] = {
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0214_ENTREPRENEUR,
	STR_0214_ENTREPRENEUR,
	STR_0215_INDUSTRIALIST,
	STR_0215_INDUSTRIALIST,
	STR_0216_CAPITALIST,
	STR_0216_CAPITALIST,
	STR_0217_MAGNATE,
	STR_0217_MAGNATE,
	STR_0218_MOGUL,
	STR_0218_MOGUL,
	STR_0219_TYCOON_OF_THE_CENTURY,
};

StringID EndGameGetPerformanceTitleFromValue(uint value)
{
	value = minu(value, 1000) >> 6;
	if (value >= lengthof(_endgame_perf_titles)) value = lengthof(_endgame_perf_titles) - 1;

	return _endgame_perf_titles[value];
}

/* Return true if any cheat has been used, false otherwise */
static bool CheatHasBeenUsed(void)
{
	const Cheat* cht = (Cheat*) &_cheats;
	const Cheat* cht_last = &cht[sizeof(_cheats) / sizeof(Cheat)];

	for (; cht != cht_last; cht++) {
		if (cht->been_used)
			return true;
	}

	return false;
}

/* Save the highscore for the player */
int8 SaveHighScoreValue(const Player *p)
{
	HighScore *hs = _highscore_table[_opt.diff_level];
	uint i;
	uint16 score = p->old_economy[0].performance_history;

	/* Exclude cheaters from the honour of being in the highscore table */
	if (CheatHasBeenUsed())
		return -1;

	for (i = 0; i < lengthof(_highscore_table[0]); i++) {
		/* You are in the TOP5. Move all values one down and save us there */
		if (hs[i].score <= score) {
			char buf[sizeof(hs[i].company)];

			// move all elements one down starting from the replaced one
			memmove(&hs[i + 1], &hs[i], sizeof(HighScore) * (lengthof(_highscore_table[0]) - i - 1));
			SetDParam(0, p->president_name_1);
			SetDParam(1, p->president_name_2);
			SetDParam(2, p->name_1);
			SetDParam(3, p->name_2);
			GetString(buf, STR_HIGHSCORE_NAME); // get manager/company name string
			ttd_strlcpy(hs[i].company, buf, sizeof(buf));
			hs[i].score = score;
			hs[i].title = EndGameGetPerformanceTitleFromValue(score);
			return i;
		}
	}

	return -1; // too bad; we did not make it into the top5
}

/* Sort all players given their performance */
static int CDECL HighScoreSorter(const void *a, const void *b)
{
	const Player *pa = *(const Player* const*)a;
	const Player *pb = *(const Player* const*)b;

	return pb->old_economy[0].performance_history - pa->old_economy[0].performance_history;
}

/* Save the highscores in a network game when it has ended */
#define LAST_HS_ITEM lengthof(_highscore_table) - 1
int8 SaveHighScoreValueNetwork(void)
{
	Player *p, *player_sort[MAX_PLAYERS];
	size_t count = 0;
	int8 player = -1;

	/* Sort all active players with the highest score first */
	FOR_ALL_PLAYERS(p) {
		if (p->is_active)
			player_sort[count++] = p;
	}
	qsort(player_sort, count, sizeof(player_sort[0]), HighScoreSorter);

	{
		HighScore *hs;
		Player* const *p_cur = &player_sort[0];
		uint8 i;

		memset(_highscore_table[LAST_HS_ITEM], 0, sizeof(_highscore_table[0]));

		/* Copy over Top5 companies */
		for (i = 0; i < lengthof(_highscore_table[LAST_HS_ITEM]) && i < count; i++) {
			char buf[sizeof(_highscore_table[0]->company)];

			hs = &_highscore_table[LAST_HS_ITEM][i];
			SetDParam(0, (*p_cur)->president_name_1);
			SetDParam(1, (*p_cur)->president_name_2);
			SetDParam(2, (*p_cur)->name_1);
			SetDParam(3, (*p_cur)->name_2);
			GetString(buf, STR_HIGHSCORE_NAME); // get manager/company name string

			ttd_strlcpy(hs->company, buf, sizeof(buf));
			hs->score = (*p_cur)->old_economy[0].performance_history;
			hs->title = EndGameGetPerformanceTitleFromValue(hs->score);

			// get the ranking of the local player
			if ((*p_cur)->index == _local_player)
				player = i;

			p_cur++;
		}
	}

	/* Add top5 players to highscore table */
	return player;
}

/* Save HighScore table to file */
void SaveToHighScore(void)
{
	FILE *fp = fopen(_highscore_file, "wb");

	if (fp != NULL) {
		uint i;
		HighScore *hs;

		for (i = 0; i < LAST_HS_ITEM; i++) { // don't save network highscores
			for (hs = _highscore_table[i]; hs != endof(_highscore_table[i]); hs++) {
				/* First character is a command character, so strlen will fail on that */
				byte length = min(sizeof(hs->company), (hs->company[0] == '\0') ? 0 : strlen(&hs->company[1]) + 1);

				fwrite(&length, sizeof(length), 1, fp); // write away string length
				fwrite(hs->company, length, 1, fp);
				fwrite(&hs->score, sizeof(hs->score), 1, fp);
				fwrite("", 2, 1, fp); /* XXX - placeholder for hs->title, not saved anymore; compatibility */
			}
		}
		fclose(fp);
	}
}

/* Initialize the highscore table to 0 and if any file exists, load in values */
void LoadFromHighScore(void)
{
	FILE *fp = fopen(_highscore_file, "rb");

	memset(_highscore_table, 0, sizeof(_highscore_table));

	if (fp != NULL) {
		uint i;
		HighScore *hs;

		for (i = 0; i < LAST_HS_ITEM; i++) { // don't load network highscores
			for (hs = _highscore_table[i]; hs != endof(_highscore_table[i]); hs++) {
				byte length;
				fread(&length, sizeof(length), 1, fp);

				fread(hs->company, 1, length, fp);
				fread(&hs->score, sizeof(hs->score), 1, fp);
				fseek(fp, 2, SEEK_CUR); /* XXX - placeholder for hs->title, not saved anymore; compatibility */
				hs->title = EndGameGetPerformanceTitleFromValue(hs->score);
			}
		}
		fclose(fp);
	}

	/* Initialize end of game variable (when to show highscore chart) */
	 _patches.ending_date = 2051;
}

void InitialiseEngineReplacement(Player *p)
{
	EngineID engine;

	for (engine = 0; engine < TOTAL_NUM_ENGINES; engine++)
		p->engine_replacement[engine] = INVALID_ENGINE;
}

/**
 * Retrieve the engine replacement for the given player and original engine type.
 * @param p Player.
 * @param engine Engine type.
 * @return Assigned replacement engine.
 */
EngineID EngineReplacement(const Player *p, EngineID engine)
{
	return p->engine_replacement[engine];
}

/**
 * Check if an engine has a replacement set up.
 * @param p Player.
 * @param engine Engine type.
 * @return True if there is a replacement for the original engine type.
 */
bool EngineHasReplacement(const Player *p, EngineID engine)
{
	return EngineReplacement(p, engine) != INVALID_ENGINE;
}

/**
 * Add an engine replacement for the player.
 * @param p Player.
 * @param old_engine The original engine type.
 * @param new_engine The replacement engine type.
 * @param flags The calling command flags.
 * @return 0 on success, CMD_ERROR on failure.
 */
int32 AddEngineReplacement(Player *p, EngineID old_engine, EngineID new_engine, uint32 flags)
{
	if (flags & DC_EXEC) p->engine_replacement[old_engine] = new_engine;
	return 0;
}

/**
 * Remove an engine replacement for the player.
 * @param p Player.
 * @param engine The original engine type.
 * @param flags The calling command flags.
 * @return 0 on success, CMD_ERROR on failure.
 */
int32 RemoveEngineReplacement(Player *p, EngineID engine, uint32 flags)
{
	if (flags & DC_EXEC) p->engine_replacement[engine] = INVALID_ENGINE;
	return 0;
}

// Save/load of players
static const SaveLoad _player_desc[] = {
	SLE_VAR(Player,name_2,					SLE_UINT32),
	SLE_VAR(Player,name_1,					SLE_STRINGID),

	SLE_VAR(Player,president_name_1,SLE_UINT16),
	SLE_VAR(Player,president_name_2,SLE_UINT32),

	SLE_VAR(Player,face,						SLE_UINT32),

	// money was changed to a 64 bit field in savegame version 1.
	SLE_CONDVAR(Player,money64,			SLE_VAR_I64 | SLE_FILE_I32, 0, 0),
	SLE_CONDVAR(Player,money64,			SLE_INT64, 1, 255),

	SLE_VAR(Player,current_loan,		SLE_INT32),

	SLE_VAR(Player,player_color,		SLE_UINT8),
	SLE_VAR(Player,player_money_fraction,SLE_UINT8),
	SLE_VAR(Player,avail_railtypes,		SLE_UINT8),
	SLE_VAR(Player,block_preview,		SLE_UINT8),

	SLE_VAR(Player,cargo_types,			SLE_UINT16),
	SLE_CONDVAR(Player, location_of_house,     SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(Player, location_of_house,     SLE_UINT32, 6, 255),
	SLE_CONDVAR(Player, last_build_coordinate, SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(Player, last_build_coordinate, SLE_UINT32, 6, 255),
	SLE_VAR(Player,inaugurated_year,SLE_UINT8),

	SLE_ARR(Player,share_owners,		SLE_UINT8, 4),

	SLE_VAR(Player,num_valid_stat_ent,SLE_UINT8),

	SLE_VAR(Player,quarters_of_bankrupcy,SLE_UINT8),
	SLE_VAR(Player,bankrupt_asked,	SLE_UINT8),
	SLE_VAR(Player,bankrupt_timeout,SLE_INT16),
	SLE_VAR(Player,bankrupt_value,	SLE_INT32),

	// yearly expenses was changed to 64-bit in savegame version 2.
	SLE_CONDARR(Player,yearly_expenses,	SLE_FILE_I32|SLE_VAR_I64, 3*13, 0, 1),
	SLE_CONDARR(Player,yearly_expenses,	SLE_INT64, 3*13, 2, 255),

	SLE_CONDVAR(Player,is_ai,			SLE_UINT8, 2, 255),
	SLE_CONDVAR(Player,is_active,	SLE_UINT8, 4, 255),

	// Engine renewal settings
	SLE_CONDARR(Player,engine_replacement,  SLE_UINT16, 256, 16, 255),
	SLE_CONDVAR(Player,engine_renew,         SLE_UINT8,      16, 255),
	SLE_CONDVAR(Player,engine_renew_months,  SLE_INT16,      16, 255),
	SLE_CONDVAR(Player,engine_renew_money,  SLE_UINT32,      16, 255),
	SLE_CONDVAR(Player,renew_keep_length,    SLE_UINT8,       2, 255),	// added with 16.1, but was blank since 2

	// reserve extra space in savegame here. (currently 63 bytes)
	SLE_CONDARR(NullStruct,null,SLE_FILE_U8  | SLE_VAR_NULL, 7, 2, 255),
	SLE_CONDARR(NullStruct,null,SLE_FILE_U64 | SLE_VAR_NULL, 7, 2, 255),

	SLE_END()
};

static const SaveLoad _player_economy_desc[] = {
	// these were changed to 64-bit in savegame format 2
	SLE_CONDVAR(PlayerEconomyEntry,income,							SLE_INT32, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry,expenses,						SLE_INT32, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry,company_value, SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry,income,	SLE_FILE_I64 | SLE_VAR_I32, 2, 255),
	SLE_CONDVAR(PlayerEconomyEntry,expenses,SLE_FILE_I64 | SLE_VAR_I32, 2, 255),
	SLE_CONDVAR(PlayerEconomyEntry,company_value, SLE_INT64, 2, 255),

	SLE_VAR(PlayerEconomyEntry,delivered_cargo,			SLE_INT32),
	SLE_VAR(PlayerEconomyEntry,performance_history,	SLE_INT32),

	SLE_END()
};

static const SaveLoad _player_ai_desc[] = {
	SLE_VAR(PlayerAI,state,							SLE_UINT8),
	SLE_VAR(PlayerAI,tick,							SLE_UINT8),
	SLE_CONDVAR(PlayerAI,state_counter, SLE_FILE_U16 | SLE_VAR_U32, 0, 12),
	SLE_CONDVAR(PlayerAI,state_counter, SLE_UINT32, 13, 255),
	SLE_VAR(PlayerAI,timeout_counter,		SLE_UINT16),

	SLE_VAR(PlayerAI,state_mode,				SLE_UINT8),
	SLE_VAR(PlayerAI,banned_tile_count,	SLE_UINT8),
	SLE_VAR(PlayerAI,railtype_to_use,		SLE_UINT8),

	SLE_VAR(PlayerAI,cargo_type,				SLE_UINT8),
	SLE_VAR(PlayerAI,num_wagons,				SLE_UINT8),
	SLE_VAR(PlayerAI,build_kind,				SLE_UINT8),
	SLE_VAR(PlayerAI,num_build_rec,			SLE_UINT8),
	SLE_VAR(PlayerAI,num_loco_to_build,	SLE_UINT8),
	SLE_VAR(PlayerAI,num_want_fullload,	SLE_UINT8),

	SLE_VAR(PlayerAI,route_type_mask,		SLE_UINT8),

	SLE_CONDVAR(PlayerAI, start_tile_a, SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(PlayerAI, start_tile_a, SLE_UINT32, 6, 255),
	SLE_CONDVAR(PlayerAI, cur_tile_a,   SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(PlayerAI, cur_tile_a,   SLE_UINT32, 6, 255),
	SLE_VAR(PlayerAI,start_dir_a,				SLE_UINT8),
	SLE_VAR(PlayerAI,cur_dir_a,					SLE_UINT8),

	SLE_CONDVAR(PlayerAI, start_tile_b, SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(PlayerAI, start_tile_b, SLE_UINT32, 6, 255),
	SLE_CONDVAR(PlayerAI, cur_tile_b,   SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(PlayerAI, cur_tile_b,   SLE_UINT32, 6, 255),
	SLE_VAR(PlayerAI,start_dir_b,				SLE_UINT8),
	SLE_VAR(PlayerAI,cur_dir_b,					SLE_UINT8),

	SLE_REF(PlayerAI,cur_veh,						REF_VEHICLE),

	SLE_ARR(PlayerAI,wagon_list,				SLE_UINT16, 9),
	SLE_ARR(PlayerAI,order_list_blocks,	SLE_UINT8, 20),
	SLE_ARR(PlayerAI,banned_tiles,			SLE_UINT16, 16),

	SLE_CONDARR(NullStruct,null,SLE_FILE_U64 | SLE_VAR_NULL, 8, 2, 255),
	SLE_END()
};

static const SaveLoad _player_ai_build_rec_desc[] = {
	SLE_CONDVAR(AiBuildRec,spec_tile, SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(AiBuildRec,spec_tile, SLE_UINT32, 6, 255),
	SLE_CONDVAR(AiBuildRec,use_tile,  SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(AiBuildRec,use_tile,  SLE_UINT32, 6, 255),
	SLE_VAR(AiBuildRec,rand_rng,			SLE_UINT8),
	SLE_VAR(AiBuildRec,cur_building_rule,SLE_UINT8),
	SLE_VAR(AiBuildRec,unk6,					SLE_UINT8),
	SLE_VAR(AiBuildRec,unk7,					SLE_UINT8),
	SLE_VAR(AiBuildRec,buildcmd_a,		SLE_UINT8),
	SLE_VAR(AiBuildRec,buildcmd_b,		SLE_UINT8),
	SLE_VAR(AiBuildRec,direction,			SLE_UINT8),
	SLE_VAR(AiBuildRec,cargo,					SLE_UINT8),
	SLE_END()
};

static void SaveLoad_PLYR(Player *p) {
	int i;

	SlObject(p, _player_desc);

	// Write AI?
	if (!IS_HUMAN_PLAYER(p->index)) {
		SlObject(&p->ai, _player_ai_desc);
		for(i=0; i!=p->ai.num_build_rec; i++)
			SlObject(&p->ai.src + i, _player_ai_build_rec_desc);
	}

	// Write economy
	SlObject(&p->cur_economy, _player_economy_desc);

	// Write old economy entries.
	{
		PlayerEconomyEntry *pe;
		for(i=p->num_valid_stat_ent,pe=p->old_economy; i!=0; i--,pe++)
			SlObject(pe, _player_economy_desc);
	}
}

static void Save_PLYR(void)
{
	Player *p;
	FOR_ALL_PLAYERS(p) {
		if (p->is_active) {
			SlSetArrayIndex(p->index);
			SlAutolength((AutolengthProc*)SaveLoad_PLYR, p);
		}
	}
}

static void Load_PLYR(void)
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Player *p = GetPlayer(index);
		SaveLoad_PLYR(p);
		_player_colors[index] = p->player_color;
		UpdatePlayerMoney32(p);

		/* This is needed so an AI is attached to a loaded AI */
		if (p->is_ai && (!_networking || _network_server) && _ai.enabled)
			AI_StartNewAI(p->index);
	}
}

const ChunkHandler _player_chunk_handlers[] = {
	{ 'PLYR', Save_PLYR, Load_PLYR, CH_ARRAY | CH_LAST},
};
