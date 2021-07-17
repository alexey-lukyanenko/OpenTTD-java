/* $Id: vehicle.h 3352 2005-12-28 22:29:59Z peter1138 $ */

#ifndef VEHICLE_H
#define VEHICLE_H

#include "pool.h"
#include "order.h"
#include "rail.h"
#include "queue.h"

enum {
	VEH_Train = 0x10,
	VEH_Road = 0x11,
	VEH_Ship = 0x12,
	VEH_Aircraft = 0x13,
	VEH_Special = 0x14,
	VEH_Disaster = 0x15,
} ;

enum VehStatus {
	VS_HIDDEN = 1,
	VS_STOPPED = 2,
	VS_UNCLICKABLE = 4,
	VS_DEFPAL = 0x8,
	VS_TRAIN_SLOWING = 0x10,
	VS_DISASTER = 0x20,
	VS_AIRCRAFT_BROKEN = 0x40,
	VS_CRASHED = 0x80,
};

/* Effect vehicle types */
typedef enum EffectVehicle {
	EV_CHIMNEY_SMOKE   = 0,
	EV_STEAM_SMOKE     = 1,
	EV_DIESEL_SMOKE    = 2,
	EV_ELECTRIC_SPARK  = 3,
	EV_SMOKE           = 4,
	EV_EXPLOSION_LARGE = 5,
	EV_BREAKDOWN_SMOKE = 6,
	EV_EXPLOSION_SMALL = 7,
	EV_BULLDOZER       = 8,
	EV_BUBBLE          = 9
} EffectVehicle;

/*
typedef struct VehicleRail {
	uint16 last_speed;		// NOSAVE: only used in UI
	uint16 crash_anim_pos;
	uint16 days_since_order_progr;

	// cached values, recalculated on load and each time a vehicle is added to/removed from the consist.
	uint16 cached_max_speed;  // max speed of the consist. (minimum of the max speed of all vehicles in the consist)
	uint32 cached_power;      // total power of the consist.
	uint8 cached_veh_length;  // length of this vehicle in units of 1/8 of normal length, cached because this can be set by a callback
	uint16 cached_total_length; ///< Length of the whole train, valid only for first engine.

	// cached values, recalculated when the cargo on a train changes (in addition to the conditions above)
	uint16 cached_weight;     // total weight of the consist.
	uint16 cached_veh_weight; // weight of the vehicle.
	/**
	 * Position/type of visual effect.
	 * bit 0 - 3 = position of effect relative to vehicle. (0 = front, 8 = centre, 15 = rear)
	 * bit 4 - 5 = type of effect. (0 = default for engine class, 1 = steam, 2 = diesel, 3 = electric)
	 * bit     6 = disable visual effect.
	 * bit     7 = disable powered wagons.
	 * /
	byte cached_vis_effect;

	// NOSAVE: for wagon override - id of the first engine in train
	// 0xffff == not in train
	EngineID first_engine;

	byte track;
	byte force_proceed;
	byte railtype;

	byte flags;

	byte pbs_status;
	TileIndex pbs_end_tile;
	Trackdir pbs_end_trackdir;

	/**
	  * stuff to figure out how long a train should be. Used by autoreplace
	  * first byte holds the length of the shortest station. Updated each time order 0 is reached
	  * last byte is the shortest station reached this round though the orders. It can be invalidated by
	  *   skip station and alike by setting it to 0. That way we will ensure that a complete loop is used to find the shortest station
	  * /
	byte shortest_platform[2];

	// Link between the two ends of a multiheaded engine
	Vehicle *other_multiheaded_part;
} VehicleRail;
* /

enum {
	VRF_REVERSING = 0,

	// used to calculate if train is going up or down
	VRF_GOINGUP   = 1,
	VRF_GOINGDOWN = 2,

	// used to store if a wagon is powered or not
	VRF_POWEREDWAGON = 3,
};
* /

typedef struct VehicleAir {
	uint16 crashed_counter;
	byte pos;
  byte previous_pos;
	uint16 targetairport;
	byte state;
	uint16 desired_speed;	// Speed aircraft desires to maintain, used to
							// decrease traffic to busy airports.
} VehicleAir;

typedef struct VehicleRoad {
	byte state;
	byte frame;
	uint16 unk2;
	byte overtaking;
	byte overtaking_ctr;
	uint16 crashed_ctr;
	byte reverse_ctr;
	struct RoadStop *slot;
	byte slotindex;
	byte slot_age;
} VehicleRoad;

typedef struct VehicleSpecial {
	uint16 unk0;
	byte unk2;
} VehicleSpecial;

typedef struct VehicleDisaster {
	uint16 image_override;
	uint16 unk2;
} VehicleDisaster;

typedef struct VehicleShip {
	byte state;
} VehicleShip;

/*
struct Vehicle {
	byte type;	// type, ie roadven,train,ship,aircraft,special
	byte subtype;     // subtype (Filled with values from EffectVehicles or TrainSubTypes)

	VehicleID index;	// NOSAVE: Index in vehicle array

	Vehicle *next;		// next
	Vehicle *first;   // NOSAVE: pointer to the first vehicle in the chain
	Vehicle *depot_list;	//NOSAVE: linked list to tell what vehicles entered a depot during the last tick. Used by autoreplace

	StringID string_id; // Displayed string

	UnitID unitnumber;	// unit number, for display purposes only
	PlayerID owner;				// which player owns the vehicle?

	TileIndex tile;		// Current tile index
	TileIndex dest_tile; // Heading for this tile

	int32 x_pos;			// coordinates
	int32 y_pos;
	uint32 z_pos;		// Was byte, changed for aircraft queueing
	byte direction;		// facing

	byte spritenum; // currently displayed sprite index
	                // 0xfd == custom sprite, 0xfe == custom second head sprite
	                // 0xff == reserved for another custom sprite
	uint16 cur_image; // sprite number for this vehicle
	byte sprite_width;// width of vehicle sprite
	byte sprite_height;// height of vehicle sprite
	byte z_height;		// z-height of vehicle sprite
	int8 x_offs;			// x offset for vehicle sprite
	int8 y_offs;			// y offset for vehicle sprite
	EngineID engine_type;

	// for randomized variational spritegroups
	// bitmask used to resolve them; parts of it get reseeded when triggers
	// of corresponding spritegroups get matched
	byte random_bits;
	byte waiting_triggers; // triggers to be yet matched

	uint16 max_speed;	// maximum speed
	uint16 cur_speed;	// current speed
	byte subspeed;		// fractional speed
	uint16 acceleration; // used by train & aircraft
	uint16 progress;

	byte vehstatus;		// Status
	uint16 last_station_visited;

	byte cargo_type;	// type of cargo this vehicle is carrying
	byte cargo_days; // how many days have the pieces been in transit
	uint16 cargo_source;// source of cargo
	uint16 cargo_cap;	// total capacity
	uint16 cargo_count;// how many pieces are used

	byte day_counter; // increased by one for each day
	byte tick_counter;// increased by one for each tick

	/* Begin Order-stuff * /
	Order current_order;     //! The current order (+ status, like: loading)
	OrderID cur_order_index; //! The index to the current order

	Order *orders;           //! Pointer to the first order for this vehicle
	OrderID num_orders;      //! How many orders there are in the list

	Vehicle *next_shared;    //! If not NULL, this points to the next vehicle that shared the order
	Vehicle *prev_shared;    //! If not NULL, this points to the prev vehicle that shared the order
	/* End Order-stuff * /

	// Boundaries for the current position in the world and a next hash link.
	// NOSAVE: All of those can be updated with VehiclePositionChanged()
	int32 left_coord;
	int32 top_coord;
	int32 right_coord;
	int32 bottom_coord;
	VehicleID next_hash;

	// Related to age and service time
	uint16 age;				// Age in days
	uint16 max_age;		// Maximum age
	uint16 date_of_last_service;
	uint16 service_interval;
	uint16 reliability;
	uint16 reliability_spd_dec;
	byte breakdown_ctr;
	byte breakdown_delay;
	byte breakdowns_since_last_service;
	byte breakdown_chance;
	byte build_year;

	bool leave_depot_instantly;	// NOSAVE: stores if the vehicle needs to leave the depot it just entered. Used by autoreplace

	uint16 load_unload_time_rem;

	int32 profit_this_year;
	int32 profit_last_year;
	uint32 value;

	// Current position in a vehicle queue - can only belong to one queue at a time
	VQueueItem* queue_item;

	union {
		VehicleRail rail;
		VehicleAir air;
		VehicleRoad road;
		VehicleSpecial special;
		VehicleDisaster disaster;
		VehicleShip ship;
	} u;
};
*/
#define is_custom_sprite(x) (x >= 0xFD)
#define IS_CUSTOM_FIRSTHEAD_SPRITE(x) (x == 0xFD)
#define IS_CUSTOM_SECONDHEAD_SPRITE(x) (x == 0xFE)
/*
typedef void VehicleTickProc(Vehicle *v);
typedef void *VehicleFromPosProc(Vehicle *v, void *data);

void VehicleServiceInDepot(Vehicle *v);
Vehicle *AllocateVehicle(void);
bool AllocateVehicles(Vehicle **vl, int num);
Vehicle *ForceAllocateVehicle(void);
Vehicle *ForceAllocateSpecialVehicle(void);
void UpdateVehiclePosHash(Vehicle *v, int x, int y);
void VehiclePositionChanged(Vehicle *v);
void AfterLoadVehicles(void);
Vehicle *GetLastVehicleInChain(Vehicle *v);
Vehicle *GetPrevVehicleInChain(const Vehicle *v);
Vehicle *GetFirstVehicleInChain(const Vehicle *v);
uint CountVehiclesInChain(const Vehicle* v);
void DeleteVehicle(Vehicle *v);
void DeleteVehicleChain(Vehicle *v);
*/

void *VehicleFromPos(TileIndex tile, void *data, VehicleFromPosProc *proc);
void CallVehicleTicks(void);
Vehicle *FindVehicleOnTileZ(TileIndex tile, byte z);

void InitializeTrains(void);
byte VehicleRandomBits(void);

bool CanFillVehicle(Vehicle *v);
bool CanRefitTo(EngineID engine_type, CargoID cid_to);

void ViewportAddVehicles(DrawPixelInfo *dpi);

void TrainEnterDepot(Vehicle *v, TileIndex tile);

void AddRearEngineToMultiheadedTrain(Vehicle *v, Vehicle *u, bool building) ;

/* train_cmd.h */
int GetTrainImage(const Vehicle *v, byte direction);
int GetAircraftImage(const Vehicle *v, byte direction);
int GetRoadVehImage(const Vehicle *v, byte direction);
int GetShipImage(const Vehicle *v, byte direction);

Vehicle *CreateEffectVehicle(int x, int y, int z, EffectVehicle type);
Vehicle *CreateEffectVehicleAbove(int x, int y, int z, EffectVehicle type);
Vehicle *CreateEffectVehicleRel(const Vehicle *v, int x, int y, int z, EffectVehicle type);

uint32 VehicleEnterTile(Vehicle *v, TileIndex tile, int x, int y);

void VehicleInTheWayErrMsg(const Vehicle* v);
Vehicle *FindVehicleBetween(TileIndex from, TileIndex to, byte z);
TileIndex GetVehicleOutOfTunnelTile(const Vehicle *v);

bool UpdateSignalsOnSegment(TileIndex tile, byte direction);
void SetSignalsOnBothDir(TileIndex tile, byte track);

Vehicle *CheckClickOnVehicle(const ViewPort *vp, int x, int y);

void DecreaseVehicleValue(Vehicle *v);
void CheckVehicleBreakdown(Vehicle *v);
void AgeVehicle(Vehicle *v);
void VehicleEnteredDepotThisTick(Vehicle *v);

void BeginVehicleMove(Vehicle *v);
void EndVehicleMove(Vehicle *v);

bool IsAircraftHangarTile(TileIndex tile);
void ShowAircraftViewWindow(const Vehicle* v);

UnitID GetFreeUnitNumber(byte type);

int LoadUnloadVehicle(Vehicle *v);

void TrainConsistChanged(Vehicle *v);
void UpdateTrainAcceleration(Vehicle *v);
int32 GetTrainRunningCost(const Vehicle *v);

int CheckTrainStoppedInDepot(const Vehicle *v);

bool VehicleNeedsService(const Vehicle *v);

typedef struct GetNewVehiclePosResult {
	int x,y;
	TileIndex old_tile;
	TileIndex new_tile;
} GetNewVehiclePosResult;

/**
 * Returns the Trackdir on which the vehicle is currently located.
 * Works for trains and ships.
 * Currently works only sortof for road vehicles, since they have a fuzzy
 * concept of being "on" a trackdir. Dunno really what it returns for a road
 * vehicle that is halfway a tile, never really understood that part. For road
 * vehicles that are at the beginning or end of the tile, should just return
 * the diagonal trackdir on which they are driving. I _think_.
 * For other vehicles types, or vehicles with no clear trackdir (such as those
 * in depots), returns 0xFF.
 */
Trackdir GetVehicleTrackdir(const Vehicle* v);

/* returns true if staying in the same tile */
bool GetNewVehiclePos(const Vehicle *v, GetNewVehiclePosResult *gp);
byte GetDirectionTowards(const Vehicle *v, int x, int y);

#define BEGIN_ENUM_WAGONS(v) do {
#define END_ENUM_WAGONS(v) } while ( (v=v->next) != NULL);

/* vehicle.c */
VARDEF SortStruct *_vehicle_sort;

//extern MemoryPool _vehicle_pool;

/**
 * Get the pointer to the vehicle with index 'index'
 * /
static inline Vehicle *GetVehicle(VehicleID index)
{
	return (Vehicle*)GetItemFromPool(&_vehicle_pool, index);
}

/**
 * Get the current size of the VehiclePool
 * /
static inline uint16 GetVehiclePoolSize(void)
{
	return _vehicle_pool.total_items;
}

#define FOR_ALL_VEHICLES_FROM(v, start) for (v = GetVehicle(start); v != NULL; v = (v->index + 1 < GetVehiclePoolSize()) ? GetVehicle(v->index + 1) : NULL)
#define FOR_ALL_VEHICLES(v) FOR_ALL_VEHICLES_FROM(v, 0)

/**
 * Check if a Vehicle really exists.
 * /
static inline bool IsValidVehicle(const Vehicle *v)
{
	return v->type != 0;
}

/**
 * Check if an index is a vehicle-index (so between 0 and max-vehicles)
 *
 * @return Returns true if the vehicle-id is in range
 * /
static inline bool IsVehicleIndex(uint index)
{
	return index < GetVehiclePoolSize();
}

/* Returns order 'index' of a vehicle or NULL when it doesn't exists * /
static inline Order *GetVehicleOrder(const Vehicle *v, int index)
{
	Order *order = v->orders;

	if (index < 0) return NULL;

	while (order != NULL && index-- > 0)
		order = order->next;

	return order;
}

/* Returns the last order of a vehicle, or NULL if it doesn't exists * /
static inline Order *GetLastVehicleOrder(const Vehicle *v)
{
	Order *order = v->orders;

	if (order == NULL) return NULL;

	while (order->next != NULL)
		order = order->next;

	return order;
}

/* Get the first vehicle of a shared-list, so we only have to walk forwards * /
static inline Vehicle *GetFirstVehicleFromSharedList(Vehicle *v)
{
	Vehicle *u = v;
	while (u->prev_shared != NULL)
		u = u->prev_shared;

	return u;
}
*/
// NOSAVE: Return values from various commands.
VARDEF VehicleID _new_train_id;
VARDEF VehicleID _new_wagon_id;
VARDEF VehicleID _new_aircraft_id;
VARDEF VehicleID _new_ship_id;
VARDEF VehicleID _new_roadveh_id;
VARDEF VehicleID _new_vehicle_id;
VARDEF uint16 _aircraft_refit_capacity;
VARDEF byte _cmd_build_rail_veh_score;

//#define INVALID_VEHICLE 0xFFFF
#define INVALID_ENGINE 0xFFFF

/* A lot of code calls for the invalidation of the status bar, which is widget 5.
 * Best is to have a virtual value for it when it needs to change again */
#define STATUS_BAR 5

#endif /* VEHICLE_H */
