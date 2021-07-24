package game;

public class Npf {








	//mowing grass
	enum {
		NPF_HASH_BITS = 12, /* The size of the hash used in pathfinding. Just changing this value should be sufficient to change the hash size. Should be an even value. */
		/* Do no change below values */
		NPF_HASH_SIZE = 1 << NPF_HASH_BITS,
		NPF_HASH_HALFBITS = NPF_HASH_BITS / 2,
		NPF_HASH_HALFMASK = (1 << NPF_HASH_HALFBITS) - 1
	};

		/** This penalty is the equivalent of "inifite", which means that paths that
		 * get this penalty will be chosen, but only if there is no other route
		 * without it. Be careful with not applying this penalty to often, or the
		 * total path cost might overflow..
		 * For now, this is just a Very Big Penalty, we might actually implement
		 * this in a nicer way :-)
		 */
		private static final int NPF_INFINITE_PENALTY = 1000 * NPF_TILE_LENGTH;


	enum { /* Indices into AyStar.userdata[] */
		NPF_TYPE = 0, /* Contains a TransportTypes value */
		NPF_OWNER, /* Contains an Owner value */
		NPF_RAILTYPE, /* Contains the RailType value of the engine when NPF_TYPE == TRANSPORT_RAIL. Unused otherwise. */
		NPF_PBS_MODE, /* Contains the pbs mode, see pbs.h */
	};

	enum { /* Indices into AyStarNode.userdata[] */
		NPF_TRACKDIR_CHOICE = 0, /* The trackdir chosen to get here */
		NPF_NODE_FLAGS,
	}

	enum NPFNodeFlag { /* Flags for AyStarNode.userdata[NPF_NODE_FLAGS]. Use NPFGetBit() and NPFGetBit() to use them. */
		NPF_FLAG_SEEN_SIGNAL, /* Used to mark that a signal was seen on the way, for rail only */
		NPF_FLAG_REVERSE, /* Used to mark that this node was reached from the second start node, if applicable */
		NPF_FLAG_LAST_SIGNAL_RED, /* Used to mark that the last signal on this path was red */
		NPF_FLAG_PBS_EXIT, /* Used to mark tracks inside a pbs block, for rail only, for the end node, this is set when the path found goes through a pbs block */
		NPF_FLAG_PBS_BLOCKED, /* Used to mark that this path crosses another pbs path */
		NPF_FLAG_PBS_RED, /* Used to mark that this path goes through a red exit-pbs signal */
		NPF_FLAG_PBS_CHOICE, /* Used to mark that the train has had a choice on this path */
		NPF_FLAG_PBS_TARGET_SEEN, /* Used to mark that a target tile has been passed on this path */
	} ;


	/* These functions below are _not_ re-entrant, in favor of speed! */

	/* Will search from the given tile and direction, for a route to the given
	 * station for the given transport type. See the declaration of
	 * NPFFoundTargetData above for the meaning of the result. */
	NPFFoundTargetData NPFRouteToStationOrTile(TileIndex tile, Trackdir trackdir, NPFFindStationOrTileData  target, TransportType type, Owner owner, RailType railtype, byte pbs_mode);

	/* Will search as above, but with two start nodes, the second being the
	 * reverse. Look at the NPF_FLAG_REVERSE flag in the result node to see which
	 * direction was taken (NPFGetBit(result.node, NPF_FLAG_REVERSE)) */
	NPFFoundTargetData NPFRouteToStationOrTileTwoWay(TileIndex tile1, Trackdir trackdir1, TileIndex tile2, Trackdir trackdir2, NPFFindStationOrTileData  target, TransportType type, Owner owner, RailType railtype, byte pbs_mode);

	/* Will search a route to the closest depot. */

	/* Search using breadth first. Good for little track choice and inaccurate
	 * heuristic, such as railway/road.*/
	NPFFoundTargetData NPFRouteToDepotBreadthFirst(TileIndex tile, Trackdir trackdir, TransportType type, Owner owner, RailType railtype);
	/* Same as above but with two start nodes, the second being the reverse. Call
	 * NPFGetBit(result.node, NPF_FLAG_REVERSE) to see from which node the path
	 * orginated. All pathfs from the second node will have the given
	 * reverse_penalty applied (NPF_TILE_LENGTH is the equivalent of one full
	 * tile).
	 */
	NPFFoundTargetData NPFRouteToDepotBreadthFirstTwoWay(TileIndex tile1, Trackdir trackdir1, TileIndex tile2, Trackdir trackdir2, TransportType type, Owner owner, RailType railtype, int reverse_penalty);
	/* Search by trying each depot in order of Manhattan Distance. Good for lots
	 * of choices and accurate heuristics, such as water. */
	NPFFoundTargetData NPFRouteToDepotTrialError(TileIndex tile, Trackdir trackdir, TransportType type, Owner owner, RailType railtype);



	/*
	 * Functions to manipulate the various NPF related flags on an AyStarNode.
	 */

	/**
	 * Returns the current value of the given flag on the given AyStarNode.
	 */
	static  boolean NPFGetFlag(final AyStarNode  node, NPFNodeFlag flag)
	{
		return BitOps.HASBIT(node.user_data[NPF_NODE_FLAGS], flag);
	}

	/**
	 * Sets the given flag on the given AyStarNode to the given value.
	 */
	static  void NPFSetFlag(AyStarNode  node, NPFNodeFlag flag, boolean value)
	{
		if (value)
			SETBIT(node.user_data[NPF_NODE_FLAGS], flag);
		else
			CLRBIT(node.user_data[NPF_NODE_FLAGS], flag);
	}

	#endif /* NPF_H */


	
	/* $Id: npf.c 3271 2005-12-08 09:12:25Z hackykid $ */













	static AyStar _npf_aystar;

	/* The cost of each trackdir. A diagonal piece is the full NPF_TILE_LENGTH,
	 * the shorter piece is sqrt(2)/2*NPF_TILE_LENGTH =~ 0.7071
	 */
	#define NPF_STRAIGHT_LENGTH (int)(NPF_TILE_LENGTH * STRAIGHT_TRACK_LENGTH)
	static final int _trackdir_length[TRACKDIR_END] = {
		NPF_TILE_LENGTH, NPF_TILE_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH,
		0, 0,
		NPF_TILE_LENGTH, NPF_TILE_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH
	};

	/**
	 * Calculates the minimum distance traveled to get from t0 to t1 when only
	 * using tracks (ie, only making 45 degree turns). Returns the distance in the
	 * NPF scale, ie the number of full tiles multiplied by NPF_TILE_LENGTH to
	 * prevent rounding.
	 */
	static int NPFDistanceTrack(TileIndex t0, TileIndex t1)
	{
		final int dx = abs(TileX(t0) - TileX(t1));
		final int dy = abs(TileY(t0) - TileY(t1));

		final int straightTracks = 2 * Math.min(dx, dy); /* The number of straight (not full length) tracks */
		/* OPTIMISATION:
		 * Original: diagTracks = Math.max(dx, dy) - Math.min(dx,dy);
		 * Proof:
		 * (dx+dy) - straightTracks  == (min + max) - straightTracks = min + max - 2 * min = max - min */
		final int diagTracks = dx + dy - straightTracks; /* The number of diagonal (full tile length) tracks. */

		/* Don't factor out NPF_TILE_LENGTH below, this will round values and lose
		 * precision */
		return diagTracks * NPF_TILE_LENGTH + straightTracks * NPF_TILE_LENGTH * STRAIGHT_TRACK_LENGTH;
	}

	/**
	 * Check if a rail track is the end of the line. Will also consider 1-way signals to be the end of a line.
	 * @param tile The tile on which the current track is.
	 * @param trackdir The (track)direction in which you want to look.
	 * @param enginetype The type of the engine for which we are checking this.
	 */
	static boolean IsEndOfLine(TileIndex tile, Trackdir trackdir, RailType enginetype)
	{
		byte exitdir = TrackdirToExitdir(trackdir);
		TileIndex dst_tile;
		int ts;

		/* Can always go into a tunnel */
		if (tile.IsTileType( TileTypes.MP_TUNNELBRIDGE) && BitOps.GB(tile.getMap().m5, 4, 4) == 0 &&
				BitOps.GB(tile.getMap().m5, 0, 2) == exitdir) {
			return false;
		}

		/* Cannot go through the back of a depot */
		if (IsTileDepotType(tile, TRANSPORT_RAIL) && (exitdir != GetDepotDirection(tile, TRANSPORT_RAIL)))
			return true;

		/* Calculate next tile */
		dst_tile = tile + TileOffsByDir(exitdir);
		// determine the track status on the next tile.
		ts = GetTileTrackStatus(dst_tile, TRANSPORT_RAIL) & TrackdirReachesTrackdirs(trackdir);

		// when none of the trackdir bits are set, we cant enter the new tile
		if ( (ts & TRACKDIR_BIT_MASK) == 0)
			return true;

		{
			byte dst_type = GetTileRailType(dst_tile, exitdir);
			if (!IsCompatibleRail(enginetype, dst_type))
				return true;
			if (GetTileOwner(tile) != GetTileOwner(dst_tile))
				return true;

			/* Prevent us from entering a depot from behind */
			if (IsTileDepotType(dst_tile, TRANSPORT_RAIL) && (exitdir != ReverseDiagdir(GetDepotDirection(dst_tile, TRANSPORT_RAIL))))
				return true;

			/* Prevent us from falling off a slope into a tunnel exit */
			if (IsTileType(dst_tile, TileTypes.MP_TUNNELBRIDGE) &&
					BitOps.GB(_m[dst_tile].m5, 4, 4) == 0 &&
					(DiagDirection)BitOps.GB(_m[dst_tile].m5, 0, 2) == ReverseDiagdir(exitdir)) {
				return true;
			}

			/* Check for oneway signal against us */
			if (IsTileType(dst_tile, TileTypes.MP_RAILWAY) && GetRailTileType(dst_tile) == RAIL_TYPE_SIGNALS) {
				if (HasSignalOnTrackdir(dst_tile, ReverseTrackdir(FindFirstBit2x64(ts))) && !HasSignalOnTrackdir(dst_tile, FindFirstBit2x64(ts)))
					// if one way signal not pointing towards us, stop going in this direction.
					return true;
			}

			return false;
		}
	}

	#if 0
	static int NTPHash(int key1, int key2)
	{
		/* This function uses the old hash, which is fixed on 10 bits (1024 buckets) */
		return PATHFIND_HASH_TILE(key1);
	}
	#endif

	/**
	 * Calculates a hash value for use in the NPF.
	 * @param key1	The TileIndex of the tile to hash
	 * @param key1	The Trackdir of the track on the tile.
	 *
	 * @todo	Think of a better hash.
	 */
	static int NPFHash(int key1, int key2)
	{
		/* TODO: think of a better hash? */
		int part1 = TileX(key1) & NPF_HASH_HALFMASK;
		int part2 = TileY(key1) & NPF_HASH_HALFMASK;

		assert(IsValidTrackdir(key2));
		assert(IsValidTile(key1));
		return ((((part1 << NPF_HASH_HALFBITS) | part2)) + (NPF_HASH_SIZE * key2 / TRACKDIR_END)) % NPF_HASH_SIZE;
	}

	static int NPFCalcZero(AyStar  as, AyStarNode  current, OpenListNode  parent)
	{
		return 0;
	}

	/* Calcs the tile of given station that is closest to a given tile
	 * for this we assume the station is a rectangle,
	 * as defined by its top tile (st.train_tile) and its width/height (st.trainst_w, st.trainst_h)
	 */
	static TileIndex CalcClosestStationTile(StationID station, TileIndex tile)
	{
		final Station  st = Station.GetStation(station);

		int minx = TileX(st.train_tile);  // topmost corner of station
		int miny = TileY(st.train_tile);
		int maxx = minx + st.trainst_w - 1; // lowermost corner of station
		int maxy = miny + st.trainst_h - 1;
		int x;
		int y;

		// we are going the aim for the x coordinate of the closest corner
		// but if we are between those coordinates, we will aim for our own x coordinate
		x = clamp(TileX(tile), minx, maxx);

		// same for y coordinate, see above comment
		y = clamp(TileY(tile), miny, maxy);

		// return the tile of our target coordinates
		return TileXY(x, y);
	}

	/* On PBS pathfinding runs, this is called before pathfinding ends (BeforeExit aystar callback), and will
	 * reserve the appropriate tracks, if needed. */
	static void NPFReservePBSPath(AyStar *as)
	{
		NPFFoundTargetData* ftd = (NPFFoundTargetData*)as.user_path;
		boolean eol_end = false;

		if (ftd.best_trackdir == 0xFF)
			return;

		if (!NPFGetFlag(&ftd.node, NPF_FLAG_PBS_EXIT) && IsEndOfLine(ftd.node.tile, ftd.node.direction, as.user_data[NPF_RAILTYPE]) && !NPFGetFlag(&ftd.node, NPF_FLAG_SEEN_SIGNAL)) {
			/* The path ends in an end of line, we'll need to reserve a path.
			 * We treat and end of line as a red exit signal */
			eol_end = true;
			NPFSetFlag(&ftd.node, NPF_FLAG_PBS_EXIT, true);
			if (!NPFGetFlag(&ftd.node, NPF_FLAG_PBS_TARGET_SEEN))
				NPFSetFlag(&ftd.node, NPF_FLAG_PBS_RED, true);
		}

		if (!NPFGetFlag(&ftd.node, NPF_FLAG_PBS_CHOICE)) {
			/* there have been no choices to make on our path, we dont care if our end signal is red */
			NPFSetFlag(&ftd.node, NPF_FLAG_PBS_RED, false);
		}

		if (NPFGetFlag(&ftd.node, NPF_FLAG_PBS_EXIT) && // we passed an exit signal
			 !NPFGetFlag(&ftd.node, NPF_FLAG_PBS_BLOCKED) && // we didnt encounter reserver tracks
			 ((as.user_data[NPF_PBS_MODE] != PBS_MODE_GREEN) || (!NPFGetFlag(&ftd.node, NPF_FLAG_PBS_RED))) ) { // our mode permits having a red exit signal, or the signal is green
			PathNode parent;
			PathNode *curr;
			PathNode *prev;
			TileIndex start = INVALID_TILE;
			byte trackdir = 0;

			parent.node = ftd.node;
			parent.parent = &ftd.path;
			curr = &parent;
			prev = null;

			do {
				if (!NPFGetFlag(&curr.node, NPF_FLAG_PBS_EXIT) || eol_end) {
					/* check for already reserved track on this path, if they clash with what we
					   currently trying to reserve, we have a self-crossing path :-( */
					if ((PBSTileUnavail(curr.node.tile) & (1 << curr.node.direction))
					&& !(PBSTileReserved(curr.node.tile) & (1 << (curr.node.direction & 7)))
					&& (start != INVALID_TILE)) {
						/* It's actually quite bad if this happens, it means the pathfinder
						 * found a path that is intersecting with itself, which is a very bad
						 * thing in a pbs block. Also there is not much we can do about it at
						 * this point....
						 * BUT, you have to have a pretty fucked up junction layout for this to happen,
						 * so we'll just stop this train, the user will eventually notice, so he can fix it.
						 */
						PBSClearPath(start, trackdir, curr.node.tile, curr.node.direction);
						NPFSetFlag(&ftd.node, NPF_FLAG_PBS_BLOCKED, true);
						DEBUG(pbs, 1) ("PBS: Self-crossing path!!!");
						return;
					};

					PBSReserveTrack(curr.node.tile, TrackdirToTrack(curr.node.direction) );

					/* we want to reserve the last tile (with the signal) on the path too
					   also remember this tile, cause its the end of the path (where we exit the block) */
					if (start == INVALID_TILE) {
						if (prev != null) {
							PBSReserveTrack(prev.node.tile, TrackdirToTrack(prev.node.direction) );
							start = prev.node.tile;
							trackdir = ReverseTrackdir(prev.node.direction);
						} else {
							start = curr.node.tile;
							trackdir = curr.node.direction;
						}
					}
				}

				prev = curr;
				curr = curr.parent;
			} while (curr != null);
			// we remember the tile/track where this path leaves the pbs junction
			ftd.node.tile = start;
			ftd.node.direction = trackdir;
		}
	}


	/* Calcs the heuristic to the target station or tile. For train stations, it
	 * takes into account the direction of approach.
	 */
	static int NPFCalcStationOrTileHeuristic(AyStar  as, AyStarNode  current, OpenListNode  parent)
	{
		NPFFindStationOrTileData  fstd = (NPFFindStationOrTileData )as.user_target;
		NPFFoundTargetData* ftd = (NPFFoundTargetData*)as.user_path;
		TileIndex from = current.tile;
		TileIndex to = fstd.dest_coords;
		int dist;

		// for train-stations, we are going to aim for the closest station tile
		if ((as.user_data[NPF_TYPE] == TRANSPORT_RAIL) && (fstd.station_index != -1))
			to = CalcClosestStationTile(fstd.station_index, from);

		if (as.user_data[NPF_TYPE] == TRANSPORT_ROAD)
			/* Since roads only have diagonal pieces, we use manhattan distance here */
			dist = DistanceManhattan(from, to) * NPF_TILE_LENGTH;
		else
			/* Ships and trains can also go diagonal, so the minimum distance is shorter */
			dist = NPFDistanceTrack(from, to);

		DEBUG_npf( 4, "Calculating H for: (%d, %d). Result: %d", TileX(current.tile), TileY(current.tile), dist);

		/* for pbs runs, we ignore tiles inside the pbs block for the tracking
		   of the 'closest' tile */
		if ((as.user_data[NPF_PBS_MODE] != PBS_MODE_NONE)
		&&  (!NPFGetFlag(current , NPF_FLAG_SEEN_SIGNAL))
		&&  (!IsEndOfLine(current.tile, current.direction, as.user_data[NPF_RAILTYPE])))
			return dist;

		if ((dist < ftd.best_bird_dist) ||
			/* for pbs runs, prefer tiles that pass a green exit signal to the pbs blocks */
			((as.user_data[NPF_PBS_MODE] != PBS_MODE_NONE) && !NPFGetFlag(current, NPF_FLAG_PBS_RED) && NPFGetFlag(&ftd.node, NPF_FLAG_PBS_RED))
	) {
			ftd.best_bird_dist = dist;
			ftd.best_trackdir = current.user_data[NPF_TRACKDIR_CHOICE];
			ftd.path = parent.path;
			ftd.node = *current;
		}
		return dist;
	}

	/* Fills AyStarNode.user_data[NPF_TRACKDIRCHOICE] with the chosen direction to
	 * get here, either getting it from the current choice or from the parent's
	 * choice */
	static void NPFFillTrackdirChoice(AyStarNode  current, OpenListNode  parent)
	{
		if (parent.path.parent == null) {
			Trackdir trackdir = (Trackdir)current.direction;
			/* This is a first order decision, so we'd better save the
			 * direction we chose */
			current.user_data[NPF_TRACKDIR_CHOICE] = trackdir;
			DEBUG_npf( 6, "Saving trackdir: %#x", trackdir);
		} else {
			/* We've already made the decision, so just save our parent's
			 * decision */
			current.user_data[NPF_TRACKDIR_CHOICE] = parent.path.node.user_data[NPF_TRACKDIR_CHOICE];
		}

	}

	/* Will return the cost of the tunnel. If it is an entry, it will return the
	 * cost of that tile. If the tile is an exit, it will return the tunnel length
	 * including the exit tile. Requires that this is a Tunnel tile */
	static int NPFTunnelCost(AyStarNode  current)
	{
		DiagDirection exitdir = TrackdirToExitdir((Trackdir)current.direction);
		TileIndex tile = current.tile;
		if ((DiagDirection)BitOps.GB(tile.getMap().m5, 0, 2) == ReverseDiagdir(exitdir)) {
			/* We just popped out if this tunnel, since were
			 * facing the tunnel exit */
			FindLengthOfTunnelResult flotr;
			flotr = FindLengthOfTunnel(tile, ReverseDiagdir(exitdir));
			return flotr.length * NPF_TILE_LENGTH;
			//TODO: Penalty for tunnels?
		} else {
			/* We are entering the tunnel, the enter tile is just a
			 * straight track */
			return NPF_TILE_LENGTH;
		}
	}

	static int NPFSlopeCost(AyStarNode  current)
	{
		TileIndex next = current.tile + TileOffsByDir(TrackdirToExitdir(current.direction));
		int x,y;
		int8 z1,z2;

		x = TileX(current.tile) * TILE_SIZE;
		y = TileY(current.tile) * TILE_SIZE;
		/* get the height of the center of the current tile */
		z1 = GetSlopeZ(x+TILE_HEIGHT, y+TILE_HEIGHT);

		x = TileX(next) * TILE_SIZE;
		y = TileY(next) * TILE_SIZE;
		/* get the height of the center of the next tile */
		z2 = GetSlopeZ(x+TILE_HEIGHT, y+TILE_HEIGHT);

		if ((z2 - z1) > 1) {
			/* Slope up */
			return Global._patches.npf_rail_slope_penalty;
		}
		return 0;
		/* Should we give a bonus for slope down? Probably not, we
		 * could just substract that bonus from the penalty, because
		 * there is only one level of steepness... */
	}

	/* Mark tiles by mowing the grass when npf debug level >= 1 */
	static void NPFMarkTile(TileIndex tile)
	{
	#ifdef NO_DEBUG_MESSAGES
		return;
	#else
		if (_debug_npf_level >= 1)
			switch(GetTileType(tile)) {
				case TileTypes.MP_RAILWAY:
					/* DEBUG: mark visited tiles by mowing the grass under them
					 * ;-) */
					if (!IsTileDepotType(tile, TRANSPORT_RAIL)) {
						BitOps.RET SB(tile.getMap().m2, 0, 4, 0);
						MarkTileDirtyByTile(tile);
					}
					break;
				case TileTypes.MP_STREET:
					if (!IsTileDepotType(tile, TRANSPORT_ROAD)) {
						BitOps.RET SB(tile.getMap().m2, 4, 3, 0);
						MarkTileDirtyByTile(tile);
					}
					break;
				default:
					break;
			}
	#endif
	}

	static int NPFWaterPathCost(AyStar  as, AyStarNode  current, OpenListNode  parent)
	{
		//TileIndex tile = current.tile;
		int cost = 0;
		Trackdir trackdir = (Trackdir)current.direction;

		cost = _trackdir_length[trackdir]; /* Should be different for diagonal tracks */

		if (IsBuoyTile(current.tile) && IsDiagonalTrackdir(trackdir))
			cost += Global._patches.npf_buoy_penalty; /* A small penalty for going over buoys */

		if (current.direction != NextTrackdir((Trackdir)parent.path.node.direction))
			cost += Global._patches.npf_water_curve_penalty;

		/* TODO More penalties? */

		return cost;
	}

	/* Determine the cost of this node, for road tracks */
	static int NPFRoadPathCost(AyStar  as, AyStarNode  current, OpenListNode  parent)
	{
		TileIndex tile = current.tile;
		int cost = 0;

		/* Determine base length */
		switch (GetTileType(tile)) {
			case TileTypes.MP_TUNNELBRIDGE:
				if (BitOps.GB(tile.getMap().m5, 4, 4) == 0) {
					cost = NPFTunnelCost(current);
					break;
				}
				cost = NPF_TILE_LENGTH;
				break;
			case TileTypes.MP_STREET:
				cost = NPF_TILE_LENGTH;
				/* Increase the cost for level crossings */
				if (IsLevelCrossing(tile))
					cost += Global._patches.npf_crossing_penalty;
				break;
			default:
				break;
		}

		/* Determine extra costs */

		/* Check for slope */
		cost += NPFSlopeCost(current);

		/* Check for turns. Road vehicles only really drive diagonal, turns are
		 * represented by non-diagonal tracks */
		if (!IsDiagonalTrackdir(current.direction))
			cost += Global._patches.npf_road_curve_penalty;

		NPFMarkTile(tile);
		DEBUG_npf( 4, "Calculating G for: (%d, %d). Result: %d", TileX(current.tile), TileY(current.tile), cost);
		return cost;
	}


	/* Determine the cost of this node, for railway tracks */
	static int NPFRailPathCost(AyStar  as, AyStarNode  current, OpenListNode  parent)
	{
		TileIndex tile = current.tile;
		Trackdir trackdir = (Trackdir)current.direction;
		int cost = 0;
		/* HACK: We create a OpenListNode manualy, so we can call EndNodeCheck */
		OpenListNode new_node;

		/* Determine base length */
		switch (GetTileType(tile)) {
			case TileTypes.MP_TUNNELBRIDGE:
				if (BitOps.GB(tile.getMap().m5, 4, 4) == 0) {
					cost = NPFTunnelCost(current);
					break;
				}
				/* Fall through if above if is false, it is a bridge
				 * then. We treat that as ordinary rail */
			case TileTypes.MP_RAILWAY:
				cost = _trackdir_length[trackdir]; /* Should be different for diagonal tracks */
				break;
			case TileTypes.MP_STREET: /* Railway crossing */
				cost = NPF_TILE_LENGTH;
				break;
			case TileTypes.MP_STATION:
				/* We give a station tile a penalty. Logically we would only
						* want to give station tiles that are not our destination
						* this penalty. This would discourage trains to drive through
						* busy stations. But, we can just give any station tile a
						* penalty, because every possible route will get this penalty
						* exactly once, on its end tile (if it's a station) and it
				* will therefore not make a difference. */
				cost = NPF_TILE_LENGTH + Global._patches.npf_rail_station_penalty;
				break;
			default:
				break;
		}

		/* Determine extra costs */

		/* Check for reserved tracks (PBS) */
		if ((as.user_data[NPF_PBS_MODE] != PBS_MODE_NONE) && !(NPFGetFlag(current, NPF_FLAG_PBS_EXIT)) && !(NPFGetFlag(current, NPF_FLAG_PBS_BLOCKED)) && (PBSTileUnavail(tile) & (1<<trackdir))) {
			NPFSetFlag(current, NPF_FLAG_PBS_BLOCKED, true);
		};

		/* Check for signals */
		if (tile.IsTileType( TileTypes.MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir)) {
			/* Ordinary track with signals */
			if (GetSignalState(tile, trackdir) == SIGNAL_STATE_RED) {
				/* Signal facing us is red */
				if (!NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL)) {
					/* Penalize the first signal we
					 * encounter, if it is red */

					/* Is this a presignal exit or combo? */
					SignalType sigtype = GetSignalType(tile, TrackdirToTrack(trackdir));
					if (sigtype == SIGTYPE_EXIT || sigtype == SIGTYPE_COMBO)
						/* Penalise exit and combo signals differently (heavier) */
						cost += Global._patches.npf_rail_firstred_exit_penalty;
					else
						cost += Global._patches.npf_rail_firstred_penalty;

					/* for pbs runs, store the fact that the exit signal to the pbs block was red */
					if (!(NPFGetFlag(current, NPF_FLAG_PBS_EXIT)) && !(NPFGetFlag(current, NPF_FLAG_PBS_RED)) && NPFGetFlag(current, NPF_FLAG_PBS_CHOICE))
						NPFSetFlag(current, NPF_FLAG_PBS_RED, true);
				}
				/* Record the state of this signal */
				NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_RED, true);
			} else {
				/* Record the state of this signal */
				NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_RED, false);
			}

			if (!NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL) && NPFGetFlag(current, NPF_FLAG_PBS_BLOCKED)) {
				/* penalise a path through the pbs block if it crosses reserved tracks */
				cost += 1000;
			}
			if ((PBSIsPbsSignal(tile, trackdir)) && !NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL)) {
				/* we've encountered an exit signal to the pbs block */
				NPFSetFlag(current, NPF_FLAG_PBS_EXIT, true);
			}
			NPFSetFlag(current, NPF_FLAG_SEEN_SIGNAL, true);
		}

		/* Penalise the tile if it is a target tile and the last signal was
		 * red */
		/* HACK: We create a new_node here so we can call EndNodeCheck. Ugly as hell
		 * of course... */
		new_node.path.node = *current;
		if (as.EndNodeCheck(as, &new_node) == AYSTAR_FOUND_END_NODE && NPFGetFlag(current, NPF_FLAG_LAST_SIGNAL_RED))
			cost += Global._patches.npf_rail_lastred_penalty;

		/* Check for slope */
		cost += NPFSlopeCost(current);

		/* Check for turns */
		if (current.direction != NextTrackdir((Trackdir)parent.path.node.direction))
			cost += Global._patches.npf_rail_curve_penalty;
		//TODO, with realistic acceleration, also the amount of straight track between
		//      curves should be taken into account, as this affects the speed limit.


		/* Check for depots */
		if (IsTileDepotType(tile, TRANSPORT_RAIL)) {
			/* Penalise any depot tile that is not the last tile in the path. This
			 * _should_ penalise every occurence of reversing in a depot (and only
			 * that) */
			if (as.EndNodeCheck(as, &new_node) != AYSTAR_FOUND_END_NODE)
				cost += Global._patches.npf_rail_depot_reverse_penalty;

			/* Do we treat this depot as a pbs signal? */
			if (!NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL)) {
				if (NPFGetFlag(current, NPF_FLAG_PBS_BLOCKED)) {
					cost += 1000;
				}
				if (PBSIsPbsSegment(tile, ReverseTrackdir(trackdir))) {
					NPFSetFlag(current, NPF_FLAG_PBS_EXIT, true);
					NPFSetFlag(current, NPF_FLAG_SEEN_SIGNAL, true);
				}
			}
			NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_RED, false);
		}

		/* Check for occupied track */
		//TODO

		NPFMarkTile(tile);
		DEBUG_npf( 4, "Calculating G for: (%d, %d). Result: %d", TileX(current.tile), TileY(current.tile), cost);
		return cost;
	}

	/* Will find any depot */
	static int NPFFindDepot(AyStar  as, OpenListNode *current)
	{
		TileIndex tile = current.path.node.tile;

		/* It's not worth caching the result with NPF_FLAG_IS_TARGET here as below,
		 * since checking the cache not that much faster than the actual check */
		if (IsTileDepotType(tile, as.user_data[NPF_TYPE]))
			return AYSTAR_FOUND_END_NODE;
		else
			return AYSTAR_DONE;
	}

	/* Will find a station identified using the NPFFindStationOrTileData */
	static int NPFFindStationOrTile(AyStar  as, OpenListNode *current)
	{
		NPFFindStationOrTileData  fstd = (NPFFindStationOrTileData )as.user_target;
		AyStarNode *node = &current.path.node;
		TileIndex tile = node.tile;

		/* If GetNeighbours said we could get here, we assume the station type
		 * is correct */
		if (
			(fstd.station_index == -1 && tile == fstd.dest_coords) || /* We've found the tile, or */
			(tile.IsTileType( TileTypes.MP_STATION) && tile.getMap().m2 == fstd.station_index) || /* the station */
			(NPFGetFlag(node, NPF_FLAG_PBS_TARGET_SEEN)) /* or, we've passed it already (for pbs) */
		) {
			NPFSetFlag(&current.path.node, NPF_FLAG_PBS_TARGET_SEEN, true);
			/* for pbs runs, only accept we've found the target if we've also found a way out of the block */
			if ((as.user_data[NPF_PBS_MODE] != PBS_MODE_NONE) && !NPFGetFlag(node, NPF_FLAG_SEEN_SIGNAL) && !IsEndOfLine(node.tile, node.direction, as.user_data[NPF_RAILTYPE]))
				return AYSTAR_DONE;
			return AYSTAR_FOUND_END_NODE;
		} else {
			return AYSTAR_DONE;
		}
	}

	/* To be called when current contains the (shortest route to) the target node.
	 * Will fill the contents of the NPFFoundTargetData using
	 * AyStarNode[NPF_TRACKDIR_CHOICE].
	 */
	static void NPFSaveTargetData(AyStar  as, OpenListNode  current)
	{
		NPFFoundTargetData* ftd = (NPFFoundTargetData*)as.user_path;
		ftd.best_trackdir = (Trackdir)current.path.node.user_data[NPF_TRACKDIR_CHOICE];
		ftd.best_path_dist = current.g;
		ftd.best_bird_dist = 0;
		ftd.node = current.path.node;
		ftd.path = current.path;
	}

	/**
	 * Finds out if a given player's vehicles are allowed to enter a given tile.
	 * @param owner    The owner of the vehicle.
	 * @param tile     The tile that is about to be entered.
	 * @param enterdir The direction from which the vehicle wants to enter the tile.
	 * @return         true if the vehicle can enter the tile.
	 * @todo           This function should be used in other places than just NPF,
	 *                 maybe moved to another file too.
	 */
	static boolean VehicleMayEnterTile(Owner owner, TileIndex tile, DiagDirection enterdir)
	{
		if (
			tile.IsTileType( TileTypes.MP_RAILWAY) /* Rail tile (also rail depot) */
			|| IsTrainStationTile(tile) /* Rail station tile */
			|| IsTileDepotType(tile, TRANSPORT_ROAD) /* Road depot tile */
			|| IsRoadStationTile(tile) /* Road station tile */
			|| IsTileDepotType(tile, TRANSPORT_WATER) /* Water depot tile */
			)
			return IsTileOwner(tile, owner); /* You need to own these tiles entirely to use them */

		switch (GetTileType(tile)) {
			case TileTypes.MP_STREET:
				/* rail-road crossing : are we looking at the railway part? */
				if (IsLevelCrossing(tile) && GetCrossingTransportType(tile, TrackdirToTrack(DiagdirToDiagTrackdir(enterdir))) == TRANSPORT_RAIL)
					return IsTileOwner(tile, owner); /* Railway needs owner check, while the street is public */
				break;
			case TileTypes.MP_TUNNELBRIDGE:
	#if 0
	/* OPTIMISATION: If we are on the middle of a bridge, we will not do the cpu
	 * intensive owner check, instead we will just assume that if the vehicle
	 * managed to get on the bridge, it is probably allowed to :-)
	 */
				if ((tile.getMap().m5 & 0xC6) == 0xC0 && BitOps.GB(tile.getMap().m5, 0, 1) == (enterdir & 0x1)) {
					/* on the middle part of a railway bridge: find bridge ending */
					while (tile.IsTileType( TileTypes.MP_TUNNELBRIDGE) && !((tile.getMap().m5 & 0xC6) == 0x80)) {
						tile += TileOffsByDir(BitOps.GB(tile.getMap().m5, 0, 1));
					}
				}
				/* if we were on a railway middle part, we are now at a railway bridge ending */
	#endif
				if (
					(tile.getMap().m5 & 0xFC) == 0 /* railway tunnel */
					|| (tile.getMap().m5 & 0xC6) == 0x80 /* railway bridge ending */
					|| ((tile.getMap().m5 & 0xF8) == 0xE0 && BitOps.GB(tile.getMap().m5, 0, 1) != (enterdir & 0x1)) /* railway under bridge */
					)
					return IsTileOwner(tile, owner);
				break;
			default:
				break;
		}

		return true; /* no need to check */
	}

	/* Will just follow the results of GetTileTrackStatus concerning where we can
	 * go and where not. Uses AyStar.user_data[NPF_TYPE] as the transport type and
	 * an argument to GetTileTrackStatus. Will skip tunnels, meaning that the
	 * entry and exit are neighbours. Will fill
	 * AyStarNode.user_data[NPF_TRACKDIR_CHOICE] with an appropriate value, and
	 * copy AyStarNode.user_data[NPF_NODE_FLAGS] from the parent */
	static void NPFFollowTrack(AyStar  aystar, OpenListNode  current)
	{
		Trackdir src_trackdir = (Trackdir)current.path.node.direction;
		TileIndex src_tile = current.path.node.tile;
		DiagDirection src_exitdir = TrackdirToExitdir(src_trackdir);
		FindLengthOfTunnelResult flotr;
		TileIndex dst_tile;
		int i;
		TrackdirBits trackdirbits, ts;
		TransportType type = aystar.user_data[NPF_TYPE];
		/* Initialize to 0, so we can jump out (return) somewhere an have no neighbours */
		aystar.num_neighbours = 0;
		DEBUG_npf( 4, "Expanding: (%d, %d, %d) [%d]", TileX(src_tile), TileY(src_tile), src_trackdir, src_tile);

		aystar.EndNodeCheck(aystar, current);

		/* Find dest tile */
		if (src_tile.IsTileType(TileTypes.MP_TUNNELBRIDGE) && BitOps.GB(src_tile.getMap().m5, 4, 4) == 0 &&
				(DiagDirection)BitOps.GB(src_tile.getMap().m5, 0, 2) == src_exitdir) {
			/* This is a tunnel. We know this tunnel is our type,
			 * otherwise we wouldn't have got here. It is also facing us,
			 * so we should skip it's body */
			flotr = FindLengthOfTunnel(src_tile, src_exitdir);
			dst_tile = flotr.tile;
		} else {
			if (type != TRANSPORT_WATER && (src_tile.IsRoadStationTile() || src_tile.IsTileDepotType(type)))
			{
				/* This is a road station or a train or road depot. We can enter and exit
				 * those from one side only. Trackdirs don't support that (yet), so we'll
				 * do this here. */

				DiagDirection exitdir;
				/* Find out the exit direction first */
				if (src_tile.IsRoadStationTile())
					exitdir = GetRoadStationDir(src_tile);
				else /* Train or road depot. Direction is stored the same for both, in map5 */
					exitdir = GetDepotDirection(src_tile, type);

				/* Let's see if were headed the right way into the depot, and reverse
				 * otherwise (only for trains, since only with trains you can
				 * (sometimes) reach tiles after reversing that you couldn't reach
				 * without reversing. */
				if (src_trackdir == DiagdirToDiagTrackdir(ReverseDiagdir(exitdir)) && type == TRANSPORT_RAIL)
					/* We are headed inwards. We can only reverse here, so we'll not
					 * consider this direction, but jump ahead to the reverse direction.
					 * It would be nicer to return one neighbour here (the reverse
					 * trackdir of the one we are considering now) and then considering
					 * that one to return the tracks outside of the depot. But, because
					 * the code layout is cleaner this way, we will just pretend we are
					 * reversed already */
					src_trackdir = ReverseTrackdir(src_trackdir);
			}
			/* This a normal tile, a bridge, a tunnel exit, etc. */
			dst_tile = AddTileIndexDiffCWrap(src_tile, TileIndexDiffCByDir(TrackdirToExitdir(src_trackdir)));
			if (dst_tile == INVALID_TILE) {
				/* We reached the border of the map */
				/* TODO Nicer control flow for this */
				return;
			}
		}

		/* I can't enter a tunnel entry/exit tile from a tile above the tunnel. Note
		 * that I can enter the tunnel from a tile below the tunnel entrance. This
		 * solves the problem of vehicles wanting to drive off a tunnel entrance */
		if (dst_tile.IsTileType(TileTypes.MP_TUNNELBRIDGE) && BitOps.GB(dst_tile.getMap().m5, 4, 4) == 0 &&
				dst_tile.GetTileZ() < src_tile.GetTileZ()) {
			return;
		}

		/* check correct rail type (mono, maglev, etc) */
		if (type == TRANSPORT_RAIL) {
			RailType dst_type = dst_tile.GetTileRailType(src_trackdir);
			if (!IsCompatibleRail(aystar.user_data[NPF_RAILTYPE], dst_type))
				return;
		}

		/* Check the owner of the tile */
		if (!VehicleMayEnterTile(aystar.user_data[NPF_OWNER], dst_tile, TrackdirToExitdir(src_trackdir))) {
			return;
		}

		/* Determine available tracks */
		if (type != TRANSPORT_WATER && (dst_tile.IsRoadStationTile() || dst_tile.IsTileDepotType(type)))
		{
			/* Road stations and road and train depots return 0 on GTTS, so we have to do this by hand... */
			DiagDirection exitdir;
			if (dst_tile.IsRoadStationTile())
				exitdir = dst_tile.GetRoadStationDir();
			else /* Road or train depot */
				exitdir = GetDepotDirection(dst_tile, type);
			/* Find the trackdirs that are available for a depot or station with this
			 * orientation. They are only "inwards", since we are reaching this tile
			 * from some other tile. This prevents vehicles driving into depots from
			 * the back */
			ts = TrackdirToTrackdirBits(DiagdirToDiagTrackdir(ReverseDiagdir(exitdir)));
		} else {
			ts = GetTileTrackStatus(dst_tile, type);
		}
		trackdirbits = ts & TRACKDIR_BIT_MASK; /* Filter out signal status and the unused bits */

		DEBUG_npf( 4, "Next node: (%d, %d) [%d], possible trackdirs: %#x", dst_tile.TileX(), dst_tile.TileY(), dst_tile.getTile(), trackdirbits);
		/* Select only trackdirs we can reach from our current trackdir */
		trackdirbits &= TrackdirReachesTrackdirs(src_trackdir);
		if (Global._patches.forbid_90_deg && (type == TRANSPORT_RAIL || type == TRANSPORT_WATER)) /* Filter out trackdirs that would make 90 deg turns for trains */

		trackdirbits &= ~TrackdirCrossesTrackdirs(src_trackdir);

		if (BitOps.KillFirstBit2x64(trackdirbits) != 0)
			NPFSetFlag(current.path.node, NPF_FLAG_PBS_CHOICE, true);

		/* When looking for 'any' route, ie when already inside a pbs block, discard all tracks that would cross
		   other reserved tracks, so we *always* will find a valid route if there is one */
		if (!(NPFGetFlag(current.path.node, NPF_FLAG_PBS_EXIT)) && (aystar.user_data[NPF_PBS_MODE] == PBS_MODE_ANY))
			trackdirbits &= ~PBSTileUnavail(dst_tile);

		DEBUG_npf(6,"After filtering: (%d, %d), possible trackdirs: %#x", dst_tile.TileX(), dst_tile.TileY(), trackdirbits);

		i = 0;
		/* Enumerate possible track */
		while (trackdirbits != 0) 
		{
			Trackdir dst_trackdir;
			dst_trackdir =  FindFirstBit2x64(trackdirbits);
			trackdirbits = KillFirstBit2x64(trackdirbits);
			DEBUG_npf( 5, "Expanded into trackdir: %d, remaining trackdirs: %#x", dst_trackdir, trackdirbits);

			/* Check for oneway signal against us */
			if (dst_tile.IsTileType(TileTypes.MP_RAILWAY) && dst_tile.GetRailTileType() == RAIL_TYPE_SIGNALS) {
				if (HasSignalOnTrackdir(dst_tile, ReverseTrackdir(dst_trackdir)) && !HasSignalOnTrackdir(dst_tile, dst_trackdir))
					// if one way signal not pointing towards us, stop going in this direction.
					break;
			}
			{
				/* We've found ourselves a neighbour :-) */
				AyStarNode  neighbour = aystar.neighbours[i];
				neighbour.tile = dst_tile;
				neighbour.direction = dst_trackdir;
				/* Save user data */
				neighbour.user_data[NPF_NODE_FLAGS] = current.path.node.user_data[NPF_NODE_FLAGS];
				NPFFillTrackdirChoice(neighbour, current);
			}
			i++;
		}
		aystar.num_neighbours = i;
	}

	/*
	 * Plan a route to the specified target (which is checked by target_proc),
	 * from start1 and if not null, from start2 as well. The type of transport we
	 * are checking is in type. reverse_penalty is applied to all routes that
	 * originate from the second start node.
	 * When we are looking for one specific target (optionally multiple tiles), we
	 * should use a good heuristic to perform aystar search. When we search for
	 * multiple targets that are spread around, we should perform a breadth first
	 * search by specifiying CalcZero as our heuristic.
	 */
	static NPFFoundTargetData NPFRouteInternal(AyStarNode  start1, AyStarNode  start2, NPFFindStationOrTileData  target, AyStar_EndNodeCheck target_proc, AyStar_CalculateH heuristic_proc, TransportType type, Owner owner, RailType railtype, int reverse_penalty, byte pbs_mode)
	{
		int r;
		NPFFoundTargetData result;

		/* Initialize procs */
		_npf_aystar.CalculateH = heuristic_proc;
		_npf_aystar.EndNodeCheck = target_proc;
		_npf_aystar.FoundEndNode = NPFSaveTargetData;
		_npf_aystar.GetNeighbours = NPFFollowTrack;
		if (type == TRANSPORT_RAIL)
			_npf_aystar.CalculateG = NPFRailPathCost;
		else if (type == TRANSPORT_ROAD)
			_npf_aystar.CalculateG = NPFRoadPathCost;
		else if (type == TRANSPORT_WATER)
			_npf_aystar.CalculateG = NPFWaterPathCost;
		else
			assert(0);

		if (pbs_mode != PBS_MODE_NONE)
			_npf_aystar.BeforeExit = NPFReservePBSPath;
		else
			_npf_aystar.BeforeExit = null;

		/* Initialize Start Node(s) */
		start1.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
		start1.user_data[NPF_NODE_FLAGS] = 0;
		_npf_aystar.addstart(&_npf_aystar, start1, 0);
		if (start2) {
			start2.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
			start2.user_data[NPF_NODE_FLAGS] = 0;
			NPFSetFlag(start2, NPF_FLAG_REVERSE, true);
			_npf_aystar.addstart(&_npf_aystar, start2, reverse_penalty);
		}

		/* Initialize result */
		result.best_bird_dist = (int)-1;
		result.best_path_dist = (int)-1;
		result.best_trackdir = INVALID_TRACKDIR;
		_npf_aystar.user_path = &result;

		/* Initialize target */
		_npf_aystar.user_target = target;

		/* Initialize user_data */
		_npf_aystar.user_data[NPF_TYPE] = type;
		_npf_aystar.user_data[NPF_OWNER] = owner;
		_npf_aystar.user_data[NPF_RAILTYPE] = railtype;
		_npf_aystar.user_data[NPF_PBS_MODE] = pbs_mode;

		/* GO! */
		r = AyStarMain_Main(&_npf_aystar);
		assert(r != AYSTAR_STILL_BUSY);

		if (result.best_bird_dist != 0) {
			if (target) {
				DEBUG(misc, 1) ("NPF: Could not find route to 0x%x from 0x%x.", target.dest_coords, start1.tile);
			} else {
				/* Assumption: target == null, so we are looking for a depot */
				DEBUG(misc, 1) ("NPF: Could not find route to a depot from 0x%x.", start1.tile);
			}

		}
		return result;
	}

	NPFFoundTargetData NPFRouteToStationOrTileTwoWay(TileIndex tile1, Trackdir trackdir1, TileIndex tile2, Trackdir trackdir2, NPFFindStationOrTileData  target, TransportType type, Owner owner, RailType railtype, byte pbs_mode)
	{
		AyStarNode start1;
		AyStarNode start2;

		start1.tile = tile1;
		start2.tile = tile2;
		/* We set this in case the target is also the start tile, we will just
		 * return a not found then */
		start1.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
		start1.direction = trackdir1;
		start2.direction = trackdir2;
		start2.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;

		return NPFRouteInternal(&start1, (IsValidTile(tile2) ? &start2 : null), target, NPFFindStationOrTile, NPFCalcStationOrTileHeuristic, type, owner, railtype, 0, pbs_mode);
	}

	NPFFoundTargetData NPFRouteToStationOrTile(TileIndex tile, Trackdir trackdir, NPFFindStationOrTileData  target, TransportType type, Owner owner, RailType railtype, byte pbs_mode)
	{
		return NPFRouteToStationOrTileTwoWay(tile, trackdir, INVALID_TILE, 0, target, type, owner, railtype, pbs_mode);
	}

	NPFFoundTargetData NPFRouteToDepotBreadthFirstTwoWay(TileIndex tile1, Trackdir trackdir1, TileIndex tile2, Trackdir trackdir2, TransportType type, Owner owner, RailType railtype, int reverse_penalty)
	{
		AyStarNode start1;
		AyStarNode start2;

		start1.tile = tile1;
		start2.tile = tile2;
		/* We set this in case the target is also the start tile, we will just
		 * return a not found then */
		start1.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
		start1.direction = trackdir1;
		start2.direction = trackdir2;
		start2.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;

		/* perform a breadth first search. Target is null,
		 * since we are just looking for any depot...*/
		return NPFRouteInternal(&start1, (IsValidTile(tile2) ? &start2 : null), null, NPFFindDepot, NPFCalcZero, type, owner, railtype, reverse_penalty, PBS_MODE_NONE);
	}

	NPFFoundTargetData NPFRouteToDepotBreadthFirst(TileIndex tile, Trackdir trackdir, TransportType type, Owner owner, RailType railtype)
	{
		return NPFRouteToDepotBreadthFirstTwoWay(tile, trackdir, INVALID_TILE, 0, type, owner, railtype, 0);
	}

	NPFFoundTargetData NPFRouteToDepotTrialError(TileIndex tile, Trackdir trackdir, TransportType type, Owner owner, RailType railtype)
	{
		/* Okay, what we're gonna do. First, we look at all depots, calculate
		 * the manhatten distance to get to each depot. We then sort them by
		 * distance. We start by trying to plan a route to the closest, then
		 * the next closest, etc. We stop when the best route we have found so
		 * far, is shorter than the manhattan distance. This will obviously
		 * always find the closest depot. It will probably be most efficient
		 * for ships, since the heuristic will not be to far off then. I hope.
		 */
		Queue depots;
		int r;
		NPFFoundTargetData best_result;
		NPFFoundTargetData result;
		NPFFindStationOrTileData target;
		AyStarNode start;
		Depot  current;
		Depot depot;

		init_InsSort(&depots);
		/* Okay, let's find all depots that we can use first */
		FOR_ALL_DEPOTS(depot) {
			/* Check if this is really a valid depot, it is of the needed type and
			 * owner */
			if (IsValidDepot(depot) && IsTileDepotType(depot.xy, type) && IsTileOwner(depot.xy, owner))
				/* If so, let's add it to the queue, sorted by distance */
				depots.push(&depots, depot, DistanceManhattan(tile, depot.xy));
		}

		/* Now, let's initialise the aystar */

		/* Initialize procs */
		_npf_aystar.CalculateH = NPFCalcStationOrTileHeuristic;
		_npf_aystar.EndNodeCheck = NPFFindStationOrTile;
		_npf_aystar.FoundEndNode = NPFSaveTargetData;
		_npf_aystar.GetNeighbours = NPFFollowTrack;
		if (type == TRANSPORT_RAIL)
			_npf_aystar.CalculateG = NPFRailPathCost;
		else if (type == TRANSPORT_ROAD)
			_npf_aystar.CalculateG = NPFRoadPathCost;
		else if (type == TRANSPORT_WATER)
			_npf_aystar.CalculateG = NPFWaterPathCost;
		else
			assert(0);

		_npf_aystar.BeforeExit = null;

		/* Initialize target */
		target.station_index = -1; /* We will initialize dest_coords inside the loop below */
		_npf_aystar.user_target = &target;

		/* Initialize user_data */
		_npf_aystar.user_data[NPF_TYPE] = type;
		_npf_aystar.user_data[NPF_OWNER] = owner;
		_npf_aystar.user_data[NPF_PBS_MODE] = PBS_MODE_NONE;

		/* Initialize Start Node */
		start.tile = tile;
		start.direction = trackdir; /* We will initialize user_data inside the loop below */

		/* Initialize Result */
		_npf_aystar.user_path = &result;
		best_result.best_path_dist = (int)-1;
		best_result.best_bird_dist = (int)-1;

		/* Just iterate the depots in order of increasing distance */
		while ((current = depots.pop(&depots))) {
			/* Check to see if we already have a path shorter than this
			 * depot's manhattan distance. HACK: We call DistanceManhattan
			 * again, we should probably modify the queue to give us that
			 * value... */
			if ( DistanceManhattan(tile, current.xy * NPF_TILE_LENGTH) > best_result.best_path_dist)
				break;

			/* Initialize Start Node */
			/* We set this in case the target is also the start tile, we will just
			 * return a not found then */
			start.user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
			start.user_data[NPF_NODE_FLAGS] = 0;
			_npf_aystar.addstart(&_npf_aystar, &start, 0);

			/* Initialize result */
			result.best_bird_dist = (int)-1;
			result.best_path_dist = (int)-1;
			result.best_trackdir = INVALID_TRACKDIR;

			/* Initialize target */
			target.dest_coords = current.xy;

			/* GO! */
			r = AyStarMain_Main(&_npf_aystar);
			assert(r != AYSTAR_STILL_BUSY);

			/* This depot is closer */
			if (result.best_path_dist < best_result.best_path_dist)
				best_result = result;
		}
		if (result.best_bird_dist != 0) {
			DEBUG(misc, 1) ("NPF: Could not find route to any depot from 0x%x.", tile);
		}
		return best_result;
	}

	void InitializeNPF()
	{
		init_AyStar(&_npf_aystar, NPFHash, NPF_HASH_SIZE);
		_npf_aystar.loops_per_tick = 0;
		_npf_aystar.max_path_cost = 0;
		//_npf_aystar.max_search_nodes = 0;
		/* We will limit the number of nodes for now, until we have a better
		 * solution to really fix performance */
		_npf_aystar.max_search_nodes = Global._patches.npf_max_search_nodes;
	}

	void NPFFillWithOrderData(NPFFindStationOrTileData  fstd, Vehicle  v)
	{
		/* Ships don't really reach their stations, but the tile in front. So don't
		 * save the station id for ships. For roadvehs we don't store it either,
		 * because multistop depends on vehicles actually reaching the exact
		 * dest_tile, not just any stop of that station.
		 * So only for train orders to stations we fill fstd.station_index, for all
		 * others only dest_coords */
		if ((v.current_order.type) == OT_GOTO_STATION && v.type == Vehicle.VEH_Train) {
			fstd.station_index = v.current_order.station;
			/* Let's take the closest tile of the station as our target for trains */
			fstd.dest_coords = CalcClosestStationTile(v.current_order.station, v.tile);
		} else {
			fstd.dest_coords = v.dest_tile;
			fstd.station_index = -1;
		}
	}
	
	
	
	
}
