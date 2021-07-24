/* $Id: rail.c 3077 2005-10-22 06:39:32Z tron $ */

#include "stdafx.h"
#include "openttd.h"
#include "rail.h"
#include "station.h"

/* XXX: Below 3 tables store duplicate data. Maybe remove some? */
/* Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction along with the trackdir */
const byte _signal_along_trackdir[] = {
	0x80, 0x80, 0x80, 0x20, 0x40, 0x10, 0, 0,
	0x40, 0x40, 0x40, 0x10, 0x80, 0x20
};

/* Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction against the trackdir */
const byte _signal_against_trackdir[] = {
	0x40, 0x40, 0x40, 0x10, 0x80, 0x20, 0, 0,
	0x80, 0x80, 0x80, 0x20, 0x40, 0x10
};

/* Maps a Track to the bits that store the status of the two signals that can
 * be present on the given track */
const byte _signal_on_track[] = {
	0xC0, 0xC0, 0xC0, 0x30, 0xC0, 0x30
};

/* Maps a diagonal direction to the all trackdirs that are connected to any
 * track entering in this direction (including those making 90 degree turns)
 */
const TrackdirBits _exitdir_reaches_trackdirs[] = {
	TRACKDIR_BIT_DIAG1_NE | TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_LEFT_N,  /* DIAGDIR_NE */
	TRACKDIR_BIT_DIAG2_SE | TRACKDIR_BIT_LEFT_S  | TRACKDIR_BIT_UPPER_E, /* DIAGDIR_SE */
	TRACKDIR_BIT_DIAG1_SW | TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_RIGHT_S, /* DIAGDIR_SW */
	TRACKDIR_BIT_DIAG2_NW | TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_LOWER_W  /* DIAGDIR_NW */
};

const Trackdir _next_trackdir[] = {
	TRACKDIR_DIAG1_NE,  TRACKDIR_DIAG2_SE,  TRACKDIR_LOWER_E, TRACKDIR_UPPER_E, TRACKDIR_RIGHT_S, TRACKDIR_LEFT_S, INVALID_TRACKDIR, INVALID_TRACKDIR,
	TRACKDIR_DIAG1_SW,  TRACKDIR_DIAG2_NW,  TRACKDIR_LOWER_W, TRACKDIR_UPPER_W, TRACKDIR_RIGHT_N, TRACKDIR_LEFT_N
};

/* Maps a trackdir to all trackdirs that make 90 deg turns with it. */
const TrackdirBits _track_crosses_trackdirs[] = {
	TRACKDIR_BIT_DIAG2_SE | TRACKDIR_BIT_DIAG2_NW,                                               /* TRACK_DIAG1 */
	TRACKDIR_BIT_DIAG1_NE | TRACKDIR_BIT_DIAG1_SW,                                               /* TRACK_DIAG2 */
	TRACKDIR_BIT_RIGHT_N  | TRACKDIR_BIT_RIGHT_S  | TRACKDIR_BIT_LEFT_N  | TRACKDIR_BIT_LEFT_S,  /* TRACK_UPPER */
	TRACKDIR_BIT_RIGHT_N  | TRACKDIR_BIT_RIGHT_S  | TRACKDIR_BIT_LEFT_N  | TRACKDIR_BIT_LEFT_S,  /* TRACK_LOWER */
	TRACKDIR_BIT_UPPER_W  | TRACKDIR_BIT_UPPER_E  | TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LOWER_E, /* TRACK_LEFT  */
	TRACKDIR_BIT_UPPER_W  | TRACKDIR_BIT_UPPER_E  | TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LOWER_E  /* TRACK_RIGHT */
};

/* Maps a track to all tracks that make 90 deg turns with it. */
const TrackBits _track_crosses_tracks[] = {
	TRACK_BIT_DIAG2,                   /* TRACK_DIAG1 */
	TRACK_BIT_DIAG1,                   /* TRACK_DIAG2 */
	TRACK_BIT_LEFT  | TRACK_BIT_RIGHT, /* TRACK_UPPER */
	TRACK_BIT_LEFT  | TRACK_BIT_RIGHT, /* TRACK_LOWER */
	TRACK_BIT_UPPER | TRACK_BIT_LOWER, /* TRACK_LEFT  */
	TRACK_BIT_UPPER | TRACK_BIT_LOWER  /* TRACK_RIGHT */
};

/* Maps a trackdir to the (4-way) direction the tile is exited when following
 * that trackdir * /
const DiagDirection _trackdir_to_exitdir[] = {
	DIAGDIR_NE,DIAGDIR_SE,DIAGDIR_NE,DIAGDIR_SE,DIAGDIR_SW,DIAGDIR_SE, DIAGDIR_NE,DIAGDIR_NE,
	DIAGDIR_SW,DIAGDIR_NW,DIAGDIR_NW,DIAGDIR_SW,DIAGDIR_NW,DIAGDIR_NE,
}; */

const Trackdir _track_exitdir_to_trackdir[][DIAGDIR_END] = {
	{TRACKDIR_DIAG1_NE, INVALID_TRACKDIR,  TRACKDIR_DIAG1_SW, INVALID_TRACKDIR},
	{INVALID_TRACKDIR,  TRACKDIR_DIAG2_SE, INVALID_TRACKDIR,  TRACKDIR_DIAG2_NW},
	{TRACKDIR_UPPER_E,  INVALID_TRACKDIR,  INVALID_TRACKDIR,  TRACKDIR_UPPER_W},
	{INVALID_TRACKDIR,  TRACKDIR_LOWER_E,  TRACKDIR_LOWER_W,  INVALID_TRACKDIR},
	{INVALID_TRACKDIR,  INVALID_TRACKDIR,  TRACKDIR_LEFT_S,   TRACKDIR_LEFT_N},
	{TRACKDIR_RIGHT_N,  TRACKDIR_RIGHT_S,  INVALID_TRACKDIR,  INVALID_TRACKDIR}
};

const Trackdir _track_enterdir_to_trackdir[][DIAGDIR_END] = { // TODO: replace magic with enums
	{TRACKDIR_DIAG1_NE, INVALID_TRACKDIR,  TRACKDIR_DIAG1_SW, INVALID_TRACKDIR},
	{INVALID_TRACKDIR,  TRACKDIR_DIAG2_SE, INVALID_TRACKDIR,  TRACKDIR_DIAG2_NW},
	{INVALID_TRACKDIR,  TRACKDIR_UPPER_E,  TRACKDIR_UPPER_W,  INVALID_TRACKDIR},
	{TRACKDIR_LOWER_E,  INVALID_TRACKDIR,  INVALID_TRACKDIR,  TRACKDIR_LOWER_W},
	{TRACKDIR_LEFT_N,   TRACKDIR_LEFT_S,   INVALID_TRACKDIR,  INVALID_TRACKDIR},
	{INVALID_TRACKDIR,  INVALID_TRACKDIR,  TRACKDIR_RIGHT_S,  TRACKDIR_RIGHT_N}
};

const Trackdir _track_direction_to_trackdir[][DIR_END] = {
	{INVALID_TRACKDIR, TRACKDIR_DIAG1_NE, INVALID_TRACKDIR, INVALID_TRACKDIR,  INVALID_TRACKDIR, TRACKDIR_DIAG1_SW, INVALID_TRACKDIR, INVALID_TRACKDIR},
	{INVALID_TRACKDIR, INVALID_TRACKDIR,  INVALID_TRACKDIR, TRACKDIR_DIAG2_SE, INVALID_TRACKDIR, INVALID_TRACKDIR,  INVALID_TRACKDIR, TRACKDIR_DIAG2_NW},
	{INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_UPPER_E, INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_UPPER_W, INVALID_TRACKDIR},
	{INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_LOWER_E, INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_LOWER_W, INVALID_TRACKDIR},
	{TRACKDIR_LEFT_N,  INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_LEFT_S,  INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR},
	{TRACKDIR_RIGHT_N, INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR,  TRACKDIR_RIGHT_S, INVALID_TRACKDIR,  INVALID_TRACKDIR, INVALID_TRACKDIR}
};

const Trackdir _dir_to_diag_trackdir[] = {
	TRACKDIR_DIAG1_NE, TRACKDIR_DIAG2_SE, TRACKDIR_DIAG1_SW, TRACKDIR_DIAG2_NW,
};

const DiagDirection _reverse_diagdir[] = {
	DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_SE
};

const Trackdir _reverse_trackdir[] = {
	TRACKDIR_DIAG1_SW, TRACKDIR_DIAG2_NW, TRACKDIR_UPPER_W, TRACKDIR_LOWER_W, TRACKDIR_LEFT_N, TRACKDIR_RIGHT_N, INVALID_TRACKDIR, INVALID_TRACKDIR,
	TRACKDIR_DIAG1_NE, TRACKDIR_DIAG2_SE, TRACKDIR_UPPER_E, TRACKDIR_LOWER_E, TRACKDIR_LEFT_S, TRACKDIR_RIGHT_S
};

RailType GetTileRailType(TileIndex tile, Trackdir trackdir)
{
	RailType type = INVALID_RAILTYPE;
	DiagDirection exitdir = TrackdirToExitdir(trackdir);
	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			/* railway track */
			type = _m[tile].m3 & RAILTYPE_MASK;
			break;
		case MP_STREET:
			/* rail/road crossing */
			if (IsLevelCrossing(tile))
				type = _m[tile].m4 & RAILTYPE_MASK;
			break;
		case MP_STATION:
			if (IsTrainStationTile(tile))
				type = _m[tile].m3 & RAILTYPE_MASK;
			break;
		case MP_TUNNELBRIDGE:
			/* railway tunnel */
			if ((_m[tile].m5 & 0xFC) == 0) type = _m[tile].m3 & RAILTYPE_MASK;
			/* railway bridge ending */
			if ((_m[tile].m5 & 0xC6) == 0x80) type = _m[tile].m3 & RAILTYPE_MASK;
			/* on railway bridge */
			if ((_m[tile].m5 & 0xC6) == 0xC0 && ((DiagDirection)(_m[tile].m5 & 0x1)) == (exitdir & 0x1))
				type = (_m[tile].m3 >> 4) & RAILTYPE_MASK;
			/* under bridge (any type) */
			if ((_m[tile].m5 & 0xC0) == 0xC0 && (_m[tile].m5 & 0x1U) != (exitdir & 0x1))
				type = _m[tile].m3 & RAILTYPE_MASK;
			break;
		default:
			break;
	}
	return type;
}
