/* $Id: gui.h 3338 2005-12-24 20:53:02Z tron $ */

#ifndef GUI_H
#define GUI_H

#if 0

#include "station.h"
#include "window.h"

/* main_gui.c */
void SetupColorsAndInitialWindow(void);
void CcPlaySound10(bool success, TileIndex tile, uint32 p1, uint32 p2);
void CcBuildCanal(bool success, TileIndex tile, uint32 p1, uint32 p2);
void CcTerraform(bool success, TileIndex tile, uint32 p1, uint32 p2);

/* settings_gui.c */
void ShowGameOptions(void);
void ShowGameDifficulty(void);
void ShowPatchesSelection(void);
void ShowNewgrf(void);
void ShowCustCurrency(void);

/* graph_gui.c */
void ShowOperatingProfitGraph(void);
void ShowIncomeGraph(void);
void ShowDeliveredCargoGraph(void);
void ShowPerformanceHistoryGraph(void);
void ShowCompanyValueGraph(void);
void ShowCargoPaymentRates(void);
void ShowCompanyLeagueTable(void);
void ShowPerformanceRatingDetail(void);

/* news_gui.c */
void ShowLastNewsMessage(void);
void ShowMessageOptions(void);
void ShowMessageHistory(void);

/* traintoolb_gui.c */
void ShowBuildRailToolbar(RailType railtype, int button);
void PlaceProc_BuyLand(TileIndex tile);

/* train_gui.c */
void ShowPlayerTrains(PlayerID player, StationID station);
void ShowTrainViewWindow(const Vehicle *v);
void ShowOrdersWindow(const Vehicle* v);

void ShowRoadVehViewWindow(const Vehicle* v);

/* road_gui.c */
void ShowBuildRoadToolbar(void);
void ShowBuildRoadScenToolbar(void);
void ShowPlayerRoadVehicles(PlayerID player, StationID station);

/* dock_gui.c */
void ShowBuildDocksToolbar(void);
void ShowPlayerShips(PlayerID player, StationID station);

void ShowShipViewWindow(const Vehicle* v);

/* aircraft_gui.c */
void ShowBuildAirToolbar(void);
void ShowPlayerAircraft(PlayerID player, StationID station);

/* terraform_gui.c */
void ShowTerraformToolbar(void);

void PlaceProc_DemolishArea(TileIndex tile);
void PlaceProc_LowerLand(TileIndex tile);
void PlaceProc_RaiseLand(TileIndex tile);
void PlaceProc_LevelLand(TileIndex tile);
bool GUIPlaceProcDragXY(const WindowEvent *we);

enum { // max 32 - 4 = 28 types
	GUI_PlaceProc_DemolishArea    = 0 << 4,
	GUI_PlaceProc_LevelArea       = 1 << 4,
	GUI_PlaceProc_DesertArea      = 2 << 4,
	GUI_PlaceProc_WaterArea       = 3 << 4,
	GUI_PlaceProc_ConvertRailArea = 4 << 4,
	GUI_PlaceProc_RockyArea       = 5 << 4,
};

/* misc_gui.c */
void PlaceLandBlockInfo(void);
void ShowAboutWindow(void);
void ShowBuildTreesToolbar(void);
void ShowBuildTreesScenToolbar(void);
void ShowTownDirectory(void);
void ShowIndustryDirectory(void);
void ShowSubsidiesList(void);
void ShowPlayerStations(PlayerID player);
void ShowPlayerFinances(PlayerID player);
void ShowPlayerCompany(PlayerID player);
void ShowSignList(void);

void ShowEstimatedCostOrIncome(int32 cost, int x, int y);
void ShowErrorMessage(StringID msg_1, StringID msg_2, int x, int y);

void DrawStationCoverageAreaText(int sx, int sy, uint mask,int rad);
void CheckRedrawStationCoverage(const Window* w);

void ShowSmallMap(void);
void ShowExtraViewPortWindow(void);
void SetVScrollCount(Window *w, int num);
void SetVScroll2Count(Window *w, int num);
void SetHScrollCount(Window *w, int num);

void ShowCheatWindow(void);
void AskForNewGameToStart(void);

void DrawEditBox(Window *w, int wid);
void HandleEditBox(Window *w, int wid);
int HandleEditBoxKey(Window *w, int wid, WindowEvent *we);
bool HandleCaret(Textbuf *tb);

void DeleteTextBufferAll(Textbuf *tb);
bool DeleteTextBufferChar(Textbuf *tb, int delmode);
bool InsertTextBufferChar(Textbuf *tb, byte key);
bool InsertTextBufferClipboard(Textbuf *tb);
bool MoveTextBufferPos(Textbuf *tb, int navmode);
void UpdateTextBufferSize(Textbuf *tb);

void BuildFileList(void);
void SetFiosType(const byte fiostype);

/*	FIOS_TYPE_FILE, FIOS_TYPE_OLDFILE etc. different colours */
extern const byte _fios_colors[];

/* network gui */
void ShowNetworkGameWindow(void);
void ShowChatWindow(StringID str, StringID caption, int maxlen, int maxwidth, WindowClass window_class, WindowNumber window_number);

/* bridge_gui.c */
void ShowBuildBridgeWindow(uint start, uint end, byte type);

enum {
	ZOOM_IN = 0,
	ZOOM_OUT = 1,
	ZOOM_NONE = 2, // hack, used to update the button status
};

bool DoZoomInOutWindow(int how, Window * w);
void ShowBuildIndustryWindow(void);
void ShowQueryString(StringID str, StringID caption, uint maxlen, uint maxwidth, WindowClass window_class, WindowNumber window_number);
void ShowMusicWindow(void);

/* main_gui.c */
VARDEF byte _station_show_coverage;
VARDEF PlaceProc *_place_proc;

/* vehicle_gui.c */
void InitializeGUI(void);

/* m_airport.c */
void MA_AskAddAirport(void);
#endif
#endif /* GUI_H */
