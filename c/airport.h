/* $Id: airport.h 2701 2005-07-24 14:12:37Z tron $ */
/*
#ifndef AIRPORT_H
#define AIRPORT_H

#include "airport_movement.h"

enum {MAX_TERMINALS = 6};
enum {MAX_HELIPADS  = 2};

// Airport types
enum {
	AT_SMALL = 0,
	AT_LARGE = 1,
	AT_HELIPORT = 2,
	AT_METROPOLITAN = 3,
	AT_INTERNATIONAL = 4,
	AT_OILRIG = 15
};

// do not change unless you change v->subtype too. This aligns perfectly with its current setting
enum {
	AIRCRAFT_ONLY = 0,
	ALL = 1,
	HELICOPTERS_ONLY = 2
};
/*
// Finite sTate mAchine --> FTA
typedef struct AirportFTAClass {
	byte nofelements;							// number of positions the airport consists of
	const byte *terminals;
	const byte *helipads;
	byte entry_point;							// when an airplane arrives at this airport, enter it at position entry_point
	byte acc_planes;							// accept airplanes or helicopters or both
	const TileIndexDiffC *airport_depots;	// gives the position of the depots on the airports
	byte nof_depots;							// number of depots this airport has
	struct AirportFTA *layout;		// state machine for airport
} AirportFTAClass;

// internal structure used in openttd - Finite sTate mAchine --> FTA
typedef struct AirportFTA {
	byte position;										// the position that an airplane is at
	byte next_position;								// next position from this position
	uint32 block;	// 32 bit blocks (st->airport_flags), should be enough for the most complex airports
	byte heading;	// heading (current orders), guiding an airplane to its target on an airport
	struct AirportFTA *next_in_chain;	// possible extra movement choices from this position
} AirportFTA;

void InitializeAirports(void);
void UnInitializeAirports(void);
const AirportFTAClass* GetAirport(const byte airport_type);
*/
/** Get buildable airport bitmask.
 * @return get all buildable airports at this given time, bitmasked.
 * Bit 0 means the small airport is buildable, etc.
 * @todo set availability of airports by year, instead of airplane
 */
//uint32 GetValidAirports(void);

#endif /* AIRPORT_H */
