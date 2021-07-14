/* $Id: openttd.c 3316 2005-12-18 14:03:28Z peter1138 $ */

#include "stdafx.h"
#include "string.h"
#include "table/strings.h"
#include "debug.h"
#include "driver.h"
#include "saveload.h"
#include "strings.h"
#include "map.h"
#include "tile.h"

#define VARDEF
#include "openttd.h"
#include "functions.h"
#include "mixer.h"
#include "spritecache.h"
#include "gfx.h"
#include "gfxinit.h"
#include "gui.h"
#include "station.h"
#include "vehicle.h"
#include "viewport.h"
#include "window.h"
#include "player.h"
#include "command.h"
#include "town.h"
#include "industry.h"
#include "news.h"
#include "engine.h"
#include "sound.h"
#include "economy.h"
#include "fileio.h"
#include "hal.h"
#include "airport.h"
#include "console.h"
#include "screenshot.h"
#include "network.h"
#include "signs.h"
#include "depot.h"
#include "waypoint.h"
#include "ai/ai.h"

#include <stdarg.h>

void GenerateWorld(int mode, uint size_x, uint size_y);
void CallLandscapeTick(void);
void IncreaseDate(void);
void DoPaletteAnimations(void);
void MusicLoop(void);
void ResetMusic(void);
void InitializeStations(void);
void DeleteAllPlayerStations(void);

extern void SetDifficultyLevel(int mode, GameOptions *gm_opt);
extern void DoStartupNewPlayer(bool is_ai);
extern void ShowOSErrorBox(const char *buf);

/* TODO: usrerror() for errors which are not of an internal nature but
 * caused by the user, i.e. missing files or fatal configuration errors.
 * Post-0.4.0 since Celestar doesn't want this in SVN before. --pasky */

void CDECL error(const char* s, ...)
{
	va_list va;
	char buf[512];

	va_start(va, s);
	vsprintf(buf, s, va);
	va_end(va);

	ShowOSErrorBox(buf);
	if (_video_driver != NULL) _video_driver->stop();

	assert(0);
	exit(1);
}

void CDECL ShowInfoF(const char *str, ...)
{
	va_list va;
	char buf[1024];
	va_start(va, str);
	vsprintf(buf, str, va);
	va_end(va);
	ShowInfo(buf);
}


void *ReadFileToMem(const char *filename, size_t *lenp, size_t maxsize)
{
	FILE *in;
	byte *mem;
	size_t len;

	in = fopen(filename, "rb");
	if (in == NULL) return NULL;

	fseek(in, 0, SEEK_END);
	len = ftell(in);
	fseek(in, 0, SEEK_SET);
	if (len > maxsize || (mem = malloc(len + 1)) == NULL) {
		fclose(in);
		return NULL;
	}
	mem[len] = 0;
	if (fread(mem, len, 1, in) != 1) {
		fclose(in);
		free(mem);
		return NULL;
	}
	fclose(in);

	*lenp = len;
	return mem;
}

static void showhelp(void)
{
	char buf[4096], *p;

	p = strecpy(buf,
		"Command line options:\n"
		"  -v drv              = Set video driver (see below)\n"
		"  -s drv              = Set sound driver (see below)\n"
		"  -m drv              = Set music driver (see below)\n"
		"  -r res              = Set resolution (for instance 800x600)\n"
		"  -h                  = Display this help text\n"
		"  -t date             = Set starting date\n"
		"  -d [[fac=]lvl[,...]]= Debug mode\n"
		"  -e                  = Start Editor\n"
		"  -g [savegame]       = Start new/save game immediately\n"
		"  -G seed             = Set random seed\n"
		"  -n [ip#player:port] = Start networkgame\n"
		"  -D                  = Start dedicated server\n"
		#if !defined(__MORPHOS__) && !defined(__AMIGA__)
		"  -f                  = Fork into the background (dedicated only)\n"
		#endif
		"  -i                  = Force to use the DOS palette (use this if you see a lot of pink)\n"
		"  -p #player          = Player as #player (deprecated) (network only)\n"
		"  -c config_file      = Use 'config_file' instead of 'openttd.cfg'\n",
		lastof(buf)
	);

	GetDriverList(p);

	ShowInfo(buf);
}


typedef struct {
	char *opt;
	int numleft;
	char **argv;
	const char *options;
	char *cont;
} MyGetOptData;

static void MyGetOptInit(MyGetOptData *md, int argc, char **argv, const char *options)
{
	md->cont = NULL;
	md->numleft = argc;
	md->argv = argv;
	md->options = options;
}

static int MyGetOpt(MyGetOptData *md)
{
	char *s,*r,*t;

	s = md->cont;
	if (s != NULL)
		goto md_continue_here;

	for (;;) {
		if (--md->numleft < 0) return -1;

		s = *md->argv++;
		if (*s == '-') {
md_continue_here:;
			s++;
			if (*s != 0) {
				// Found argument, try to locate it in options.
				if (*s == ':' || (r = strchr(md->options, *s)) == NULL) {
					// ERROR!
					return -2;
				}
				if (r[1] == ':') {
					// Item wants an argument. Check if the argument follows, or if it comes as a separate arg.
					if (!*(t = s + 1)) {
						// It comes as a separate arg. Check if out of args?
						if (--md->numleft < 0 || *(t = *md->argv) == '-') {
							// Check if item is optional?
							if (r[2] != ':')
								return -2;
							md->numleft++;
							t = NULL;
						} else {
							md->argv++;
						}
					}
					md->opt = t;
					md->cont = NULL;
					return *s;
				}
				md->opt = NULL;
				md->cont = s;
				return *s;
			}
		} else {
			// This is currently not supported.
			return -2;
		}
	}
}


static void ParseResolution(int res[2], const char* s)
{
	char *t = strchr(s, 'x');
	if (t == NULL) {
		ShowInfoF("Invalid resolution '%s'", s);
		return;
	}

	res[0] = clamp(strtoul(s, NULL, 0), 64, MAX_SCREEN_WIDTH);
	res[1] = clamp(strtoul(t + 1, NULL, 0), 64, MAX_SCREEN_HEIGHT);
}

static void InitializeDynamicVariables(void)
{
	/* Dynamic stuff needs to be initialized somewhere... */
	_station_sort  = NULL;
	_vehicle_sort  = NULL;
	_town_sort     = NULL;
	_industry_sort = NULL;
}

static void UnInitializeDynamicVariables(void)
{
	/* Dynamic stuff needs to be free'd somewhere... */
	CleanPool(&_town_pool);
	CleanPool(&_industry_pool);
	CleanPool(&_station_pool);
	CleanPool(&_vehicle_pool);
	CleanPool(&_sign_pool);
	CleanPool(&_order_pool);

	free(_station_sort);
	free(_vehicle_sort);
	free(_town_sort);
	free(_industry_sort);
}

static void UnInitializeGame(void)
{
	UnInitWindowSystem();

	free(_config_file);
}

static void LoadIntroGame(void)
{
	char filename[256];

	_game_mode = GM_MENU;
	CLRBITS(_display_opt, DO_TRANS_BUILDINGS); // don't make buildings transparent in intro
	_opt_ptr = &_opt_newgame;

	GfxLoadSprites();
	LoadStringWidthTable();

	// Setup main window
	ResetWindowSystem();
	SetupColorsAndInitialWindow();

	// Generate a world.
	sprintf(filename, "%sopntitle.dat",  _path.data_dir);
	if (SaveOrLoad(filename, SL_LOAD) != SL_OK) {
#if defined SECOND_DATA_DIR
		sprintf(filename, "%sopntitle.dat",  _path.second_data_dir);
		if (SaveOrLoad(filename, SL_LOAD) != SL_OK)
#endif
			GenerateWorld(1, 64, 64); // if failed loading, make empty world.
	}

	_pause = 0;
	_local_player = 0;
	MarkWholeScreenDirty();

	// Play main theme
	if (_music_driver->is_song_playing()) ResetMusic();
}

#if defined(UNIX) && !defined(__MORPHOS__)
extern void DedicatedFork(void);
#endif

int ttd_main(int argc, char* argv[])
{
	MyGetOptData mgo;
	int i;
	bool network = false;
	char *network_conn = NULL;
	const char *optformat;
	char musicdriver[16], sounddriver[16], videodriver[16];
	int resolution[2] = {0,0};
	uint startdate = -1;
	bool dedicated;

	musicdriver[0] = sounddriver[0] = videodriver[0] = 0;

	_game_mode = GM_MENU;
	_switch_mode = SM_MENU;
	_switch_mode_errorstr = INVALID_STRING_ID;
	_dedicated_forks = false;
	dedicated = false;
	_config_file = NULL;

	// The last param of the following function means this:
	//   a letter means: it accepts that param (e.g.: -h)
	//   a ':' behind it means: it need a param (e.g.: -m<driver>)
	//   a '::' behind it means: it can optional have a param (e.g.: -d<debug>)
	#if !defined(__MORPHOS__) && !defined(__AMIGA__) && !defined(WIN32)
		optformat = "bm:s:v:hDfn::eit:d::r:g::G:p:c:";
	#else
		optformat = "bm:s:v:hDn::eit:d::r:g::G:p:c:"; // no fork option
	#endif

	MyGetOptInit(&mgo, argc-1, argv+1, optformat);
	while ((i = MyGetOpt(&mgo)) != -1) {
		switch(i) {
		case 'm': ttd_strlcpy(musicdriver, mgo.opt, sizeof(musicdriver)); break;
		case 's': ttd_strlcpy(sounddriver, mgo.opt, sizeof(sounddriver)); break;
		case 'v': ttd_strlcpy(videodriver, mgo.opt, sizeof(videodriver)); break;
		case 'D': {
				strcpy(musicdriver, "null");
				strcpy(sounddriver, "null");
				strcpy(videodriver, "dedicated");
				dedicated = true;
			} break;
		case 'f': {
				_dedicated_forks = true;
			}; break;
		case 'n': {
				network = true;
				if (mgo.opt)
					// Optional, you can give an IP
					network_conn = mgo.opt;
				else
					network_conn = NULL;
			} break;
		case 'b': _ai.network_client = true; break;
		case 'r': ParseResolution(resolution, mgo.opt); break;
		case 't': startdate = atoi(mgo.opt); break;
		case 'd': {
#if defined(WIN32)
				CreateConsole();
#endif
				if (mgo.opt != NULL) SetDebugString(mgo.opt);
			} break;
		case 'e': _switch_mode = SM_EDITOR; break;
		case 'i': _use_dos_palette = true; break;
		case 'g':
			if (mgo.opt != NULL) {
				strcpy(_file_to_saveload.name, mgo.opt);
				_switch_mode = SM_LOAD;
			} else
				_switch_mode = SM_NEWGAME;
			break;
		case 'G':
			_random_seeds[0][0] = atoi(mgo.opt);
			break;
		case 'p': {
			int i = atoi(mgo.opt);
			// Play as an other player in network games
			if (IS_INT_INSIDE(i, 1, MAX_PLAYERS)) _network_playas = i;
			break;
		}
		case 'c':
			_config_file = strdup(mgo.opt);
			break;
		case -2:
		case 'h':
			showhelp();
			return 0;
		}
	}

	if (_ai.network_client && !network) {
		_ai.network_client = false;
		DEBUG(ai, 0)("[AI] Can't enable network-AI, because '-n' is not used\n");
	}

	DeterminePaths();
	CheckExternalFiles();

#if defined(UNIX) && !defined(__MORPHOS__)
	// We must fork here, or we'll end up without some resources we need (like sockets)
	if (_dedicated_forks)
		DedicatedFork();
#endif

	LoadFromConfig();
	CheckConfig();
	LoadFromHighScore();

	// override config?
	if (musicdriver[0]) ttd_strlcpy(_ini_musicdriver, musicdriver, sizeof(_ini_musicdriver));
	if (sounddriver[0]) ttd_strlcpy(_ini_sounddriver, sounddriver, sizeof(_ini_sounddriver));
	if (videodriver[0]) ttd_strlcpy(_ini_videodriver, videodriver, sizeof(_ini_videodriver));
	if (resolution[0]) { _cur_resolution[0] = resolution[0]; _cur_resolution[1] = resolution[1]; }
	if (startdate != (uint)-1) _patches.starting_date = startdate;

	if (_dedicated_forks && !dedicated)
		_dedicated_forks = false;

	// enumerate language files
	InitializeLanguagePacks();

	// initialize screenshot formats
	InitializeScreenshotFormats();

	// initialize airport state machines
	InitializeAirports();

	/* initialize all variables that are allocated dynamically */
	InitializeDynamicVariables();

	/* start the AI */
	AI_Initialize();

	// Sample catalogue
	DEBUG(misc, 1) ("Loading sound effects...");
	MxInitialize(11025);
	SoundInitialize("sample.cat");

	// This must be done early, since functions use the InvalidateWindow* calls
	InitWindowSystem();

	GfxLoadSprites();
	LoadStringWidthTable();

	DEBUG(misc, 1) ("Loading drivers...");
	LoadDriver(SOUND_DRIVER, _ini_sounddriver);
	LoadDriver(MUSIC_DRIVER, _ini_musicdriver);
	LoadDriver(VIDEO_DRIVER, _ini_videodriver); // load video last, to prevent an empty window while sound and music loads
	_savegame_sort_order = SORT_BY_DATE | SORT_DESCENDING;

#ifdef ENABLE_NETWORK
	// initialize network-core
	NetworkStartUp();
#endif /* ENABLE_NETWORK */

	_opt_ptr = &_opt_newgame;

	/* XXX - ugly hack, if diff_level is 9, it means we got no setting from the config file */
	if (_opt_newgame.diff_level == 9)
		SetDifficultyLevel(0, &_opt_newgame);

	// initialize the ingame console
	IConsoleInit();
	InitializeGUI();
	IConsoleCmdExec("exec scripts/autoexec.scr 0");

	GenerateWorld(1, 64, 64); // Make the viewport initialization happy

#ifdef ENABLE_NETWORK
	if ((network) && (_network_available)) {
		if (network_conn != NULL) {
			const char *port = NULL;
			const char *player = NULL;
			uint16 rport;

			rport = NETWORK_DEFAULT_PORT;

			ParseConnectionString(&player, &port, network_conn);

			if (player != NULL) _network_playas = atoi(player);
			if (port != NULL) rport = atoi(port);

			LoadIntroGame();
			_switch_mode = SM_NONE;
			NetworkClientConnectGame(network_conn, rport);
		}
	}
#endif /* ENABLE_NETWORK */

	_video_driver->main_loop();

	WaitTillSaved();
	IConsoleFree();

#ifdef ENABLE_NETWORK
	if (_network_available) {
		// Shut down the network and close any open connections
		NetworkDisconnect();
		NetworkUDPClose();
		NetworkShutDown();
	}
#endif /* ENABLE_NETWORK */

	_video_driver->stop();
	_music_driver->stop();
	_sound_driver->stop();

	SaveToConfig();
	SaveToHighScore();

	// uninitialize airport state machines
	UnInitializeAirports();

	/* uninitialize variables that are allocated dynamic */
	UnInitializeDynamicVariables();

	/* stop the AI */
	AI_Uninitialize();

	/* Close all and any open filehandles */
	FioCloseAll();
	UnInitializeGame();

	return 0;
}

/** Mutex so that only one thread can communicate with the main program
 * at any given time */
static ThreadMsg _message = 0;

static inline void OTTD_ReleaseMutex(void) {_message = 0;}
static inline ThreadMsg OTTD_PollThreadEvent(void) {return _message;}

/** Called by running thread to execute some action in the main game.
 * It will stall as long as the mutex is not freed (handled) by the game */
void OTTD_SendThreadMessage(ThreadMsg msg)
{
	if (_exit_game) return;
	while (_message != 0) CSleep(10);

	_message = msg;
}


/** Handle the user-messages sent to us
 * @param message message sent
 */
void ProcessSentMessage(ThreadMsg message)
{
	switch (message) {
		case MSG_OTTD_SAVETHREAD_START: SaveFileStart(); break;
		case MSG_OTTD_SAVETHREAD_DONE:  SaveFileDone(); break;
		case MSG_OTTD_SAVETHREAD_ERROR: SaveFileError(); break;
		default: NOT_REACHED();
	}

	OTTD_ReleaseMutex(); // release mutex so that other threads, messages can be handled
}

static void ShowScreenshotResult(bool b)
{
	if (b) {
		SetDParamStr(0, _screenshot_name);
		ShowErrorMessage(INVALID_STRING_ID, STR_031B_SCREENSHOT_SUCCESSFULLY, 0, 0);
	} else {
		ShowErrorMessage(INVALID_STRING_ID, STR_031C_SCREENSHOT_FAILED, 0, 0);
	}

}

static void MakeNewGame(void)
{
	_game_mode = GM_NORMAL;

	// Copy in game options
	_opt_ptr = &_opt;
	memcpy(_opt_ptr, &_opt_newgame, sizeof(*_opt_ptr));

	GfxLoadSprites();

	// Reinitialize windows
	ResetWindowSystem();
	LoadStringWidthTable();

	SetupColorsAndInitialWindow();

	// Randomize world
	GenerateWorld(0, 1<<_patches.map_x, 1<<_patches.map_y);

	// In a dedicated server, the server does not play
	if (_network_dedicated) {
		_local_player = OWNER_SPECTATOR;
	} else {
		// Create a single player
		DoStartupNewPlayer(false);

		_local_player = 0;
		_current_player = _local_player;
		DoCommandP(0, (_patches.autorenew << 15 ) | (_patches.autorenew_months << 16) | 4, _patches.autorenew_money, NULL, CMD_REPLACE_VEHICLE);
	}

	MarkWholeScreenDirty();
}

static void MakeNewEditorWorld(void)
{
	_game_mode = GM_EDITOR;

	// Copy in game options
	_opt_ptr = &_opt;
	memcpy(_opt_ptr, &_opt_newgame, sizeof(GameOptions));

	GfxLoadSprites();

	// Re-init the windowing system
	ResetWindowSystem();

	// Create toolbars
	SetupColorsAndInitialWindow();

	// Startup the game system
	GenerateWorld(1, 1 << _patches.map_x, 1 << _patches.map_y);

	_local_player = OWNER_NONE;
	MarkWholeScreenDirty();
}

void StartupPlayers(void);
void StartupDisasters(void);

/**
 * Start Scenario starts a new game based on a scenario.
 * Eg 'New Game' --> select a preset scenario
 * This starts a scenario based on your current difficulty settings
 */
static void StartScenario(void)
{
	_game_mode = GM_NORMAL;

	// invalid type
	if (_file_to_saveload.mode == SL_INVALID) {
		printf("Savegame is obsolete or invalid format: %s\n", _file_to_saveload.name);
		ShowErrorMessage(INVALID_STRING_ID, STR_4009_GAME_LOAD_FAILED, 0, 0);
		_game_mode = GM_MENU;
		return;
	}

	GfxLoadSprites();

	// Reinitialize windows
	ResetWindowSystem();
	LoadStringWidthTable();

	SetupColorsAndInitialWindow();

	// Load game
	if (SaveOrLoad(_file_to_saveload.name, _file_to_saveload.mode) != SL_OK) {
		LoadIntroGame();
		ShowErrorMessage(INVALID_STRING_ID, STR_4009_GAME_LOAD_FAILED, 0, 0);
	}

	_opt_ptr = &_opt;
	memcpy(&_opt_ptr->diff, &_opt_newgame.diff, sizeof(GameDifficulty));
	_opt.diff_level = _opt_newgame.diff_level;

	// Inititalize data
	StartupPlayers();
	StartupEngines();
	StartupDisasters();

	_local_player = 0;
	_current_player = _local_player;
	DoCommandP(0, (_patches.autorenew << 15 ) | (_patches.autorenew_months << 16) | 4, _patches.autorenew_money, NULL, CMD_REPLACE_VEHICLE);

	MarkWholeScreenDirty();
}

bool SafeSaveOrLoad(const char *filename, int mode, int newgm)
{
	byte ogm = _game_mode;
	int r;

	_game_mode = newgm;
	r = SaveOrLoad(filename, mode);
	if (r == SL_REINIT) {
		switch (ogm) {
			case GM_MENU:   LoadIntroGame();      break;
			case GM_EDITOR: MakeNewEditorWorld(); break;
			default:        MakeNewGame();        break;
		}
		return false;
	} else if (r != SL_OK) {
		_game_mode = ogm;
		return false;
	} else {
		return true;
	}
}

void SwitchMode(int new_mode)
{
#ifdef ENABLE_NETWORK
	// If we are saving something, the network stays in his current state
	if (new_mode != SM_SAVE) {
		// If the network is active, make it not-active
		if (_networking) {
			if (_network_server && (new_mode == SM_LOAD || new_mode == SM_NEWGAME)) {
				NetworkReboot();
				NetworkUDPClose();
			} else {
				NetworkDisconnect();
				NetworkUDPClose();
			}
		}

		// If we are a server, we restart the server
		if (_is_network_server) {
			// But not if we are going to the menu
			if (new_mode != SM_MENU) {
				NetworkServerStart();
			} else {
				// This client no longer wants to be a network-server
				_is_network_server = false;
			}
		}
	}
#endif /* ENABLE_NETWORK */

	switch (new_mode) {
	case SM_EDITOR: /* Switch to scenario editor */
		MakeNewEditorWorld();
		break;

	case SM_NEWGAME: /* New Game --> 'Random game' */
#ifdef ENABLE_NETWORK
		if (_network_server)
			snprintf(_network_game_info.map_name, NETWORK_NAME_LENGTH, "Random Map");
#endif /* ENABLE_NETWORK */
		MakeNewGame();
		break;

	case SM_START_SCENARIO: /* New Game --> Choose one of the preset scenarios */
		#ifdef ENABLE_NETWORK
			if (_network_server)
				snprintf(_network_game_info.map_name, NETWORK_NAME_LENGTH, "%s (Loaded scenario)", _file_to_saveload.title);
		#endif /* ENABLE_NETWORK */
		StartScenario();
		break;

	case SM_LOAD: { /* Load game, Play Scenario */
		_opt_ptr = &_opt;

		if (!SafeSaveOrLoad(_file_to_saveload.name, _file_to_saveload.mode, GM_NORMAL)) {
			LoadIntroGame();
			ShowErrorMessage(INVALID_STRING_ID, STR_4009_GAME_LOAD_FAILED, 0, 0);
		} else {
			_local_player = 0;
			DoCommandP(0, 0, 0, NULL, CMD_PAUSE); // decrease pause counter (was increased from opening load dialog)
#ifdef ENABLE_NETWORK
			if (_network_server)
				snprintf(_network_game_info.map_name, NETWORK_NAME_LENGTH, "%s (Loaded game)", _file_to_saveload.title);
#endif /* ENABLE_NETWORK */
		}
		break;
	}

	case SM_LOAD_SCENARIO: { /* Load scenario from scenario editor */
		if (SafeSaveOrLoad(_file_to_saveload.name, _file_to_saveload.mode, GM_EDITOR)) {
			PlayerID i;

			_opt_ptr = &_opt;

			_local_player = OWNER_NONE;
			_generating_world = true;
			// delete all players.
			for (i = 0; i != MAX_PLAYERS; i++) {
				ChangeOwnershipOfPlayerItems(i, OWNER_SPECTATOR);
				_players[i].is_active = false;
			}
			_generating_world = false;
			// delete all stations owned by a player
			DeleteAllPlayerStations();
		} else {
			ShowErrorMessage(INVALID_STRING_ID, STR_4009_GAME_LOAD_FAILED, 0, 0);
		}
		break;
	}


	case SM_MENU: /* Switch to game intro menu */
		LoadIntroGame();
		break;

	case SM_SAVE: /* Save game */
		if (SaveOrLoad(_file_to_saveload.name, SL_SAVE) != SL_OK) {
			ShowErrorMessage(INVALID_STRING_ID, STR_4007_GAME_SAVE_FAILED, 0, 0);
		} else {
			DeleteWindowById(WC_SAVELOAD, 0);
		}
		break;

	case SM_GENRANDLAND: /* Generate random land within scenario editor */
		GenerateWorld(2, 1<<_patches.map_x, 1<<_patches.map_y);
		// XXX: set date
		_local_player = OWNER_NONE;
		MarkWholeScreenDirty();
		break;
	}

	if (_switch_mode_errorstr != INVALID_STRING_ID)
		ShowErrorMessage(INVALID_STRING_ID,_switch_mode_errorstr,0,0);
}


// State controlling game loop.
// The state must not be changed from anywhere
// but here.
// That check is enforced in DoCommand.
void StateGameLoop(void)
{
	// dont execute the state loop during pause
	if (_pause) return;

	// _frame_counter is increased somewhere else when in network-mode
	//  Sidenote: _frame_counter is ONLY used for _savedump in non-MP-games
	//    Should that not be deleted? If so, the next 2 lines can also be deleted
	if (!_networking) _frame_counter++;

	if (_savedump_path[0] && (uint)_frame_counter >= _savedump_first && (uint)(_frame_counter -_savedump_first) % _savedump_freq == 0 ) {
		char buf[100];
		sprintf(buf, "%s%.5d.sav", _savedump_path, _frame_counter);
		SaveOrLoad(buf, SL_SAVE);
		if ((uint)_frame_counter >= _savedump_last) exit(1);
	}

	if (_game_mode == GM_EDITOR) {
		RunTileLoop();
		CallVehicleTicks();
		CallLandscapeTick();
		CallWindowTickEvent();
		NewsLoop();
	} else {
		// All these actions has to be done from OWNER_NONE
		//  for multiplayer compatibility
		PlayerID p = _current_player;
		_current_player = OWNER_NONE;

		AnimateAnimatedTiles();
		IncreaseDate();
		RunTileLoop();
		CallVehicleTicks();
		CallLandscapeTick();

		AI_RunGameLoop();

		CallWindowTickEvent();
		NewsLoop();
		_current_player = p;
	}
}

static void DoAutosave(void)
{
	char buf[200];

	if (_patches.keep_all_autosave && _local_player != OWNER_SPECTATOR) {
		const Player *p = GetPlayer(_local_player);
		char *s;
		sprintf(buf, "%s%s", _path.autosave_dir, PATHSEP);

		SetDParam(0, p->name_1);
		SetDParam(1, p->name_2);
		SetDParam(2, _date);
		s = GetString(buf + strlen(_path.autosave_dir) + strlen(PATHSEP), STR_4004);
		strcpy(s, ".sav");
	} else { /* generate a savegame name and number according to _patches.max_num_autosaves */
		sprintf(buf, "%s%sautosave%d.sav", _path.autosave_dir, PATHSEP, _autosave_ctr);

		_autosave_ctr++;
		if (_autosave_ctr >= _patches.max_num_autosaves) {
			// we reached the limit for numbers of autosaves. We will start over
			_autosave_ctr = 0;
		}
	}

	DEBUG(misc, 2) ("Autosaving to %s", buf);
	if (SaveOrLoad(buf, SL_SAVE) != SL_OK)
		ShowErrorMessage(INVALID_STRING_ID, STR_AUTOSAVE_FAILED, 0, 0);
}

static void ScrollMainViewport(int x, int y)
{
	if (_game_mode != GM_MENU) {
		Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
		assert(w);

		WP(w,vp_d).scrollpos_x += x << w->viewport->zoom;
		WP(w,vp_d).scrollpos_y += y << w->viewport->zoom;
	}
}

static const int8 scrollamt[16][2] = {
	{ 0, 0},
	{-2, 0}, // 1:left
	{ 0,-2}, // 2:up
	{-2,-1}, // 3:left + up
	{ 2, 0}, // 4:right
	{ 0, 0}, // 5:left + right
	{ 2,-1}, // 6:right + up
	{ 0,-2}, // 7:left + right + up = up
	{ 0 ,2}, // 8:down
	{-2 ,1}, // 9:down+left
	{ 0, 0}, // 10:impossible
	{-2, 0}, // 11:left + up + down = left
	{ 2, 1}, // 12:down+right
	{ 0, 2}, // 13:left + right + down = down
	{ 0,-2}, // 14:left + right + up = up
	{ 0, 0}, // 15:impossible
};

static void HandleKeyScrolling(void)
{
	if (_dirkeys && !_no_scroll) {
		int factor = _shift_pressed ? 50 : 10;
		ScrollMainViewport(scrollamt[_dirkeys][0] * factor, scrollamt[_dirkeys][1] * factor);
	}
}

void GameLoop(void)
{
	int m;
	ThreadMsg message;

	if ((message = OTTD_PollThreadEvent()) != 0) ProcessSentMessage(message);

	// autosave game?
	if (_do_autosave) {
		_do_autosave = false;
		DoAutosave();
		RedrawAutosave();
	}

	// handle scrolling of the main window
	if (_dirkeys) HandleKeyScrolling();

	// make a screenshot?
	if ((m=_make_screenshot) != 0) {
		_make_screenshot = 0;
		switch(m) {
		case 1: // make small screenshot
			UndrawMouseCursor();
			ShowScreenshotResult(MakeScreenshot());
			break;
		case 2: // make large screenshot
			ShowScreenshotResult(MakeWorldScreenshot(-(int)MapMaxX() * TILE_PIXELS, 0, (MapMaxX() + MapMaxY()) * TILE_PIXELS, (MapMaxX() + MapMaxY()) * TILE_PIXELS >> 1, 0));
			break;
		}
	}

	// switch game mode?
	if ((m=_switch_mode) != SM_NONE) {
		_switch_mode = SM_NONE;
		SwitchMode(m);
	}

	IncreaseSpriteLRU();
	InteractiveRandom();

	if (_scroller_click_timeout > 3) {
		_scroller_click_timeout -= 3;
	} else {
		_scroller_click_timeout = 0;
	}

	_caret_timer += 3;
	_timer_counter += 8;
	CursorTick();

#ifdef ENABLE_NETWORK
	// Check for UDP stuff
	NetworkUDPGameLoop();

	if (_networking) {
		// Multiplayer
		NetworkGameLoop();
	} else {
		if (_network_reconnect > 0 && --_network_reconnect == 0) {
			// This means that we want to reconnect to the last host
			// We do this here, because it means that the network is really closed
			NetworkClientConnectGame(_network_last_host, _network_last_port);
		}
		// Singleplayer
		StateGameLoop();
	}
#else
	StateGameLoop();
#endif /* ENABLE_NETWORK */

	if (!_pause && _display_opt & DO_FULL_ANIMATION) DoPaletteAnimations();

	if (!_pause || _cheats.build_in_pause.value) MoveAllTextEffects();

	InputLoop();

	MusicLoop();
}

void BeforeSaveGame(void)
{
	const Window* w = FindWindowById(WC_MAIN_WINDOW, 0);

	if (w != NULL) {
		_saved_scrollpos_x = WP(w, const vp_d).scrollpos_x;
		_saved_scrollpos_y = WP(w, const vp_d).scrollpos_y;
		_saved_scrollpos_zoom = w->viewport->zoom;
	}
}

static void ConvertTownOwner(void)
{
	TileIndex tile;

	for (tile = 0; tile != MapSize(); tile++) {
		if (IsTileType(tile, MP_STREET)) {
			if (IsLevelCrossing(tile) && _m[tile].m3 & 0x80) _m[tile].m3 = OWNER_TOWN;

			if (_m[tile].m1 & 0x80) SetTileOwner(tile, OWNER_TOWN);
		} else if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			if (_m[tile].m1 & 0x80) SetTileOwner(tile, OWNER_TOWN);
		}
	}
}

// before savegame version 4, the name of the company determined if it existed
static void CheckIsPlayerActive(void)
{
	Player* p;

	FOR_ALL_PLAYERS(p) {
		if (p->name_1 != 0) p->is_active = true;
	}
}

// since savegame version 4.1, exclusive transport rights are stored at towns
static void UpdateExclusiveRights(void)
{
	Town* t;

	FOR_ALL_TOWNS(t) {
		if (t->xy != 0) t->exclusivity = (byte)-1;
	}

	/* FIXME old exclusive rights status is not being imported (stored in s->blocked_months_obsolete)
			could be implemented this way:
			1.) Go through all stations
					Build an array town_blocked[ town_id ][ player_id ]
				 that stores if at least one station in that town is blocked for a player
			2.) Go through that array, if you find a town that is not blocked for
					one player, but for all others, then give him exclusivity.
	*/
}

static const byte convert_currency[] = {
	 0,  1, 12,  8,  3,
	10, 14, 19,  4,  5,
	 9, 11, 13,  6, 17,
	16, 22, 21,  7, 15,
	18,  2, 20, };

// since savegame version 4.2 the currencies are arranged differently
static void UpdateCurrencies(void)
{
	_opt.currency = convert_currency[_opt.currency];
}

/* Up to revision 1413 the invisible tiles at the southern border have not been
 * MP_VOID, even though they should have. This is fixed by this function
 */
static void UpdateVoidTiles(void)
{
	uint i;

	for (i = 0; i < MapMaxY(); ++i)
		SetTileType(i * MapSizeX() + MapMaxX(), MP_VOID);
	for (i = 0; i < MapSizeX(); ++i)
		SetTileType(MapSizeX() * MapMaxY() + i, MP_VOID);
}

// since savegame version 6.0 each sign has an "owner", signs without owner (from old games are set to 255)
static void UpdateSignOwner(void)
{
	SignStruct *ss;

	FOR_ALL_SIGNS(ss) ss->owner = OWNER_NONE;
}

extern void UpdateOldAircraft( void );
extern void UpdateOilRig( void );

bool AfterLoadGame(uint version)
{
	Window *w;
	ViewPort *vp;
	Player *p;

	// in version 2.1 of the savegame, town owner was unified.
	if (CheckSavegameVersionOldStyle(2, 1)) ConvertTownOwner();

	// from version 4.1 of the savegame, exclusive rights are stored at towns
	if (CheckSavegameVersionOldStyle(4, 1)) UpdateExclusiveRights();

	// from version 4.2 of the savegame, currencies are in a different order
	if (CheckSavegameVersionOldStyle(4, 2)) UpdateCurrencies();

	// from version 6.1 of the savegame, signs have an "owner"
	if (CheckSavegameVersionOldStyle(6, 1)) UpdateSignOwner();

	/* In old version there seems to be a problem that water is owned by
	    OWNER_NONE, not OWNER_WATER.. I can't replicate it for the current
	    (4.3) version, so I just check when versions are older, and then
	    walk through the whole map.. */
	if (CheckSavegameVersionOldStyle(4, 3)) {
		TileIndex tile = TileXY(0, 0);
		uint w = MapSizeX();
		uint h = MapSizeY();

		BEGIN_TILE_LOOP(tile_cur, w, h, tile)
			if (IsTileType(tile_cur, MP_WATER) && GetTileOwner(tile_cur) >= MAX_PLAYERS)
				SetTileOwner(tile_cur, OWNER_WATER);
		END_TILE_LOOP(tile_cur, w, h, tile)
	}

	// convert road side to my format.
	if (_opt.road_side) _opt.road_side = 1;

	// Load the sprites
	GfxLoadSprites();

	// Update current year
	SetDate(_date);

	// reinit the landscape variables (landscape might have changed)
	InitializeLandscapeVariables(true);

	// Update all vehicles
	AfterLoadVehicles();

	// Update all waypoints
	if (CheckSavegameVersion(12)) FixOldWaypoints();

	UpdateAllWaypointSigns();

	// in version 2.2 of the savegame, we have new airports
	if (CheckSavegameVersionOldStyle(2, 2)) UpdateOldAircraft();

	UpdateAllStationVirtCoord();

	// Setup town coords
	AfterLoadTown();
	UpdateAllSignVirtCoords();

	// make sure there is a town in the game
	if (_game_mode == GM_NORMAL && !ClosestTownFromTile(0, (uint)-1)) {
		_error_message = STR_NO_TOWN_IN_SCENARIO;
		return false;
	}

	// Initialize windows
	ResetWindowSystem();
	SetupColorsAndInitialWindow();

	w = FindWindowById(WC_MAIN_WINDOW, 0);

	WP(w,vp_d).scrollpos_x = _saved_scrollpos_x;
	WP(w,vp_d).scrollpos_y = _saved_scrollpos_y;

	vp = w->viewport;
	vp->zoom = _saved_scrollpos_zoom;
	vp->virtual_width = vp->width << vp->zoom;
	vp->virtual_height = vp->height << vp->zoom;

	// in version 4.1 of the savegame, is_active was introduced to determine
	// if a player does exist, rather then checking name_1
	if (CheckSavegameVersionOldStyle(4, 1)) CheckIsPlayerActive();

	// the void tiles on the southern border used to belong to a wrong class (pre 4.3).
	if (CheckSavegameVersionOldStyle(4, 3)) UpdateVoidTiles();

	// If Load Scenario / New (Scenario) Game is used,
	//  a player does not exist yet. So create one here.
	// 1 exeption: network-games. Those can have 0 players
	//   But this exeption is not true for network_servers!
	if (!_players[0].is_active && (!_networking || (_networking && _network_server)))
		DoStartupNewPlayer(false);

	DoZoomInOutWindow(ZOOM_NONE, w); // update button status
	MarkWholeScreenDirty();

	// In 5.1, Oilrigs have been moved (again)
	if (CheckSavegameVersionOldStyle(5, 1)) UpdateOilRig();

	/* In version 6.1 we put the town index in the map-array. To do this, we need
	 *  to use m2 (16bit big), so we need to clean m2, and that is where this is
	 *  all about ;) */
	if (CheckSavegameVersionOldStyle(6, 1)) {
		BEGIN_TILE_LOOP(tile, MapSizeX(), MapSizeY(), 0) {
			if (IsTileType(tile, MP_HOUSE)) {
				_m[tile].m4 = _m[tile].m2;
				//XXX magic
				SetTileType(tile, MP_VOID);
				_m[tile].m2 = ClosestTownFromTile(tile,(uint)-1)->index;
				SetTileType(tile, MP_HOUSE);
			} else if (IsTileType(tile, MP_STREET)) {
				//XXX magic
				_m[tile].m4 |= (_m[tile].m2 << 4);
				if (IsTileOwner(tile, OWNER_TOWN)) {
					SetTileType(tile, MP_VOID);
					_m[tile].m2 = ClosestTownFromTile(tile,(uint)-1)->index;
					SetTileType(tile, MP_STREET);
				} else {
					_m[tile].m2 = 0;
				}
			}
		} END_TILE_LOOP(tile, MapSizeX(), MapSizeY(), 0);
	}

	/* From version 9.0, we update the max passengers of a town (was sometimes negative
	 *  before that. */
	if (CheckSavegameVersion(9)) {
		Town *t;
		FOR_ALL_TOWNS(t) UpdateTownMaxPass(t);
	}

	/* From version 15.0, we moved a semaphore bit from bit 2 to bit 3 in m4, making
	 *  room for PBS. While doing that, clean some blocks that should be empty, for PBS. */
	if (CheckSavegameVersion(15)) {
		BEGIN_TILE_LOOP(tile, MapSizeX(), MapSizeY(), 0) {
			if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile) && HASBIT(_m[tile].m4, 2)) {
				CLRBIT(_m[tile].m4, 2);
				SETBIT(_m[tile].m4, 3);
			}
			// Clear possible junk data in PBS bits.
			if (IsTileType(tile, MP_RAILWAY) && !HASBIT(_m[tile].m5, 7))
				SB(_m[tile].m4, 4, 4, 0);
		} END_TILE_LOOP(tile, MapSizeX(), MapSizeY(), 0);
	}

	/* From version 16.0, we included autorenew on engines, which are now saved, but
	 *  of course, we do need to initialize them for older savegames. */
	if (CheckSavegameVersion(16)) {
		FOR_ALL_PLAYERS(p) {
			InitialiseEngineReplacement(p);
			p->engine_renew = false;
			p->engine_renew_months = -6;
			p->engine_renew_money = 100000;
		}
		if (_local_player < MAX_PLAYERS) {
			// Set the human controlled player to the patch settings
			// Scenario editor do not have any companies
			p = GetPlayer(_local_player);
			p->engine_renew = _patches.autorenew;
			p->engine_renew_months = _patches.autorenew_months;
			p->engine_renew_money = _patches.autorenew_money;
		}
	}

	/* In version 16.1 of the savegame, trains became aware of station lengths
		need to initialized to the invalid state
		players needs to set renew_keep_length too */
	if (CheckSavegameVersionOldStyle(16, 1)) {
		Vehicle *v;
		FOR_ALL_PLAYERS(p) {
			p->renew_keep_length = false;
		}

		FOR_ALL_VEHICLES(v) {
			if (v->type == VEH_Train) {
				v->u.rail.shortest_platform[0] = 255;
				v->u.rail.shortest_platform[1] = 0;
			}
		}
	}

	/* In version 17, ground type is moved from m2 to m4 for depots and
	 * waypoints to make way for storing the index in m2. The custom graphics
	 * id which was stored in m4 is now saved as a grf/id reference in the
	 * waypoint struct. */
	if (CheckSavegameVersion(17)) {
		Waypoint *wp;

		FOR_ALL_WAYPOINTS(wp) {
			if (wp->xy != 0 && wp->deleted == 0) {
				const StationSpec *spec = NULL;

				if (HASBIT(_m[wp->xy].m3, 4))
					spec = GetCustomStation(STAT_CLASS_WAYP, _m[wp->xy].m4 + 1);

				if (spec != NULL) {
					wp->stat_id = _m[wp->xy].m4 + 1;
					wp->grfid = spec->grfid;
					wp->localidx = spec->localidx;
				} else {
					// No custom graphics set, so set to default.
					wp->stat_id = 0;
					wp->grfid = 0;
					wp->localidx = 0;
				}

				// Move ground type bits from m2 to m4.
				_m[wp->xy].m4 = GB(_m[wp->xy].m2, 0, 4);
				// Store waypoint index in the tile.
				_m[wp->xy].m2 = wp->index;
			}
		}
	} else {
		/* As of version 17, we recalculate the custom graphic ID of waypoints
		 * from the GRF ID / station index. */
		UpdateAllWaypointCustomGraphics();
	}

	FOR_ALL_PLAYERS(p) p->avail_railtypes = GetPlayerRailtypes(p->index);

	return true;
}
