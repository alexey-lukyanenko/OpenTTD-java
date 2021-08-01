package game;

import java.util.Iterator;

import game.struct.RailtypeSlowdownParams;
import game.tables.TrainTables;
import game.util.BitOps;

public class TrainCmd extends TrainTables 
{

	/**
	 * Recalculates the cached stuff of a train. Should be called each time a vehicle is added
	 * to/removed from the chain, and when the game is loaded.
	 * Note: this needs to be called too for 'wagon chains' (in the depot, without an engine)
	 * @param v First vehicle of the chain.
	 */
	public static void TrainConsistChanged(Vehicle  v)
	{
		final RailVehicleInfo rvi_v;
		Vehicle u;
		int max_speed = 0xFFFF;
		int power = 0;
		EngineID first_engine;

		assert(v.type == Vehicle.VEH_Train);

		assert(v.IsFrontEngine() || v.IsFreeWagon());

		rvi_v = Engine.RailVehInfo(v.engine_type.id);
		first_engine = v.IsFrontEngine() ? v.engine_type : EngineID.getInvalid();
		v.rail.cached_total_length = 0;

		for (u = v; u != null; u = u.next) {
			final RailVehicleInfo rvi_u = Engine.RailVehInfo(u.engine_type.id);
			int veh_len;

			// update the 'first engine'
			u.rail.first_engine = (v == u) ? EngineID.getInvalid() : first_engine;

			if (rvi_u.visual_effect != 0) {
				u.rail.cached_vis_effect = rvi_u.visual_effect;
			} else {
				if (u.IsTrainWagon() || u.IsArticulatedPart()) {
					// Wagons and articulated parts have no effect by default
					u.rail.cached_vis_effect = 0x40;
				} else if (rvi_u.engclass == 0) {
					// Steam is offset by -4 units
					u.rail.cached_vis_effect = 4;
				} else {
					// Diesel fumes and sparks come from the centre
					u.rail.cached_vis_effect = 8;
				}
			}

			if (!u.IsArticulatedPart()) {
				// power is the sum of the powers of all engines and powered wagons in the consist
				power += rvi_u.power;

				// check if its a powered wagon
				u.rail.flags = BitOps.RETCLRBIT(u.rail.flags, Vehicle.VRF_POWEREDWAGON);
				if ((rvi_v.pow_wag_power != 0) && 0 != (rvi_u.flags & Engine.RVI_WAGON) && UsesWagonOverride(u)) {
					if (BitOps.HASBIT(rvi_u.callbackmask, CBM_WAGON_POWER)) {
						int callback = Engine.GetCallBackResult(CBID_WAGON_POWER,  u.engine_type, u);

						if (callback != CALLBACK_FAILED)
							u.rail.cached_vis_effect = callback;
					}

					if (u.rail.cached_vis_effect < 0x40) {
						/* wagon is powered */
						u.rail.flags = BitOps.RETSETBIT(u.rail.flags, Vehicle.VRF_POWEREDWAGON); // cache 'powered' status
						power += rvi_v.pow_wag_power;
					}
				}

				// max speed is the minimum of the speed limits of all vehicles in the consist
				if (0 != (rvi_u.flags & Engine.RVI_WAGON) || Global._patches.wagon_speed_limits)
					if (rvi_u.max_speed != 0 && !UsesWagonOverride(u))
						max_speed = Math.min(rvi_u.max_speed, max_speed);
			}

			// check the vehicle length (callback)
			veh_len = CALLBACK_FAILED;
			if (BitOps.HASBIT(rvi_u.callbackmask, CBM_Vehicle.VEH_LENGTH))
				veh_len = Engine.GetCallBackResult(CBID_VEH_LENGTH,  u.engine_type, u);
			if (veh_len == CALLBACK_FAILED)
				veh_len = rvi_u.shorten_factor;
			veh_len = BitOps.clamp(veh_len, 0, u.next == null ? 7 : 5); // the clamp on vehicles not the last in chain is stricter, as too short wagons can break the 'follow next vehicle' code
			u.rail.cached_veh_length = 8 - veh_len;
			v.rail.cached_total_length += u.rail.cached_veh_length;

		};

		// store consist weight/max speed in cache
		v.rail.cached_max_speed = max_speed;
		v.rail.cached_power = power;

		// recalculate cached weights too (we do this *after* the rest, so it is known which wagons are powered and need extra weight added)
		Train.TrainCargoChanged(v);
	}

	//enum AccelType 
	//{
	public static final int AM_ACCEL = 0;
	public static final int AM_BRAKE = 1;
	//};

	static boolean TrainShouldStop(final Vehicle  v, TileIndex tile)
	{
		final Order  o = v.current_order;

		assert(v.type == Vehicle.VEH_Train);
		assert(v.tile.IsTileType( TileTypes.MP_STATION));
		//When does a train drive through a station
		//first we deal with the "new nonstop handling"
		if (Global._patches.new_nonstop && 0 != (o.flags & Order.OF_NON_STOP) &&
				tile.getMap().m2 == o.station )
			return false;

		if (v.last_station_visited == tile.getMap().m2)
			return false;

		if (tile.getMap().m2 != o.station &&
				(0 != (o.flags & Order.OF_NON_STOP) || Global._patches.new_nonstop))
			return false;

		return true;
	}

	//new acceleration
	static int GetTrainAcceleration(Vehicle v, int mode)
	{
		final Vehicle u;
		int num = 0;	//number of vehicles, change this into the number of axles later
		int power = 0;
		int mass = 0;
		int max_speed = 2000;
		int area = 120;
		int friction = 35; //[1e-3]
		int drag_coeff = 20;	//[1e-4]
		int incl = 0;
		int resistance;
		int speed = v.cur_speed; //[mph]
		int force = 0x3FFFFFFF;
		int pos = 0;
		int lastpos = -1;
		int curvecount[] = {0, 0};
		int sum = 0;
		int numcurve = 0;

		speed *= 10;
		speed /= 16;

		//first find the curve speed limit
		for (u = v; u.next != null; u = u.next, pos++) {
			int dir = u.direction;
			int ndir = u.next.direction;
			int i;

			for (i = 0; i < 2; i++) {
				if ( _curve_neighbours45[dir][i] == ndir) {
					curvecount[i]++;
					if (lastpos != -1) {
						numcurve++;
						sum += pos - lastpos;
						if (pos - lastpos == 1) {
							max_speed = 88;
						}
					}
					lastpos = pos;
				}
			}

			//if we have a 90 degree turn, fix the speed limit to 60
			if (_curve_neighbours90[dir][0] == ndir ||
					_curve_neighbours90[dir][1] == ndir) {
				max_speed = 61;
			}
		}

		if (numcurve > 0) sum /= numcurve;

		if ((curvecount[0] != 0 || curvecount[1] != 0) && max_speed > 88) {
			int total = curvecount[0] + curvecount[1];

			if (curvecount[0] == 1 && curvecount[1] == 1) {
				max_speed = 0xFFFF;
			} else if (total > 1) {
				max_speed = 232 - (13 - BitOps.clamp(sum, 1, 12)) * (13 - BitOps.clamp(sum, 1, 12));
			}
		}

		max_speed += (max_speed / 2) * v.rail.railtype;

		if (v.tile.IsTileType( TileTypes.MP_STATION) && v.IsFrontEngine()) {
			if (TrainShouldStop(v, v.tile)) {
				int station_length = 0;
				TileIndex tile = v.tile;
				int delta_v;

				max_speed = 120;
				do {
					station_length++;
					tile = tile.iadd( TileIndex.TileOffsByDir(v.direction / 2) );
				} while (tile.IsCompatibleTrainStationTile( v.tile));

				delta_v = v.cur_speed / (station_length + 1);
				if (v.max_speed > (v.cur_speed - delta_v))
					max_speed = v.cur_speed - (delta_v / 10);

				max_speed = Math.max(max_speed, 25 * station_length);
			}
		}

		mass = v.rail.cached_weight;
		power = v.rail.cached_power * 746;
		max_speed = Math.min(max_speed, v.rail.cached_max_speed);

		for (u = v; u != null; u = u.next) {
			num++;
			drag_coeff += 3;

			if (u.rail.track == 0x80)
				max_speed = Math.min(61, max_speed);

			if (BitOps.HASBIT(u.rail.flags, Vehicle.VRF_GOINGUP)) {
				incl += u.rail.cached_veh_weight * 60;		//3% slope, quite a bit actually
			} else if (BitOps.HASBIT(u.rail.flags, Vehicle.VRF_GOINGDOWN)) {
				incl -= u.rail.cached_veh_weight * 60;
			}
		}

		v.max_speed = max_speed;

		if (v.rail.railtype != RAILTYPE_MAGLEV) {
			resistance = 13 * mass / 10;
			resistance += 60 * num;
			resistance += friction * mass * speed / 1000;
			resistance += (area * drag_coeff * speed * speed) / 10000;
		} else
			resistance = (area * (drag_coeff / 2) * speed * speed) / 10000;
		resistance += incl;
		resistance *= 4; //[N]

		if (speed > 0) {
			switch (v.rail.railtype) {
			case RAILTYPE_RAIL:
			case RAILTYPE_MONO:
				force = power / speed; //[N]
				force *= 22;
				force /= 10;
				break;

			case RAILTYPE_MAGLEV:
				force = power / 25;
				break;
			}
		} else {
			//"kickoff" acceleration
			force = (mass * 8) + resistance;
		}

		if (force <= 0) force = 10000;

		if (v.rail.railtype != RAILTYPE_MAGLEV) force = Math.min(force, mass * 10 * 200);

		if (mode == AM_ACCEL) {
			return (force - resistance) / (mass * 4);
		} else {
			return Math.min((-force - resistance) / (mass * 4), 10000 / (mass * 4));
		}
	}

	static void UpdateTrainAcceleration(Vehicle v)
	{
		int power = 0;
		int weight = 0;

		assert(v.IsFrontEngine());

		weight = v.rail.cached_weight;
		power = v.rail.cached_power;
		v.max_speed = v.rail.cached_max_speed;

		assert(weight != 0);

		v.acceleration = BitOps.clamp(power / weight * 4, 1, 255);
	}

	public static int GetTrainImage(final Vehicle v, int direction)
	{
		int img = v.spritenum;
		int base;

		if (is_custom_sprite(img)) {
			base = GetCustomVehicleSprite(v, direction + 4 * IS_CUSTOM_SECONDHEAD_SPRITE(img));
			if (base != 0) return base;
			img = orig_rail_vehicle_info[v.engine_type].image_index;
		}

		base = _engine_sprite_base[img] + ((direction + _engine_sprite_add[img]) & _engine_sprite_and[img]);

		if (v.cargo_count >= v.cargo_cap / 2) base += _wagon_full_adder[img];
		return base;
	}

	//extern int _traininfo_vehicle_pitch;

	static void DrawTrainEngine(int x, int y, EngineID engine, int image_ormod)
	{
		final RailVehicleInfo rvi = RailVehInfo(engine);

		int img = rvi.image_index;
		int image = 0;

		if (is_custom_sprite(img)) {
			image = GetCustomVehicleIcon(engine, 6);
			if (image == 0) {
				img = orig_rail_vehicle_info[engine].image_index;
			} else {
				y += _traininfo_vehicle_pitch;
			}
		}
		if (image == 0) {
			image = (6 & _engine_sprite_and[img]) + _engine_sprite_base[img];
		}

		if (rvi.flags & Engine.RVI_MULTIHEAD) {
			Gfx.DrawSprite(image | image_ormod, x - 14, y);
			x += 15;
			image = 0;
			if (is_custom_sprite(img)) {
				image = GetCustomVehicleIcon(engine, 2);
				if (image == 0) img = orig_rail_vehicle_info[engine].image_index;
			}
			if (image == 0) {
				image =
						((6 + _engine_sprite_add[img + 1]) & _engine_sprite_and[img + 1]) +
						_engine_sprite_base[img + 1];
			}
		}
		Gfx.DrawSprite(image | image_ormod, x, y);
	}

	static int CountArticulatedParts(final RailVehicleInfo rvi, EngineID engine_type)
	{
		int callback;
		int i;

		if (!BitOps.HASBIT(rvi.callbackmask, CBM_ARTIC_ENGINE)) return 0;

		for (i = 1; i < 10; i++) {
			callback = GetCallBackResult(CBID_ARTIC_ENGINE + (i << 8), engine_type, null);
			if (callback == CALLBACK_FAILED || callback == 0xFF) break;
		}

		return i - 1;
	}

	static void AddArticulatedParts(final RailVehicleInfo rvi, Vehicle [] vl)
	{
		final RailVehicleInfo rvi_artic;
		int engine_type;
		Vehicle v = vl[0];
		Vehicle u = v;
		int callback;
		boolean flip_image;
		int i;

		if (!BitOps.HASBIT(rvi.callbackmask, CBM_ARTIC_ENGINE))
			return;

		for (i = 1; i < 10; i++) {
			callback = GetCallBackResult(CBID_ARTIC_ENGINE + (i << 8), v.engine_type, null);
			if (callback == CALLBACK_FAILED || callback == 0xFF)
				return;

			u.next = vl[i];
			u = u.next;

			engine_type = BitOps.GB(callback, 0, 7);
			flip_image = BitOps.HASBIT(callback, 7);
			rvi_artic = RailVehInfo(engine_type);

			// get common values from first engine
			u.direction = v.direction;
			u.owner = v.owner;
			u.tile = v.tile;
			u.x_pos = v.x_pos;
			u.y_pos = v.y_pos;
			u.z_pos = v.z_pos;
			u.z_height = v.z_height;
			u.rail.track = v.rail.track;
			u.rail.railtype = v.rail.railtype;
			u.build_year = v.build_year;
			u.vehstatus = v.vehstatus & ~Vehicle.VS_STOPPED;
			u.rail.first_engine = v.engine_type;

			// get more settings from rail vehicle info
			u.spritenum = rvi_artic.image_index;
			if (flip_image) u.spritenum++;
			u.cargo_type = rvi_artic.cargo_type;
			u.cargo_cap = rvi_artic.capacity;
			u.max_speed = 0;
			u.max_age = 0;
			u.engine_type = engine_type;
			u.value = 0;
			u.type = Vehicle.VEH_Train;
			u.subtype = 0;
			u.SetArticulatedPart();
			u.cur_image = 0xAC2;
			u.random_bits = VehicleRandomBits();

			u.VehiclePositionChanged());
		}
	}

	public static int CmdBuildRailWagon(EngineID engine, TileIndex tile, int flags)
	{
		int value;
		final RailVehicleInfo rvi;
		int num_vehicles;

		Player.SET_EXPENSES_TYPE(Player.EXPENSES_NEW_VEHICLES);

		rvi = RailVehInfo(engine);
		value = (rvi.base_cost * Global._price.build_railwagon) >> 8;

			num_vehicles = 1 + CountArticulatedParts(rvi, engine);

			if (!(flags & Cmd.DC_QUERY_COST)) {
				Vehicle vl = new Vehicle[11]; // Allow for wagon and upto 10 artic parts.
				Vehicle  v;
				int x;
				int y;

				if (!Vehicle.AllocateVehicles(vl, num_vehicles))
					return Cmd.return_cmd_error(Str.STR_00E1_TOO_MANY_VEHICLES_IN_GAME);

				if (flags & Cmd.DC_EXEC) {
					Vehicle u, w;
					int dir;

					v = vl[0];
					v.spritenum = rvi.image_index;

					u = null;

					//FOR_ALL_VEHICLES(w) 
					Iterator<Vehicle> it = Vehicle.getIterator();
					while(it.hasNext())
					{
						final Vehicle  w = it.next();

						if (w.type == Vehicle.VEH_Train && w.tile == tile &&
								w.IsFreeWagon() && w.engine_type == engine) {
							u = GetLastVehicleInChain(w);
							break;
						}
					}

					v.engine_type = engine;

					dir = BitOps.GB(tile.getMap().m5, 0, 2);

					v.direction = dir * 2 + 1;
					v.tile = tile;

					x = TileX(tile) * TILE_SIZE | _vehicle_initial_x_fract[dir];
					y = TileY(tile) * TILE_SIZE | _vehicle_initial_y_fract[dir];

					v.x_pos = x;
					v.y_pos = y;
					v.z_pos = Landscape.GetSlopeZ(x,y);
					v.owner = Global._current_player;
					v.z_height = 6;
					v.rail.track = 0x80;
					v.vehstatus = Vehicle.VS_HIDDEN | Vehicle.VS_DEFPAL;

					v.subtype = 0;
					v.SetTrainWagon();
					if (u != null) {
						u.next = v;
					} else {
						v.SetFreeWagon();
					}

					v.cargo_type = rvi.cargo_type;
					v.cargo_cap = rvi.capacity;
					v.value = value;
					//			v.day_counter = 0;

					v.rail.railtype = GetEngine(engine).railtype;

					v.build_year = _cur_year;
					v.type = Vehicle.VEH_Train;
					v.cur_image = 0xAC2;
					v.random_bits = VehicleRandomBits();

					AddArticulatedParts(rvi, vl);

					_new_wagon_id = v.index;
					_new_vehicle_id = v.index;

					v.VehiclePositionChanged();
					TrainConsistChanged(v.GetFirstVehicleInChain());

					Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);
				}
			}

			return value;
	}

	// Move all free vehicles in the depot to the train
	static void NormalizeTrainVehInDepot(final Vehicle  u)
	{


		//FOR_ALL_VEHICLES(v)
		Iterator<Vehicle> it = Vehicle.getIterator();
		while(it.hasNext())
		{
			final Vehicle  v = it.next();
			if (v.type == Vehicle.VEH_Train && v.IsFreeWagon() &&
					v.tile == u.tile &&
					v.rail.track == 0x80) {
				if (Cmd.CmdFailed(Cmd.DoCommandByTile(0, v.index | (u.index << 16), 1, Cmd.DC_EXEC,
						Cmd.CMD_MOVE_RAIL_VEHICLE)))
					break;
			}
		}
	}



	static int EstimateTrainCost(final RailVehicleInfo  rvi)
	{
		return (rvi.base_cost * (Global._price.build_railvehicle >> 3)) >> 5;
	}

	void AddRearEngineToMultiheadedTrain(Vehicle v, Vehicle u, boolean building)
	{
		u.direction = v.direction;
		u.owner = v.owner;
		u.tile = v.tile;
		u.x_pos = v.x_pos;
		u.y_pos = v.y_pos;
		u.z_pos = v.z_pos;
		u.z_height = 6;
		u.rail.track = 0x80;
		u.vehstatus = v.vehstatus & ~Vehicle.VS_STOPPED;
		u.subtype = 0;
		u.SetMultiheaded();
		u.spritenum = v.spritenum + 1;
		u.cargo_type = v.cargo_type;
		u.cargo_cap = v.cargo_cap;
		u.rail.railtype = v.rail.railtype;
		if (building) v.next = u;
		u.engine_type = v.engine_type;
		u.build_year = v.build_year;
		if (building) v.value >>= 1;
			u.value = v.value;
			u.type = Vehicle.VEH_Train;
			u.cur_image = 0xAC2;
			u.random_bits = VehicleRandomBits();
			u.VehiclePositionChanged();
	}

	/** Build a railroad vehicle.
	 * @param x,y tile coordinates (depot) where rail-vehicle is built
	 * @param p1 engine type id
	 * @param p2 bit 0 prevents any free cars from being added to the train
	 */
	public static int CmdBuildRailVehicle(int x, int y, int flags, int p1, int p2)
	{
		final RailVehicleInfo rvi;
		int value;
		Vehicle v;
		UnitID unit_num;
		Engine e;
		TileIndex tile = TileVirtXY(x, y);
		int num_vehicles;

		/* Check if the engine-type is valid (for the player) */
		if (!IsEngineBuildable(p1, Vehicle.VEH_Train)) return Cmd.CMD_ERROR;

		/* Check if the train is actually being built in a depot belonging
		 * to the player. Doesn't matter if only the cost is queried */
		if (!(flags & Cmd.DC_QUERY_COST)) {
			if (!IsTileDepotType(tile, TRANSPORT_RAIL)) return Cmd.CMD_ERROR;
			if (!tile.IsTileOwner( Global._current_player)) return Cmd.CMD_ERROR;
		}

		Player.SET_EXPENSES_TYPE(Player.EXPENSES_NEW_VEHICLES);

		rvi = RailVehInfo(p1);
		e = GetEngine(p1);

		/* Check if depot and new engine uses the same kind of tracks */
		if (!IsCompatibleRail(e.railtype, GetRailType(tile))) return Cmd.CMD_ERROR;

		if (rvi.flags & Engine.RVI_WAGON) return CmdBuildRailWagon(p1, tile, flags);

		value = EstimateTrainCost(rvi);

		num_vehicles = (rvi.flags & Engine.RVI_MULTIHEAD) ? 2 : 1;
		num_vehicles += CountArticulatedParts(rvi, p1);

		if (!(flags & Cmd.DC_QUERY_COST)) {
			Vehicle [] vl = new Vehicle[12]; // Allow for upto 10 artic parts and dual-heads
			if (!AllocateVehicles(vl, num_vehicles) || IsOrderPoolFull())
				return Cmd.return_cmd_error(Str.STR_00E1_TOO_MANY_VEHICLES_IN_GAME);

			v = vl[0];

			unit_num = GetFreeUnitNumber(Vehicle.VEH_Train);
			if (unit_num > Global._patches.max_trains)
				return Cmd.return_cmd_error(Str.STR_00E1_TOO_MANY_VEHICLES_IN_GAME);

			if (flags & Cmd.DC_EXEC) {
				int dir;

				v.unitnumber = unit_num;

				dir = BitOps.GB(tile.getMap().m5, 0, 2);

				v.direction = dir * 2 + 1;
				v.tile = tile;
				v.owner = Global._current_player;
				v.x_pos = (x |= _vehicle_initial_x_fract[dir]);
				v.y_pos = (y |= _vehicle_initial_y_fract[dir]);
				v.z_pos = Landscape.GetSlopeZ(x,y);
				v.z_height = 6;
				v.rail.track = 0x80;
				v.vehstatus = Vehicle.VS_HIDDEN | Vehicle.VS_STOPPED | Vehicle.VS_DEFPAL;
				v.spritenum = rvi.image_index;
				v.cargo_type = rvi.cargo_type;
				v.cargo_cap = rvi.capacity;
				v.max_speed = rvi.max_speed;
				v.value = value;
				v.last_station_visited = INVALID_STATION;
				v.dest_tile = 0;

				v.engine_type = p1;

				v.reliability = e.reliability;
				v.reliability_spd_dec = e.reliability_spd_dec;
				v.max_age = e.lifelength * 366;

				v.string_id = Str.STR_SV_TRAIN_NAME;
				v.rail.railtype = e.railtype;
				_new_train_id = v.index;
				_new_vehicle_id = v.index;

				v.service_interval = Global._patches.servint_trains;
				v.date_of_last_service = _date;
				v.build_year = _cur_year;
				v.type = Vehicle.VEH_Train;
				v.cur_image = 0xAC2;
				v.random_bits = VehicleRandomBits();

				v.subtype = 0;
				SetFrontEngine(v);
				SetTrainEngine(v);

				v.rail.shortest_platform[0] = 255;
				v.rail.shortest_platform[1] = 0;

				v.VehiclePositionChanged();

				if (rvi.flags & Engine.RVI_MULTIHEAD) {
					v.SetMultiheaded();
					AddRearEngineToMultiheadedTrain(vl[0], vl[1], true);
					/* Now we need to link the front and rear engines together
					 * other_multiheaded_part is the pointer that links to the other half of the engine
					 * vl[0] is the front and vl[1] is the rear
					 */
					vl[0].rail.other_multiheaded_part = vl[1];
					vl[1].rail.other_multiheaded_part = vl[0];
				} else {
					AddArticulatedParts(rvi, vl);
				}

				TrainConsistChanged(v);
				UpdateTrainAcceleration(v);

				if (!BitOps.HASBIT(p2, 0)) {	// check if the cars should be added to the new vehicle
					NormalizeTrainVehInDepot(v);
				}

				Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, tile);
				RebuildVehicleLists();
				Window.InvalidateWindow(Window.WC_COMPANY, v.owner);
				if (IsLocalPlayer()) {
					Window.InvalidateWindow(Window.WC_REPLACE_VEHICLE, Vehicle.VEH_Train); // updates the replace Train window
				}
			}
		}
		_cmd_build_rail_veh_score = _railveh_score[p1];

		return value;
	}


	/* Check if all the wagons of the given train are in a depot, returns the
	 * number of cars (including loco) then. If not, sets the error message to
	 * Str.STR_881A_TRAINS_CAN_ONLY_BE_ALTERED and returns -1 */
	static int CheckTrainStoppedInDepot(final Vehicle v)
	{
		int count;
		TileIndex tile = v.tile;

		/* check if stopped in a depot */
		if (!tile.IsTileDepotType(Global.TRANSPORT_RAIL) || v.cur_speed != 0) {
			Global._error_message = Str.STR_881A_TRAINS_CAN_ONLY_BE_ALTERED;
			return -1;
		}

		count = 0;
		for (; v != null; v = v.next) {
			count++;
			if (v.rail.track != 0x80 || v.tile != tile ||
					(v.IsFrontEngine() && !(v.vehstatus & Vehicle.VS_STOPPED))) {
				Global._error_message = Str.STR_881A_TRAINS_CAN_ONLY_BE_ALTERED;
				return -1;
			}
		}

		return count;
	}

	/**
	 * Unlink a rail wagon from the consist.
	 * @param v Vehicle to remove.
	 * @param first The first vehicle of the consist.
	 * @return The first vehicle of the consist.
	 */
	static Vehicle UnlinkWagon(Vehicle v, Vehicle first)
	{
		Vehicle u;

		// unlinking the first vehicle of the chain?
		if (v == first) {
			v = v.GetNextVehicle();
			if (v == null) return null;

			if (v.IsTrainWagon()) v.SetFreeWagon();

			return v;
		}

		for (u = first; u.GetNextVehicle() != v; u = u.GetNextVehicle()) {}
		u.GetLastEnginePart().next = v.GetNextVehicle();
		return first;
	}

	static Vehicle FindGoodVehiclePos(final Vehicle src)
	{
		//Vehicle dst;
		EngineID eng = src.engine_type;
		TileIndex tile = src.tile;

		//FOR_ALL_VEHICLES(dst) {
		Iterator<Vehicle> it = Vehicle.getIterator();
		while(it.hasNext())
		{
			final Vehicle  dst = it.next();
			if (dst.type == Vehicle.VEH_Train && dst.IsFreeWagon() &&
					dst.tile == tile) {
				// check so all vehicles in the line have the same engine.
				Vehicle v = dst;

				while (v.engine_type == eng) {
					v = v.next;
					if (v == null) return dst;
				}
			}
		}

		return null;
	}

	/*
	 * add a vehicle v behind vehicle dest
	 * use this function since it sets flags as needed
	 */
	static void AddWagonToConsist(Vehicle v, Vehicle dest)
	{
		UnlinkWagon(v, v.GetFirstVehicleInChain());
		if (dest == null) return;

		v.next = dest.next;
		dest.next = v;
		ClearFreeWagon(v);
		ClearFrontEngine(v);
	}

	/*
	 * move around on the train so rear engines are placed correctly according to the other engines
	 * always call with the front engine
	 */
	static void NormaliseTrainConsist(Vehicle v)
	{
		Vehicle u;

		if (v.IsFreeWagon()) return;

		assert(v.IsFrontEngine());

		for(; v != null; v = v.GetNextVehicle()) {
			if (!v.IsMultiheaded() || !v.IsTrainEngine()) continue;

			/* make sure that there are no free cars before next engine */
			for(u = v; u.next != null && !IsTrainEngine(u.next); u = u.next);

			if (u == v.rail.other_multiheaded_part) continue;
			AddWagonToConsist(v.rail.other_multiheaded_part, u);

		}
	}

	/** Move a rail vehicle around inside the depot.
	 * @param x,y unused
	 * @param p1 various bitstuffed elements
	 * - p1 (bit  0 - 15) source vehicle index
	 * - p1 (bit 16 - 31) what wagon to put the source wagon AFTER, XXX - INVALID_VEHICLE to make a new line
	 * @param p2 (bit 0) move all vehicles following the source vehicle
	 */
	public static int CmdMoveRailVehicle(int x, int y, int flags, int p1, int p2)
	{
		VehicleID s = BitOps.GB(p1, 0, 16);
		VehicleID d = BitOps.GB(p1, 16, 16);
		Vehicle src, dst, src_head, dst_head;

		if (!Vehicle.IsVehicleIndex(s)) return Cmd.CMD_ERROR;

		src = Vehicle.GetVehicle(s);

		if (src.type != Vehicle.VEH_Train) return Cmd.CMD_ERROR;

		// if nothing is selected as destination, try and find a matching vehicle to drag to.
		if (d == INVALID_VEHICLE) {
			dst = null;
			if (!src.IsTrainEngine()) dst = FindGoodVehiclePos(src);
		} else {
			dst = Vehicle.GetVehicle(d);
		}

		// if an articulated part is being handled, deal with its parent vehicle
		while (IsArticulatedPart(src)) src = GetPrevVehicleInChain(src);
		if (dst != null) {
			while (IsArticulatedPart(dst)) dst = GetPrevVehicleInChain(dst);
		}

		// don't move the same vehicle..
		if (src == dst) return 0;

		/* the player must be the owner */
		if (!CheckOwnership(src.owner) || (dst != null && !CheckOwnership(dst.owner)))
			return Cmd.CMD_ERROR;

		/* locate the head of the two chains */
		src_head = GetFirstVehicleInChain(src);
		dst_head = null;
		if (dst != null) {
			dst_head = GetFirstVehicleInChain(dst);
			// Now deal with articulated part of destination wagon
			dst = dst.GetLastEnginePart();
		}

		if (dst != null && IsMultiheaded(dst) && !IsTrainEngine(dst) && IsTrainWagon(src)) {
			/* We are moving a wagon to the rear part of a multiheaded engine */
			if (dst.next == null) {
				/* It's the last one, so we will add the wagon just before the rear engine */
				dst = GetPrevVehicleInChain(dst);
				// if dst is null, it means that dst got a rear multiheaded engine as first engine. We can't use that
				if (dst == null) return Cmd.CMD_ERROR;
			} else {
				/* there are more units on this train, so we will add the wagon after the next one*/
				dst = dst.next;
			}
		}

		if (src.IsTrainEngine() && dst_head != null) {
			/* we need to make sure that we didn't place it between a pair of multiheaded engines */
			Vehicle u, engine = null;

			for(u = dst_head; u != null; u = u.next) {
				if (u.IsTrainEngine() && u.IsMultiheaded() && u.rail.other_multiheaded_part != null) {
					engine = u;
				}
				if (engine != null && engine.rail.other_multiheaded_part == u) {
					engine = null;
				}
				if (u == dst) {
					if (engine != null) {
						dst = engine.rail.other_multiheaded_part;
					}
					break;
				}

			}
		}

		if (IsMultiheaded(src) && !src.IsTrainEngine()) return Cmd.return_cmd_error(Str.STR_REAR_ENGINE_FOLLOW_FRONT_ERROR);

		/* check if all vehicles in the source train are stopped inside a depot */
		if (CheckTrainStoppedInDepot(src_head) < 0) return Cmd.CMD_ERROR;

		/* check if all the vehicles in the dest train are stopped,
		 * and that the length of the dest train is no longer than XXX vehicles */
		if (dst_head != null) {
			int num = CheckTrainStoppedInDepot(dst_head);
			if (num < 0) return Cmd.CMD_ERROR;

			if (num > (Global._patches.mammoth_trains ? 100 : 9) && IsFrontEngine(dst_head))
				return Cmd.return_cmd_error(Str.STR_8819_TRAIN_TOO_LONG);

			assert(dst_head.tile == src_head.tile);
		}

		// when moving all wagons, we can't have the same src_head and dst_head
		if (BitOps.HASBIT(p2, 0) && src_head == dst_head) return 0;

		// moving a loco to a new line?, then we need to assign a unitnumber.
		if (dst == null && !src.IsFrontEngine() && src.IsTrainEngine()) {
			UnitID unit_num = GetFreeUnitNumber(Vehicle.VEH_Train);
			if (unit_num > Global._patches.max_trains)
				return Cmd.return_cmd_error(Str.STR_00E1_TOO_MANY_VEHICLES_IN_GAME);

			if (flags & Cmd.DC_EXEC)
				src.unitnumber = unit_num;
		}


		/* do it? */
		if (flags & Cmd.DC_EXEC) {
			/* clear the .first cache */
			{
				Vehicle u;

				for (u = src_head; u != null; u = u.next) u.first = null;
				for (u = dst_head; u != null; u = u.next) u.first = null;
			}

			if (BitOps.HASBIT(p2, 0)) {
				// unlink ALL wagons
				if (src != src_head) {
					Vehicle v = src_head;
					while (v.GetNextVehicle() != src) v = v.GetNextVehicle();
					v.GetLastEnginePart().next = null;
				} else {
					src_head = null;
				}
			} else {
				// if moving within the same chain, dont use dst_head as it may get invalidated
				if (src_head == dst_head)
					dst_head = null;
				// unlink single wagon from linked list
				src_head = UnlinkWagon(src, src_head);
				src.GetLastEnginePart().next = null;
			}

			if (dst == null) {
				// move the train to an empty line. for locomotives, we set the type to TS_Front. for wagons, 4.
				if (src.IsTrainEngine()) {
					if (!src.IsFrontEngine()) {
						// setting the type to 0 also involves setting up the orders field.
						src.SetFrontEngine();
						assert(src.orders == null);
						src.num_orders = 0;
					}
				} else {
					src.SetFreeWagon();
				}
				dst_head = src;
			} else {
				if (src.IsFrontEngine()) {
					// the vehicle was previously a loco. need to free the order list and delete vehicle windows etc.
					Window.DeleteWindowById(Window.WC_VEHICLE_VIEW, src.index);
					src.DeleteVehicleOrders();
				}

				src.ClearFrontEngine();
				src.ClearFreeWagon();
				src.unitnumber = 0; // doesn't occupy a unitnumber anymore.

				// link in the wagon(s) in the chain.
				{
					Vehicle v;

					for (v = src; v.GetNextVehicle() != null; v = v.GetNextVehicle());
					v.GetLastEnginePart().next = dst.next;
				}
				dst.next = src;
			}
			if (src.rail.other_multiheaded_part != null) {
				if (src.rail.other_multiheaded_part == src_head) {
					src_head = src_head.next;
				}
				AddWagonToConsist(src.rail.other_multiheaded_part, src);
			}

			if (BitOps.HASBIT(p2, 0) && src_head != null && src_head != src) {
				/* if we stole a rear multiheaded engine, we better give it back to the front end */
				Vehicle engine = null, u;
				for (u = src_head; u != null; u = u.next) {
					if (u.IsMultiheaded()) {
						if (u.IsTrainEngine()) {
							engine = u;
							continue;
						}
						/* we got the rear engine to match with the front one */
						engine = null;
					}
				}
				if (engine != null && engine.rail.other_multiheaded_part != null) {
					AddWagonToConsist(engine.rail.other_multiheaded_part, engine);
					// previous line set the front engine to the old front. We need to clear that
					engine.rail.other_multiheaded_part.first = null;
				}
			}

			/* If there is an engine behind first_engine we moved away, it should become new first_engine
			 * To do this, CmdMoveRailVehicle must be called once more
			 * we can't loop forever here because next time we reach this line we will have a front engine */
			if (src_head != null && !IsFrontEngine(src_head) && IsTrainEngine(src_head)) {
				CmdMoveRailVehicle(x, y, flags, src_head.index | (INVALID_VEHICLE << 16), 1);
				src_head = null;	// don't do anything more to this train since the new call will do it
			}

			if (src_head) {
				NormaliseTrainConsist(src_head);
				TrainConsistChanged(src_head);
				if (IsFrontEngine(src_head)) {
					UpdateTrainAcceleration(src_head);
					Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, src_head.index);
					/* Update the refit button and window */
					Window.InvalidateWindow(Window.WC_VEHICLE_REFIT, src_head.index);
					Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, src_head.index, 12);
				}
				/* Update the depot window */
				Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, src_head.tile);
			};

			if (dst_head) {
				NormaliseTrainConsist(dst_head);
				TrainConsistChanged(dst_head);
				if (IsFrontEngine(dst_head)) {
					UpdateTrainAcceleration(dst_head);
					Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, dst_head.index);
					/* Update the refit button and window */
					Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, dst_head.index, 12);
					Window.InvalidateWindow(Window.WC_VEHICLE_REFIT, dst_head.index);
				}
				/* Update the depot window */
				Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, dst_head.tile);
			}

			RebuildVehicleLists();
		}

		return 0;
	}

	/** Start/Stop a train.
	 * @param x,y unused
	 * @param p1 train to start/stop
	 * @param p2 unused
	 */
	public static int CmdStartStopTrain(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v;

		if (!Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		if (flags & Cmd.DC_EXEC) {
			v.rail.days_since_order_progr = 0;
			v.vehstatus ^= Vehicle.VS_STOPPED;
			Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
			Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);
		}
		return 0;
	}

	/** Sell a (single) train wagon/engine.
	 * @param x,y unused
	 * @param p1 the wagon/engine index
	 * @param p2 the selling mode
	 * - p2 = 0: only sell the single dragged wagon/engine (and any belonging rear-engines)
	 * - p2 = 1: sell the vehicle and all vehicles following it in the chain
             if the wagon is dragged, don't delete the possibly belonging rear-engine to some front
	 * - p2 = 2: when selling attached locos, rearrange all vehicles after it to separate lines;
	 *           all wagons of the same type will go on the same line. Used by the AI currently
	 */
	public static int CmdSellRailWagon(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v, tmp, first;
		Vehicle new_f = null;
		int cost = 0;

		if (!Vehicle.IsVehicleIndex(p1) || p2 > 2) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !Player.CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		Player.SET_EXPENSES_TYPE(Player.EXPENSES_NEW_VEHICLES);

		while (v.IsArticulatedPart()) v = v.GetPrevVehicleInChain();
		first = v.GetFirstVehicleInChain();

		// make sure the vehicle is stopped in the depot
		if (CheckTrainStoppedInDepot(first) < 0) return Cmd.CMD_ERROR;

		if (v.IsMultiheaded() && !v.IsTrainEngine()) return Cmd.return_cmd_error(Str.STR_REAR_ENGINE_FOLLOW_FRONT_ERROR);

		if (flags & Cmd.DC_EXEC) {
			if (v == first && IsFrontEngine(first)) {
				Window.DeleteWindowById(Window.WC_VEHICLE_VIEW, first.index);
			}
			if (IsLocalPlayer() && (p1 == 1 || !(RailVehInfo(v.engine_type).flags & Engine.RVI_WAGON))) {
				Window.InvalidateWindow(Window.WC_REPLACE_VEHICLE, Vehicle.VEH_Train);
			}
			Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, first.tile);
			RebuildVehicleLists();
		}

		switch (p2) {
		case 0: case 2: { /* Delete given wagon */
			boolean switch_engine = false;    // update second wagon to engine?
			byte ori_subtype = v.subtype; // backup subtype of deleted wagon in case DeleteVehicle() changes

			/* 1. Delete the engine, if it is dualheaded also delete the matching
			 * rear engine of the loco (from the point of deletion onwards) */
			Vehicle rear = (v.IsMultiheaded() &&
					v.IsTrainEngine()) ? v.rail.other_multiheaded_part : null;

			if (rear != null) {
				cost -= rear.value;
				if (flags & Cmd.DC_EXEC) {
					UnlinkWagon(rear, first);
					DeleteVehicle(rear);
				}
			}

			/* 2. We are selling the first engine, some special action might be required
			 * here, so take attention */
			if ((flags & Cmd.DC_EXEC) && v == first) {
				new_f = GetNextVehicle(first);

				/* 2.1 If the first wagon is sold, update the first. pointers to null */
				for (tmp = first; tmp != null; tmp = tmp.next) tmp.first = null;

				/* 2.2 If there are wagons present after the deleted front engine, check
				 * if the second wagon (which will be first) is an engine. If it is one,
				 * promote it as a new train, retaining the unitnumber, orders */
				if (new_f != null) {
					if (IsTrainEngine(new_f)) {
						switch_engine = true;
						/* Copy important data from the front engine */
						new_f.unitnumber = first.unitnumber;
						new_f.current_order = first.current_order;
						new_f.cur_order_index = first.cur_order_index;
						new_f.orders = first.orders;
						new_f.num_orders = first.num_orders;
						first.orders = null; // XXX - to not to delete the orders */
						if (IsLocalPlayer()) ShowTrainViewWindow(new_f);
					}
				}
			}

			/* 3. Delete the requested wagon */
			cost -= v.value;
			if (flags & Cmd.DC_EXEC) {
				first = UnlinkWagon(v, first);
				v.DeleteVehicle();

				/* 4 If the second wagon was an engine, update it to front_engine
				 * which UnlinkWagon() has changed to TS_Free_Car */
				if (switch_engine) SetFrontEngine(first);

				/* 5. If the train still exists, update its acceleration, window, etc. */
				if (first != null) {
					NormaliseTrainConsist(first);
					TrainConsistChanged(first);
					if (IsFrontEngine(first)) {
						Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, first.index);
						Window.InvalidateWindow(Window.WC_VEHICLE_REFIT, first.index);
						UpdateTrainAcceleration(first);
					}
				}


				/* (6.) Borked AI. If it sells an engine it expects all wagons lined
				 * up on a new line to be added to the newly built loco. Replace it is.
				 * Totally braindead cause building a new engine adds all loco-less
				 * engines to its train anyways */
				if (p2 == 2 && BitOps.HASBIT(ori_subtype, Train_Front)) {
					for (v = first; v != null; v = tmp) {
						tmp = v.GetNextVehicle();
						DoCommandByTile(v.tile, v.index | INVALID_VEHICLE << 16, 0, Cmd.DC_EXEC, Cmd.CMD_MOVE_RAIL_VEHICLE);
					}
				}
			}
		} break;
		case 1: { /* Delete wagon and all wagons after it given certain criteria */
			/* Start deleting every vehicle after the selected one
			 * If we encounter a matching rear-engine to a front-engine
			 * earlier in the chain (before deletion), leave it alone */
			for (; v != null; v = tmp) {
				tmp = v.GetNextVehicle();

				if (v.IsMultiheaded()) {
					if (v.IsTrainEngine()) {
						/* We got a front engine of a multiheaded set. Now we will sell the rear end too */
						Vehicle rear = v.rail.other_multiheaded_part;

						if (rear != null) {
							cost -= rear.value;
							if (flags & Cmd.DC_EXEC) {
								first = UnlinkWagon(rear, first);
								DeleteVehicle(rear);
							}
						}
					} else if (v.rail.other_multiheaded_part != null) {
						/* The front to this engine is earlier in this train. Do nothing */
						continue;
					}
				}

				cost -= v.value;
				if (flags & Cmd.DC_EXEC) {
					first = UnlinkWagon(v, first);
					v.DeleteVehicle();
				}
			}

			/* 3. If it is still a valid train after selling, update its acceleration and cached values */
			if ((flags & Cmd.DC_EXEC) && first != null) {
				NormaliseTrainConsist(first);
				TrainConsistChanged(first);
				if (IsFrontEngine(first))
					UpdateTrainAcceleration(first);
				Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, first.index);
				Window.InvalidateWindow(Window.WC_VEHICLE_REFIT, first.index);
			}
		} break;
		}
		return cost;
	}

	static void UpdateTrainDeltaXY(Vehicle v, int direction)
	{

		int x = _delta_xy_table[direction];

		v.x_offs        = BitOps.GB(x,  0, 8);
		v.y_offs        = BitOps.GB(x,  8, 8);
		v.sprite_width  = BitOps.GB(x, 16, 8);
		v.sprite_height = BitOps.GB(x, 24, 8);
	}

	static void UpdateVarsAfterSwap(Vehicle v)
	{
		UpdateTrainDeltaXY(v, v.direction);
		v.cur_image = GetTrainImage(v, v.direction);
		v.BeginVehicleMove();
		v.VehiclePositionChanged();
		EndVehicleMove(v);
	}

	static void SetLastSpeed(Vehicle  v, int spd)
	{
		int old = v.rail.last_speed;
		if (spd != old) {
			v.rail.last_speed = spd;
			if (Global._patches.vehicle_speed || !old != !spd)
				Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
		}
	}

	/* TODO
	static void SwapTrainFlags(byte *swap_flag1, byte *swap_flag2)
	{
		byte flag1, flag2;

		flag1 = *swap_flag1;
		flag2 = *swap_flag2;

		// Clear the flags 
		CLRBIT(*swap_flag1, Vehicle.VRF_GOINGUP);
		CLRBIT(*swap_flag1, Vehicle.VRF_GOINGDOWN);
		CLRBIT(*swap_flag2, Vehicle.VRF_GOINGUP);
		CLRBIT(*swap_flag2, Vehicle.VRF_GOINGDOWN);

		// Reverse the rail-flags (if needed) 
	if (BitOps.HASBIT(flag1, Vehicle.VRF_GOINGUP)) {
		SETBIT(*swap_flag2, Vehicle.VRF_GOINGDOWN);
	} else if (BitOps.HASBIT(flag1, Vehicle.VRF_GOINGDOWN)) {
		SETBIT(*swap_flag2, Vehicle.VRF_GOINGUP);
	}
	if (BitOps.HASBIT(flag2, Vehicle.VRF_GOINGUP)) {
		SETBIT(*swap_flag1, Vehicle.VRF_GOINGDOWN);
	} else if (BitOps.HASBIT(flag2, Vehicle.VRF_GOINGDOWN)) {
		SETBIT(*swap_flag1, Vehicle.VRF_GOINGUP);
	}
}
	 */

	static void ReverseTrainSwapVeh(Vehicle v, int l, int r)
	{
		Vehicle a, b;

		/* locate vehicles to swap */
		for (a = v; l != 0; l--) a = a.next;
		for (b = v; r != 0; r--) b = b.next;

		if (a != b) {
			/* swap the hidden bits */
			{
				int tmp = (a.vehstatus & ~Vehicle.VS_HIDDEN) | (b.vehstatus&Vehicle.VS_HIDDEN);
				b.vehstatus = (b.vehstatus & ~Vehicle.VS_HIDDEN) | (a.vehstatus&Vehicle.VS_HIDDEN);
				a.vehstatus = tmp;
			}

			/* swap variables */
			swap_byte(&a.rail.track, &b.rail.track);
			swap_byte(&a.direction, &b.direction);

			/* toggle direction */
			if (!(a.rail.track & 0x80)) a.direction ^= 4;
			if (!(b.rail.track & 0x80)) b.direction ^= 4;

			/* swap more variables */
			swap_int(&a.x_pos, &b.x_pos);
			swap_int(&a.y_pos, &b.y_pos);
			swap_tile(&a.tile, &b.tile);
			swap_int(&a.z_pos, &b.z_pos);

			SwapTrainFlags(&a.rail.flags, &b.rail.flags);

			/* update other vars */
			UpdateVarsAfterSwap(a);
			UpdateVarsAfterSwap(b);

			VehicleEnterTile(a, a.tile, a.x_pos, a.y_pos);
			VehicleEnterTile(b, b.tile, b.x_pos, b.y_pos);
		} else {
			if (!(a.rail.track & 0x80)) a.direction ^= 4;
			UpdateVarsAfterSwap(a);

			VehicleEnterTile(a, a.tile, a.x_pos, a.y_pos);
		}
	}

	/* Check if the vehicle is a train and is on the tile we are testing */
	static Object TestTrainOnCrossing(Vehicle v, Object data)
	{
		if (!v.tile.equals( (TileIndex)data ) || v.type != Vehicle.VEH_Train) return null;
		return v;
	}

	static void DisableTrainCrossing(TileIndex tile)
	{
		if (tile.IsTileType( TileTypes.MP_STREET) &&
				tile.IsLevelCrossing() &&
				Vehicle.VehicleFromPos(tile, tile, TrainCmd::TestTrainOnCrossing) == null && // empty?
				BitOps.GB(tile.getMap().m5, 2, 1) != 0) { // Lights on?
			tile.getMap().m5 = (byte) BitOps.RETSB(tile.getMap().m5, 2, 1, 0); // Switch lights off
			tile.MarkTileDirtyByTile();
		}
	}

	/**
	 * Advances wagons for train reversing, needed for variable length wagons.
	 * Needs to be called once before the train is reversed, and once after it.
	 * @param v First vehicle in chain
	 * @param before Set to true for the call before reversing, false otherwise
	 */
	static void AdvanceWagons(Vehicle v, boolean before)
	{
		Vehicle  base;
		Vehicle  first;
		int length;

		base = v;
		first = base.next;
		length = v.CountVehiclesInChain();

		while (length > 2) {
			Vehicle  last;
			int differential;
			int i;

			// find pairwise matching wagon
			// start<>end, start+1<>end-1, ... */
			last = first;
			for (i = length - 3; i > 0; i--) last = last.next;

			differential = last.rail.cached_veh_length - base.rail.cached_veh_length;
			if (before) differential *= -1;

			if (differential > 0) {
				Vehicle  tempnext;

				// disconnect last car to make sure only this subset moves
				tempnext = last.next;
				last.next = null;

				for (i = 0; i < differential; i++) TrainController(first);

				last.next = tempnext;
			}

			base = first;
			first = first.next;
			length -= 2;
		}
	}

	static TileIndex GetVehicleTileOutOfTunnel(final Vehicle  v, boolean reverse)
	{
		TileIndex tile;
		byte direction = (!reverse) ? DirToDiagdir(v.direction) : ReverseDiagdir(v.direction >> 1);
		TileIndexDiff delta = TileIndex.TileOffsByDir(direction);

		if (v.rail.track != 0x40) return v.tile;

		for (tile = v.tile;; tile += delta) {
			if (tile.IsTunnelTile() && BitOps.GB(tile.getMap().m5, 0, 2) != direction 
					&& tile.GetTileZ() == v.z_pos)
				break;
		}
		return tile;
	}

	static void ReverseTrainDirection(Vehicle v)
	{
		int l = 0, r = -1;
		Vehicle u;
		TileIndex tile;
		Trackdir trackdir;
		TileIndex pbs_end_tile = v.rail.pbs_end_tile; // these may be changed, and we may need
		Trackdir pbs_end_trackdir = v.rail.pbs_end_trackdir; // the old values, so cache them

		u = v.GetLastVehicleInChain();
		tile = Vehicle.GetVehicleTileOutOfTunnel(u, false);
		trackdir = ReverseTrackdir(Vehicle.GetVehicleTrackdir(u));

		if (PBSTileReserved(tile) & (1 << TrackdirToTrack(trackdir))) {
			NPFFindStationOrTileData fstd = new NPFFindStationOrTileData();
			NPFFoundTargetData ftd;

			NPFFillWithOrderData(fstd, v);

			tile = Vehicle.GetVehicleTileOutOfTunnel(u, true);

			Global.DEBUG_pbs(2, "pbs: (%i) choose reverse (RV), tile:%x, trackdir:%i",v.unitnumber,  u.tile, trackdir);
			ftd = NPFRouteToStationOrTile(tile, trackdir, fstd, Global.TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_ANY);

			if (ftd.best_trackdir == 0xFF) {
				Global.DEBUG_pbs(0, "pbs: (%i) no nodes encountered (RV)", v.unitnumber);
				v.rail.flags = BitOps.RETCLRBIT(v.rail.flags, Vehicle.VRF_REVERSING);
				return;
			}

			// we found a way out of the pbs block
			if (NPFGetFlag(ftd.node, NPF_FLAG_PBS_EXIT)) {
				if (NPFGetFlag(ftd.node, NPF_FLAG_PBS_BLOCKED)) {
					v.rail.flags = BitOps.RETCLRBIT(v.rail.flags, Vehicle.VRF_REVERSING);
					return;
				}
			}

			v.rail.pbs_end_tile = ftd.node.tile;
			v.rail.pbs_end_trackdir = ftd.node.direction;
		}

		tile = Vehicle.GetVehicleTileOutOfTunnel(v, false);
		trackdir = Vehicle.GetVehicleTrackdir(v);

		if (v.rail.pbs_status == PBS_STAT_HAS_PATH) 
		{
			TileIndex tile = AddTileIndexDiffCWrap(v.tile, TileIndexDiffCByDir(TrackdirToExitdir(trackdir)));

			int ts;
			assert(tile != INVALID_TILE);

			ts = GetTileTrackStatus(tile, TRANSPORT_RAIL);
			ts &= TrackdirReachesTrackdirs(trackdir);

			assert(ts != 0 && KillFirstBit2x64(ts) == 0);

			trackdir = FindFirstBit2x64(ts);
			PBSClearPath(tile, trackdir, pbs_end_tile, pbs_end_trackdir);
			v.rail.pbs_status = PBS_STAT_NONE;

		} else if (PBSTileReserved(tile) & (1 << TrackdirToTrack(trackdir))) 
		{
			PBSClearPath(tile, trackdir, pbs_end_tile, pbs_end_trackdir);
			if (v.rail.track != 0x40)
				PBSReserveTrack(tile, trackdir & 7);
		};

		if (IsTileDepotType(v.tile, TRANSPORT_RAIL))
			Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);


		/* Check if we were approaching a rail/road-crossing */
		{
			TileIndex tile = v.tile;
			int t;
			/* Determine the diagonal direction in which we will exit this tile */
			t = v.direction >> 1;

		if (!(v.direction & 1) && v.rail.track != _state_dir_table[t]) {
			t = (t - 1) & 3;
		}
		/* Calculate next tile */
		tile += TileOffsByDir(t);

		/* Check if the train left a rail/road-crossing */
		DisableTrainCrossing(tile);
		}

		// count number of vehicles
		u = v;
		do r++; while ( (u = u.next) != null );

		AdvanceWagons(v, true);

		/* swap start<>end, start+1<>end-1, ... */
		do {
			ReverseTrainSwapVeh(v, l++, r--);
		} while (l <= r);

		AdvanceWagons(v, false);

		if (IsTileDepotType(v.tile, TRANSPORT_RAIL))
			Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);

		v.rail.flags = BitOps.RETCLRBIT(v.rail.flags, Vehicle.VRF_REVERSING);
	}

	/** Reverse train.
	 * @param x,y unused
	 * @param p1 train to reverse
	 * @param p2 unused
	 */
	public static int CmdReverseTrainDirection(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v;

		if (!Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		Global._error_message = Str.STR_EMPTY;

		//	if (v.rail.track & 0x80 || IsTileDepotType(v.tile, TRANSPORT_RAIL))
		//		return Cmd.CMD_ERROR;

		if (v.rail.crash_anim_pos != 0 || v.breakdown_ctr != 0) return Cmd.CMD_ERROR;

		if (flags & Cmd.DC_EXEC) {
			if (Global._patches.realistic_acceleration && v.cur_speed != 0) {
				v.rail.flags = BitOps.RETTOGGLEBIT(v.rail.flags, Vehicle.VRF_REVERSING);
			} else {
				v.cur_speed = 0;
				SetLastSpeed(v, 0);
				ReverseTrainDirection(v);
			}
		}
		return 0;
	}

	/** Force a train through a red signal
	 * @param x,y unused
	 * @param p1 train to ignore the red signal
	 * @param p2 unused
	 */
	public static int CmdForceTrainProceed(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v;

		if (!Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		if (flags & Cmd.DC_EXEC) v.rail.force_proceed = 0x50;

		return 0;
	}

	/** Refits a train to the specified cargo type.
	 * @param x,y unused
	 * @param p1 vehicle ID of the train to refit
	 * @param p2 the new cargo type to refit to (p2 & 0xFF)
	 */
	public static int CmdRefitRailVehicle(int x, int y, int flags, int p1, int p2)
	{
		//CargoID new_cid = BitOps.GB(p2, 0, 8);
		int new_cid = BitOps.GB(p2, 0, 8);
		Vehicle v;
		int cost;
		int num;

		if (!Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;
		if (CheckTrainStoppedInDepot(v) < 0) return Cmd.return_cmd_error(Str.STR_TRAIN_MUST_BE_STOPPED);

		/* Check cargo */
		if (new_cid > NUM_CARGO) return Cmd.CMD_ERROR;

		Player.SET_EXPENSES_TYPE(Player.EXPENSES_TRAIN_RUN);

		cost = 0;
		num = 0;

		do {
			/* XXX: We also refit all the attached wagons en-masse if they
			 * can be refitted. This is how TTDPatch does it.  TODO: Have
			 * some nice [Refit] button near each wagon. --pasky */
			if (!CanRefitTo(v.engine_type, new_cid)) continue;

			if (v.cargo_cap != 0) {
				final RailVehicleInfo rvi = RailVehInfo(v.engine_type);
				int amount = CALLBACK_FAILED;

				if (BitOps.HASBIT(rvi.callbackmask, CBM_REFIT_CAP)) {
					/* Check the 'refit capacity' callback */
					CargoID temp_cid = v.cargo_type;
					v.cargo_type = new_cid;
					amount = GetCallBackResult(CBID_REFIT_CAP, v.engine_type, v);
					v.cargo_type = temp_cid;
				}

				if (amount == CALLBACK_FAILED) { // callback failed or not used, use default
					CargoID old_cid = rvi.cargo_type;
					/* normally, the capacity depends on the cargo type, a rail vehicle
					 * can carry twice as much mail/goods as normal cargo,
					 * and four times as much passengers */
					amount = rvi.capacity;
					/*
					(old_cid == AcceptedCargo.CT_PASSENGERS) ||
					(amount <<= 1, old_cid == AcceptedCargo.CT_MAIL || old_cid == AcceptedCargo.CT_GOODS) ||
					(amount <<= 1, true);
					(new_cid == AcceptedCargo.CT_PASSENGERS) ||
					(amount >>= 1, new_cid == AcceptedCargo.CT_MAIL || new_cid == AcceptedCargo.CT_GOODS) ||
					(amount >>= 1, true);
					 */

					if(old_cid != AcceptedCargo.CT_PASSENGERS)
					{
						amount <<= 1;
						if( !(old_cid == AcceptedCargo.CT_MAIL || old_cid == AcceptedCargo.CT_GOODS)) 
						{
							amount <<= 1;

							if(new_cid != AcceptedCargo.CT_PASSENGERS) 
							{
								amount >>= 1;
							if( !(new_cid == AcceptedCargo.CT_MAIL || new_cid == AcceptedCargo.CT_GOODS) )
								amount >>= 1;
							}
						}
					}
				};

				if (amount != 0) 
				{
					if (new_cid != v.cargo_type) 
						cost += Global._price.build_railvehicle >> 8;

		num += amount;
		if (flags & Cmd.DC_EXEC) 
		{
			v.cargo_count = 0;
			v.cargo_type = new_cid;
			v.cargo_cap = amount;
			Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);
			Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);
		}
				}
			}
		} while ( (v=v.next) != null );

		_returned_refit_amount = num;

		return cost;
	}

	class TrainFindDepotData {
		int best_length;
		TileIndex tile;
		PlayerID owner;
		/**
		 * true if reversing is necessary for the train to get to this depot
		 * This value is unused when new depot finding and NPF are both disabled
		 */
		boolean reverse;
	} TrainFindDepotData;

	static boolean NtpCallbFindDepot(TileIndex tile, TrainFindDepotData tfdd, int track, int length)
	{
		if (tile.IsTileType( TileTypes.MP_RAILWAY) && tile.IsTileOwner( tfdd.owner)) {
			if ((tile.getMap().m5 & ~0x3) == 0xC0) {
				tfdd.best_length = length;
				tfdd.tile = tile;
				return true;
			}
		}

		return false;
	}

	// returns the tile of a depot to goto to. The given vehicle must not be
	// crashed!
	static TrainFindDepotData FindClosestTrainDepot(Vehicle v)
	{
		int i;
		TrainFindDepotData tfdd;
		TileIndex tile = v.tile;

		assert(!(v.vehstatus & Vehicle.VS_CRASHED));

		tfdd.owner = v.owner;
		tfdd.best_length = (int)-1;
		tfdd.reverse = false;

		if (IsTileDepotType(tile, TRANSPORT_RAIL)){
			tfdd.tile = tile;
			tfdd.best_length = 0;
			return tfdd;
		}

		if (v.rail.track == 0x40) tile = Vehicle.GetVehicleOutOfTunnelTile(v);

		if (Global._patches.new_pathfinding_all) {
			NPFFoundTargetData ftd;
			Vehicle  last = v.GetLastVehicleInChain();
			Trackdir trackdir = Vehicle.GetVehicleTrackdir(v);
			Trackdir trackdir_rev = ReverseTrackdir(Vehicle.GetVehicleTrackdir(last));

			assert (trackdir != INVALID_TRACKDIR);
			ftd = NPFRouteToDepotBreadthFirstTwoWay(v.tile, trackdir, last.tile, trackdir_rev, TRANSPORT_RAIL, v.owner, v.rail.railtype, NPF_INFINITE_PENALTY);
			if (ftd.best_bird_dist == 0) {
				/* Found target */
				tfdd.tile = ftd.node.tile;
				/* Our caller expects a number of tiles, so we just approximate that
				 * number by this. It might not be completely what we want, but it will
				 * work for now :-) We can possibly change this when the old pathfinder
				 * is removed. */
				tfdd.best_length = ftd.best_path_dist / NPF_TILE_LENGTH;
				if (NPFGetFlag(&ftd.node, NPF_FLAG_REVERSE))
					tfdd.reverse = true;
			}
		} else {
			// search in the forward direction first.
			i = v.direction >> 1;
		if (!(v.direction & 1) && v.rail.track != _state_dir_table[i]) i = (i - 1) & 3;
		NewTrainPathfind(tile, 0, i, (NTPEnumProc*)NtpCallbFindDepot, &tfdd);
		if (tfdd.best_length == (int)-1){
			tfdd.reverse = true;
			// search in backwards direction
			i = (v.direction^4) >> 1;
			if (!(v.direction & 1) && v.rail.track != _state_dir_table[i]) i = (i - 1) & 3;
			NewTrainPathfind(tile, 0, i, (NTPEnumProc*)NtpCallbFindDepot, &tfdd);
		}
		}

		return tfdd;
	}

	/** Send a train to a depot
	 * @param x,y unused
	 * @param p1 train to send to the depot
	 * @param p2 unused
	 */
	public static int CmdSendTrainToDepot(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v;
		TrainFindDepotData tfdd;

		if (!Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		if (v.vehstatus & Vehicle.VS_CRASHED) return Cmd.CMD_ERROR;

		if (v.current_order.type == Order.OT_GOTO_DEPOT) {
			if (flags & Cmd.DC_EXEC) {
				if (BitOps.HASBIT(v.current_order.flags, OFB_PART_OF_ORDERS)) {
					v.rail.days_since_order_progr = 0;
					v.cur_order_index++;
				}

				v.current_order.type = Order.OT_DUMMY;
				v.current_order.flags = 0;
				Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
			}
			return 0;
		}

		tfdd = FindClosestTrainDepot(v);
		if (tfdd.best_length == (int)-1)
			return Cmd.return_cmd_error(Str.STR_883A_UNABLE_TO_FIND_ROUTE_TO);

		if (flags & Cmd.DC_EXEC) {
			v.dest_tile = tfdd.tile;
			v.current_order.type = Order.OT_GOTO_DEPOT;
			v.current_order.flags = Order.OF_NON_STOP | Order.OF_FULL_LOAD;
			v.current_order.station = GetDepotByTile(tfdd.tile).index;
			Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
			/* If there is no depot in front, reverse automatically */
			if (tfdd.reverse)
				DoCommandByTile(v.tile, v.index, 0, Cmd.DC_EXEC, Cmd.CMD_REVERSE_TRAIN_DIRECTION);
		}

		return 0;
	}

	/** Change the service interval for trains.
	 * @param x,y unused
	 * @param p1 vehicle ID that is being service-interval-changed
	 * @param p2 new service interval
	 */
	public static int CmdChangeTrainServiceInt(int x, int y, int flags, int p1, int p2)
	{
		Vehicle v;
		int serv_int = GetServiceIntervalClamped(p2); /* Double check the service interval from the user-input */

		if (serv_int != p2 || !Vehicle.IsVehicleIndex(p1)) return Cmd.CMD_ERROR;

		v = Vehicle.GetVehicle(p1);

		if (v.type != Vehicle.VEH_Train || !CheckOwnership(v.owner)) return Cmd.CMD_ERROR;

		if (flags & Cmd.DC_EXEC) {
			v.service_interval = serv_int;
			Window.InvalidateWindowWidget(Window.WC_VEHICLE_DETAILS, v.index, 8);
		}

		return 0;
	}

	static void OnTick_Train()
	{
		_age_cargo_skip_counter = (_age_cargo_skip_counter == 0) ? 184 : (_age_cargo_skip_counter - 1);
	}


	static void HandleLocomotiveSmokeCloud(final Vehicle  v)
	{
		final Vehicle  u;

		if ( 0 != (v.vehstatus & Vehicle.VS_TRAIN_SLOWING) 
				|| v.load_unload_time_rem != 0 
				|| v.cur_speed < 2)
			return;

		u = v;

		do {
			EngineID engtype = v.engine_type;
			int effect_offset = BitOps.GB(v.rail.cached_vis_effect, 0, 4) - 8;
			int effect_type = BitOps.GB(v.rail.cached_vis_effect, 4, 2);
			boolean disable_effect = BitOps.HASBIT(v.rail.cached_vis_effect, 6);
			int x, y;

			// no smoke?
			if ((RailVehInfo(engtype).flags & Engine.RVI_WAGON && effect_type == 0) ||
					disable_effect ||
					GetEngine(engtype).railtype > RAILTYPE_RAIL ||
					v.vehstatus & Vehicle.VS_HIDDEN ||
					v.rail.track & 0xC0) {
				continue;
			}

			// No smoke in depots or tunnels
			if (IsTileDepotType(v.tile, TRANSPORT_RAIL) || IsTunnelTile(v.tile))
				continue;

			if (effect_type == 0) {
				// Use default effect type for engine class.
				effect_type = RailVehInfo(engtype).engclass;
			} else {
				effect_type--;
			}

			x = _vehicle_smoke_pos[v.direction] * effect_offset;
			y = _vehicle_smoke_pos[(v.direction + 2) % 8] * effect_offset;

			switch (effect_type) {
			case 0:
				// steam smoke.
				if (BitOps.GB(v.tick_counter, 0, 4) == 0) {
					CreateEffectVehicleRel(v, x, y, 10, EV_STEAM_SMOKE);
				}
				break;

			case 1:
				// diesel smoke
				if (u.cur_speed <= 40 && BitOps.CHANCE16(15, 128)) {
					CreateEffectVehicleRel(v, 0, 0, 10, EV_DIESEL_SMOKE);
				}
				break;

			case 2:
				// blue spark
				if (BitOps.GB(v.tick_counter, 0, 2) == 0 && BitOps.CHANCE16(1, 45)) {
					CreateEffectVehicleRel(v, 0, 0, 10, EV_ELECTRIC_SPARK);
				}
				break;
			}
		} while ((v = v.next) != null);
	}

	static void TrainPlayLeaveStationSound(final Vehicle  v)
	{

		EngineID engtype = v.engine_type;

		switch (Engine.GetEngine(engtype).railtype) {
		case RAILTYPE_RAIL:
			//SndPlayVehicleFx(sfx[RailVehInfo(engtype).engclass], v);
			break;

		case RAILTYPE_MONO:
			//SndPlayVehicleFx(SND_47_MAGLEV_2, v);
			break;

		case RAILTYPE_MAGLEV:
			//SndPlayVehicleFx(SND_41_MAGLEV, v);
			break;
		}
	}

	static boolean CheckTrainStayInDepot(Vehicle v)
	{
		Vehicle u;

		// bail out if not all wagons are in the same depot or not in a depot at all
		for (u = v; u != null; u = u.next) {
			if (u.rail.track != 0x80 || u.tile != v.tile) return false;
		}

		if (v.rail.force_proceed == 0) {
			Trackdir trackdir = Vehicle.GetVehicleTrackdir(v);

			if (++v.load_unload_time_rem < 37) {
				Window.InvalidateWindowClasses(Window.WC_TRAINS_LIST);
				return true;
			}

			v.load_unload_time_rem = 0;

			if (PBSIsPbsSegment(v.tile, trackdir)) {
				NPFFindStationOrTileData fstd;
				NPFFoundTargetData ftd;

				if (PBSTileUnavail(v.tile) & (1 << trackdir)) return true;

				NPFFillWithOrderData(&fstd, v);

				Global.DEBUG_pbs(2, "pbs: (%i) choose depot (DP), tile:%x, trackdir:%i",v.unitnumber,  v.tile, trackdir);
				ftd = NPFRouteToStationOrTile(v.tile, trackdir, &fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_GREEN);

				// we found a way out of the pbs block
				if (NPFGetFlag(&ftd.node, NPF_FLAG_PBS_EXIT)) {
					if (NPFGetFlag(&ftd.node, NPF_FLAG_PBS_BLOCKED) || NPFGetFlag(&ftd.node, NPF_FLAG_PBS_RED)) {
						return true;
					} else {
						v.rail.pbs_end_tile = ftd.node.tile;
						v.rail.pbs_end_trackdir = ftd.node.direction;
						goto green;
					}
				}
			}

			if (UpdateSignalsOnSegment(v.tile, v.direction)) {
				Window.InvalidateWindowClasses(Window.WC_TRAINS_LIST);
				return true;
			}
		}
		green:
			v.VehicleServiceInDepot();
		Window.InvalidateWindowClasses(Window.WC_TRAINS_LIST);
		TrainPlayLeaveStationSound(v);

		v.rail.track = 1;
		if (v.direction & 2) v.rail.track = 2;

		v.vehstatus &= ~Vehicle.VS_HIDDEN;
		v.cur_speed = 0;

		UpdateTrainDeltaXY(v, v.direction);
		v.cur_image = GetTrainImage(v, v.direction);
		v.VehiclePositionChanged();
		UpdateSignalsOnSegment(v.tile, v.direction);
		UpdateTrainAcceleration(v);
		Window.InvalidateWindow(Window.WC_VEHICLE_DEPOT, v.tile);

		return false;
	}

	/* Check for station tiles */
	static class TrainTrackFollowerData {
		TileIndex dest_coords;
		StationID station_index; // station index we're heading for
		int best_bird_dist;
		int best_track_dist;
		byte best_track;
	}

	static boolean NtpCallbFindStation(TileIndex tile, TrainTrackFollowerData ttfd, int track, int length)
	{
		// heading for nowhere?
		if (ttfd.dest_coords == null)
			return false;

		// did we reach the final station?
		if ((ttfd.station_index == INVALID_STATION && tile == ttfd.dest_coords) ||
				(tile.IsTileType( TileTypes.MP_STATION) && BitOps.IS_INT_INSIDE(tile.getMap().m5, 0, 8) && tile.getMap().m2 == ttfd.station_index)) {
			/* We do not check for dest_coords if we have a station_index,
			 * because in that case the dest_coords are just an
			 * approximation of where the station is */
			// found station
			ttfd.best_track = track;
			return true;
		} else {
			int dist;

			// didn't find station, keep track of the best path so far.
			dist = DistanceManhattan(tile, ttfd.dest_coords);
			if (dist < ttfd.best_bird_dist) {
				ttfd.best_bird_dist = dist;
				ttfd.best_track = track;
			}
			return false;
		}
	}

	static void FillWithStationData(TrainTrackFollowerData fd, final Vehicle  v)
	{
		fd.dest_coords = v.dest_tile;
		if (v.current_order.type == Order.OT_GOTO_STATION) {
			fd.station_index = v.current_order.station;
		} else {
			fd.station_index = INVALID_STATION;
		}
	}






	/* choose a track */
	static byte ChooseTrainTrack(Vehicle v, TileIndex tile, int enterdir, TrackdirBits trackdirbits)
	{
		TrainTrackFollowerData fd = new TrainTrackFollowerData();
		int best_track;
		/*#ifdef PF_BENCHMARK
	int time = _rdtsc();
	static float f;
#endif*/

		assert( (trackdirbits & ~0x3F) == 0);

		/* quick return in case only one possible track is available */
		if (KILL_FIRST_BIT(trackdirbits) == 0)
			return FIND_FIRST_BIT(trackdirbits);

		if (Global._patches.new_pathfinding_all) { /* Use a new pathfinding for everything */
			NPFFindStationOrTileData fstd = new NPFFindStationOrTileData();
			NPFFoundTargetData ftd;
			Trackdir trackdir;
			int pbs_tracks;

			NPFFillWithOrderData(fstd, v);
			/* The enterdir for the new tile, is the exitdir for the old tile */
			trackdir = Vehicle.GetVehicleTrackdir(v);
			assert(trackdir != 0xff);

			pbs_tracks = PBSTileReserved(tile);
			pbs_tracks |= pbs_tracks << 8;
			pbs_tracks &= TrackdirReachesTrackdirs(trackdir);
			if (pbs_tracks || (v.rail.pbs_status == PBS_STAT_NEED_PATH)) {
				Global.DEBUG_pbs(2, "pbs: (%i) choosefromblock, tile_org:%x tile_dst:%x  trackdir:%i  pbs_tracks:%i",v.unitnumber, tile,tile - TileOffsByDir(enterdir), trackdir, pbs_tracks);
				// clear the currently planned path
				if (v.rail.pbs_status != PBS_STAT_NEED_PATH) PBSClearPath(tile, FindFirstBit2x64(pbs_tracks), v.rail.pbs_end_tile, v.rail.pbs_end_trackdir);

				// try to find a route to a green exit signal
				ftd = NPFRouteToStationOrTile(tile - TileOffsByDir(enterdir), trackdir, fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_ANY);

				v.rail.pbs_end_tile = ftd.node.tile;
				v.rail.pbs_end_trackdir = ftd.node.direction;

			} else
				ftd = NPFRouteToStationOrTile(tile - TileOffsByDir(enterdir), trackdir, fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_NONE);

			if (ftd.best_trackdir == 0xff) {
				/* We are already at our target. Just do something */
				//TODO: maybe display error?
				//TODO: go straight ahead if possible?
				best_track = FIND_FIRST_BIT(trackdirbits);
			} else {
				/* If ftd.best_bird_dist is 0, we found our target and ftd.best_trackdir contains
			the direction we need to take to get there, if ftd.best_bird_dist is not 0,
			we did not find our target, but ftd.best_trackdir contains the direction leading
			to the tile closest to our target. */
				/* Discard enterdir information, making it a normal track */
				best_track = TrackdirToTrack(ftd.best_trackdir);
			}
		} else {
			FillWithStationData(fd, v);

			/* New train pathfinding */
			fd.best_bird_dist = (int)-1;
			fd.best_track_dist = (int)-1;
			fd.best_track = 0xFF;

			NewTrainPathfind(tile - TileOffsByDir(enterdir), v.dest_tile,
					enterdir, (NTPEnumProc)NtpCallbFindStation, fd);

			if (fd.best_track == 0xff) {
				// blaha
				best_track = FIND_FIRST_BIT(trackdirbits);
			} else {
				best_track = fd.best_track & 7;
			}
		}

		/*#ifdef PF_BENCHMARK
	time = _rdtsc() - time;
	f = f * 0.99 + 0.01 * time;
	printf("PF time = %d %f\n", time, f);
#endif*/

		return best_track;
	}


	private static boolean CheckReverseTrain_helper(int i, int best_track, int best_bird_dist, int best_track_dist, TrainTrackFollowerData fd)
	{
		if (best_track != -1) {
			if (best_bird_dist != 0) {
				if (fd.best_bird_dist != 0) {
					// neither reached the destination, pick the one with the smallest bird dist 
					if (fd.best_bird_dist > best_bird_dist) return false; //goto bad;
					if (fd.best_bird_dist < best_bird_dist) return true; //goto good;
				} else {
					// we found the destination for the first time 
					return false; //goto bad;
				}
			} else {
				if (fd.best_bird_dist != 0) {
					// didn't find destination, but we've found the destination previously 
					return false; //goto bad;
				} else {
					// both old & new reached the destination, compare track length 
					if (fd.best_track_dist > best_track_dist) return false; //goto bad;
					if (fd.best_track_dist < best_track_dist) return true; //goto good;
				}
			}

			// if we reach this position, there's two paths of equal value so far. pick one randomly. 
			int r = BitOps.GB(Hal.Random(), 0, 8);
			if (_pick_track_table[i] == (v.direction & 3)) r += 80;
			if (_pick_track_table[best_track] == (v.direction & 3)) r -= 80;
			if (r <= 127) return false; //goto bad;

			return true;
		}
	}


	static boolean CheckReverseTrain(Vehicle v)
	{
		TrainTrackFollowerData fd = new TrainTrackFollowerData();
		int i, r;
		int best_track;
		int best_bird_dist  = 0;
		int best_track_dist = 0;
		int reverse, reverse_best;

		if (GameOptions._opt.diff.line_reverse_mode != 0 ||
				v.rail.track & 0xC0 ||
				!(v.direction & 1))
			return false;

		FillWithStationData(fd, v);

		best_track = -1;
		reverse_best = reverse = 0;

		assert(v.rail.track);

		i = _search_directions[FIND_FIRST_BIT(v.rail.track)][v.direction>>1];

		if (Global._patches.new_pathfinding_all) { /* Use a new pathfinding for everything */
			NPFFindStationOrTileData fstd = new NPFFindStationOrTileData();
			NPFFoundTargetData ftd;
			byte trackdir, trackdir_rev;
			Vehicle  last = v.GetLastVehicleInChain();

			NPFFillWithOrderData(fstd, v);

			trackdir = Vehicle.GetVehicleTrackdir(v);
			trackdir_rev = ReverseTrackdir(Vehicle.GetVehicleTrackdir(last));
			assert(trackdir != 0xff);
			assert(trackdir_rev != 0xff);

			ftd = NPFRouteToStationOrTileTwoWay(v.tile, trackdir, last.tile, trackdir_rev, &fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_NONE);

			if (ftd.best_bird_dist != 0) {
				/* We didn't find anything, just keep on going straight ahead */
				reverse_best = false;
			} else {
				if (NPFGetFlag(ftd.node, NPF_FLAG_REVERSE))
					reverse_best = true;
				else
					reverse_best = false;
			}
		} else {
			while(true) {
				fd.best_bird_dist = (int)-1;
				fd.best_track_dist = (int)-1;

				NewTrainPathfind(v.tile, v.dest_tile, reverse ^ i, (NTPEnumProc*)NtpCallbFindStation, fd);

				/* if (best_track != -1) {
				if (best_bird_dist != 0) {
					if (fd.best_bird_dist != 0) {
						// neither reached the destination, pick the one with the smallest bird dist 
						if (fd.best_bird_dist > best_bird_dist) goto bad;
						if (fd.best_bird_dist < best_bird_dist) goto good;
					} else {
						// we found the destination for the first time 
						goto good;
					}
				} else {
					if (fd.best_bird_dist != 0) {
						// didn't find destination, but we've found the destination previously 
						goto bad;
					} else {
						// both old & new reached the destination, compare track length 
						if (fd.best_track_dist > best_track_dist) goto bad;
						if (fd.best_track_dist < best_track_dist) goto good;
					}
				}

				// if we reach this position, there's two paths of equal value so far. pick one randomly. 
				r = BitOps.GB(Hal.Random(), 0, 8);
				if (_pick_track_table[i] == (v.direction & 3)) r += 80;
				if (_pick_track_table[best_track] == (v.direction & 3)) r -= 80;
				if (r <= 127) goto bad;
			} */
				if( CheckReverseTrain_helper(i, best_track, best_bird_dist, best_track_dist, fd ) )
				{
					//good:;
					best_track = i;
					best_bird_dist = fd.best_bird_dist;
					best_track_dist = fd.best_track_dist;
					reverse_best = reverse;
				}
				//bad:;
				if (reverse != 0)
					break;
				reverse = 2;
			}
		}

		return reverse_best != 0;
	}

	static boolean ProcessTrainOrder(Vehicle v)
	{
		final Order order;
		boolean result;

		// These are un-interruptible
		if (v.current_order.type >= Order.OT_GOTO_DEPOT &&
				v.current_order.type <= Order.OT_LEAVESTATION) {
			// Let a depot order in the orderlist interrupt.
			if (v.current_order.type != Order.OT_GOTO_DEPOT ||
					!(v.current_order.flags & Order.OF_UNLOAD))
				return false;
		}

		if (v.current_order.type == Order.OT_GOTO_DEPOT &&
				(v.current_order.flags & (Order.OF_PART_OF_ORDERS | Order.OF_SERVICE_IF_NEEDED)) ==  (Order.OF_PART_OF_ORDERS | Order.OF_SERVICE_IF_NEEDED) &&
				!v.VehicleNeedsService()) {
			v.cur_order_index++;
		}

		// check if we've reached the waypoint?
		if (v.current_order.type == Order.OT_GOTO_WAYPOINT && v.tile == v.dest_tile) {
			v.cur_order_index++;
		}

		// check if we've reached a non-stop station while TTDPatch nonstop is enabled..
		if (Global._patches.new_nonstop &&
				v.current_order.flags & Order.OF_NON_STOP &&
				v.tile.IsTileType( TileTypes.MP_STATION) &&
				v.current_order.station == _m[v.tile].m2) {
			v.cur_order_index++;
		}

		// Get the current order
		if (v.cur_order_index >= v.num_orders) v.cur_order_index = 0;

		order = Vehicle.GetVehicleOrder(v, v.cur_order_index);

		// If no order, do nothing.
		if (order == null) {
			v.current_order.type = Order.OT_NOTHING;
			v.current_order.flags = 0;
			v.dest_tile = 0;
			return false;
		}

		// If it is unchanged, keep it.
		if (order.type    == v.current_order.type &&
				order.flags   == v.current_order.flags &&
				order.station == v.current_order.station)
			return false;

		// Otherwise set it, and determine the destination tile.
		v.current_order = *order;

		v.dest_tile = 0;

		// store the station length if no shorter station was visited this order round
		if (v.cur_order_index == 0) {
			if (v.rail.shortest_platform[1] != 0 && v.rail.shortest_platform[1] != 255) {
				// we went though a whole round of orders without interruptions, so we store the length of the shortest station
				v.rail.shortest_platform[0] = v.rail.shortest_platform[1];
			}
			// all platforms are shorter than 255, so now we can find the shortest in the next order round. They might have changed size
			v.rail.shortest_platform[1] = 255;
		}

		if (v.last_station_visited != INVALID_STATION) {
			Station st = Station.GetStation(v.last_station_visited);
			if (TileBelongsToRailStation(st, v.tile)) {
				byte length = GetStationPlatforms(st, v.tile);

				if (length < v.rail.shortest_platform[1]) {
					v.rail.shortest_platform[1] = length;
				}
			}
		}

		result = false;
		switch (order.type) {
		case Order.OT_GOTO_STATION:
			if (order.station == v.last_station_visited)
				v.last_station_visited = INVALID_STATION;
			v.dest_tile = Station.GetStation(order.station).xy;
			result = CheckReverseTrain(v);
			break;

		case Order.OT_GOTO_DEPOT:
			v.dest_tile = GetDepot(order.station).xy;
			result = CheckReverseTrain(v);
			break;

		case Order.OT_GOTO_WAYPOINT:
			v.dest_tile = GetWaypoint(order.station).xy;
			result = CheckReverseTrain(v);
			break;
		}

		InvalidateVehicleOrder(v);

		return result;
	}

	static void MarkTrainDirty(Vehicle v)
	{
		do {
			v.cur_image = GetTrainImage(v, v.direction);
			MarkAllViewportsDirty(v.left_coord, v.top_coord, v.right_coord + 1, v.bottom_coord + 1);
		} while ((v = v.next) != null);
	}

	static void HandleTrainLoading(Vehicle v, boolean mode)
	{
		if (v.current_order.type == Order.OT_NOTHING) return;

		if (v.current_order.type != Order.OT_DUMMY) {
			if (v.current_order.type != Order.OT_LOADING) return;
			if (mode) return;

			// don't mark the train as lost if we're loading on the final station.
			if (v.current_order.flags & Order.OF_NON_STOP)
				v.rail.days_since_order_progr = 0;

			if (--v.load_unload_time_rem) return;

			if (v.current_order.flags & Order.OF_FULL_LOAD && CanFillVehicle(v)) {
				v.rail.days_since_order_progr = 0; /* Prevent a train lost message for full loading trains */
				Player.SET_EXPENSES_TYPE(Player.EXPENSES_TRAIN_INC);
				if (LoadUnloadVehicle(v)) {
					Window.InvalidateWindow(Window.WC_TRAINS_LIST, v.owner);
					MarkTrainDirty(v);

					// need to update acceleration and cached values since the goods on the train changed.
					TrainCargoChanged(v);
					UpdateTrainAcceleration(v);
				}
				return;
			}

			TrainPlayLeaveStationSound(v);

			{
				Order b = v.current_order;
				v.current_order.type = Order.OT_LEAVESTATION;
				v.current_order.flags = 0;

				// If this was not the final order, don't remove it from the list.
				if (!(b.flags & Order.OF_NON_STOP)) return;
			}
		}

		v.rail.days_since_order_progr = 0;
		v.cur_order_index++;
		InvalidateVehicleOrder(v);
	}

	static int UpdateTrainSpeed(Vehicle v)
	{
		int spd;
		int accel;

		if ( 0 != (v.vehstatus & Vehicle.VS_STOPPED) || BitOps.HASBIT(v.rail.flags, Vehicle.VRF_REVERSING)) {
			if (Global._patches.realistic_acceleration) {
				accel = GetTrainAcceleration(v, AM_BRAKE) * 2;
			} else {
				accel = v.acceleration * -2;
			}
		} else {
			if (Global._patches.realistic_acceleration) {
				accel = GetTrainAcceleration(v, AM_ACCEL);
			} else {
				accel = v.acceleration;
			}
		}

		spd = v.subspeed + accel * 2;
		v.subspeed = (byte)spd;
		{
			int tempmax = v.max_speed;
			if (v.cur_speed > v.max_speed)
				tempmax = v.cur_speed - (v.cur_speed / 10) - 1;
			v.cur_speed = spd = BitOps.clamp(v.cur_speed + ((int)spd >> 8), 0, tempmax);
		}

		if (0 == (v.direction & 1)) spd = spd * 3 >> 2;

				spd += v.progress;
				v.progress = (byte)spd;
				return (spd >> 8);
	}

	static void TrainEnterStation(Vehicle v, StationID station)
	{
		Station st;
		int flags;

		v.last_station_visited = station.id;

		/* check if a train ever visited this station before */
		st = Station.GetStation(station.id);
		if (0 == (st.had_vehicle_of_type & Station.HVOT_TRAIN)) {
			st.had_vehicle_of_type |= Station.HVOT_TRAIN;
			Global.SetDParam(0, st.index);
			flags = (v.owner == Global._local_player) ? NewsItem.NEWS_FLAGS(NewsItem.NM_THIN, NewsItem.NF_VIEWPORT|NewsItem.NF_VEHICLE, NewsItem.NT_ARRIVAL_PLAYER, 0) : NewsItem.NEWS_FLAGS(NewsItem.NM_THIN, NewsItem.NF_VIEWPORT|NewsItem.NF_VEHICLE, NewsItem.NT_ARRIVAL_OTHER, 0);
			NewsItem.AddNewsItem(
					Str.STR_8801_CITIZENS_CELEBRATE_FIRST,
					flags,
					v.index,
					0);
		}

		// Did we reach the final destination?
		if (v.current_order.type == Order.OT_GOTO_STATION &&
				v.current_order.station == station.id) {
			// Yeah, keep the load/unload flags
			// Non Stop now means if the order should be increased.
			v.current_order.type = Order.OT_LOADING;
			v.current_order.flags &= Order.OF_FULL_LOAD | Order.OF_UNLOAD | Order.OF_TRANSFER;
			v.current_order.flags |= Order.OF_NON_STOP;
		} else {
			// No, just do a simple load
			v.current_order.type = Order.OT_LOADING;
			v.current_order.flags = 0;
		}
		v.current_order.station = 0;

		Player.SET_EXPENSES_TYPE(Player.EXPENSES_TRAIN_INC);
		if (Economy.LoadUnloadVehicle(v) != 0) {
			Window.InvalidateWindow(Window.WC_TRAINS_LIST, v.owner);
			MarkTrainDirty(v);
			Train.TrainCargoChanged(v);
			UpdateTrainAcceleration(v);
		}
		Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
	}

	static int AfterSetTrainPos(Vehicle v, boolean new_tile)
	{
		int new_z, old_z;

		// need this hint so it returns the right z coordinate on bridges.
		Global._get_z_hint = v.z_pos;
		new_z = Landscape.GetSlopeZ(v.x_pos, v.y_pos);
		Global._get_z_hint = 0;

		old_z = v.z_pos;
		v.z_pos = new_z;

		if (new_tile) {
			v.rail.flags = BitOps.RETCLRBIT(v.rail.flags, Vehicle.VRF_GOINGUP);
			v.rail.flags = BitOps.RETCLRBIT(v.rail.flags, Vehicle.VRF_GOINGDOWN);

			if (new_z != old_z) {
				TileIndex tile = TileIndex.TileVirtXY(v.x_pos, v.y_pos);

				// XXX workaround, whole UP/DOWN detection needs overhaul
				if (!tile.IsTileType( TileTypes.MP_TUNNELBRIDGE) || (tile.getMap().m5 & 0x80) != 0)
					v.rail.flags = BitOps.RETSETBIT(v.rail.flags, (new_z > old_z) ? Vehicle.VRF_GOINGUP : Vehicle.VRF_GOINGDOWN);
			}
		}

		v.VehiclePositionChanged();
		v.EndVehicleMove();
		return old_z;
	}


	static int GetNewVehicleDirectionByTile(TileIndex new_tile, TileIndex old_tile)
	{
		int offs = (new_tile.TileY() - old_tile.TileY() + 1) * 4 +
				new_tile.TileX() - old_tile.TileX() + 1;
		assert(offs < 11);
		return _new_vehicle_direction_table[offs];
	}

	static int GetNewVehicleDirection(final Vehicle v, int x, int y)
	{
		int offs = (y - v.y_pos + 1) * 4 + (x - v.x_pos + 1);
		assert(offs < 11);
		return _new_vehicle_direction_table[offs];
	}

	static int GetDirectionToVehicle(final Vehicle v, int x, int y)
	{
		int offs;

		x -= v.x_pos;
		if (x >= 0) {
			offs = (x > 2) ? 0 : 1;
		} else {
			offs = (x < -2) ? 2 : 1;
		}

		y -= v.y_pos;
		if (y >= 0) {
			offs += ((y > 2) ? 0 : 1) * 4;
		} else {
			offs += ((y < -2) ? 2 : 1) * 4;
		}

		assert(offs < 11);
		return _new_vehicle_direction_table[offs];
	}

	/* Check if the vehicle is compatible with the specified tile */
	static boolean CheckCompatibleRail(final Vehicle v, TileIndex tile)
	{
		switch (tile.GetTileType()) {
		case MP_RAILWAY:
		case MP_STATION:
			// normal tracks, jump to owner check
			break;

		case MP_TUNNELBRIDGE:
			if ((tile.getMap().m5 & 0xC0) == 0xC0) { // is bridge middle part?
				IntContainer ic = new IntContainer();
				int tileh = tile.GetTileSlope(ic);
				int height = ic.v;

				// correct Z position of a train going under a bridge on slopes
				if (TileIndex.CorrectZ(tileh)) height += 8;

				if (v.z_pos != height) return true; // train is going over bridge
			}
			break;

		case MP_STREET:
			// tracks over roads, do owner check of tracks
			return
					tile.IsTileOwner( v.owner) && (
							!v.IsFrontEngine() ||
							Rail.IsCompatibleRail(v.rail.railtype, BitOps.GB(tile.getMap().m4, 0, 4))
							);

		default:
			return true;
		}

		return
				tile.IsTileOwner( v.owner) && (
						!v.IsFrontEngine() ||
						Rail.IsCompatibleRail(v.rail.railtype, Rail.GetRailType(tile))
						);
	}


	/* Modify the speed of the vehicle due to a turn */
	static void AffectSpeedByDirChange(Vehicle v, byte new_dir)
	{
		int diff;
		final RailtypeSlowdownParams rsp;

		if (Global._patches.realistic_acceleration || (diff = (v.direction - new_dir) & 7) == 0)
			return;

		rsp = _railtype_slowdown[v.rail.railtype];
		v.cur_speed -= ((diff == 1 || diff == 7) ? rsp.small_turn : rsp.large_turn) * v.cur_speed >> 8;
	}

	/* Modify the speed of the vehicle due to a change in altitude */
	static void AffectSpeedByZChange(Vehicle v, byte old_z)
	{
		final RailtypeSlowdownParams rsp;
		if (old_z == v.z_pos || Global._patches.realistic_acceleration) return;

		rsp = _railtype_slowdown[v.rail.railtype];

		if (old_z < v.z_pos) {
			v.cur_speed -= (v.cur_speed * rsp.z_up >> 8);
		} else {
			int spd = v.cur_speed + rsp.z_down;
			if (spd <= v.max_speed) v.cur_speed = spd;
		}
	}


	static void TrainMovedChangeSignals(TileIndex tile, int dir)
	{
		if (tile.IsTileType( TileTypes.MP_RAILWAY) && (tile.getMap().m5 & 0xC0) == 0x40) {
			int i = BitOps.FindFirstBit2x64((tile.getMap().m5 + (tile.getMap().m5 << 8)) & _reachable_tracks[dir]);
			UpdateSignalsOnSegment(tile, _otherside_signal_directions[i]);
		}
	}


	class TrainCollideChecker {
		final Vehicle v;
		final Vehicle v_skip;
	}

	static Object FindTrainCollideEnum(Vehicle v, Object data)
	{
		final TrainCollideChecker tcc = (TrainCollideChecker) data;

		if (v != tcc.v &&
				v != tcc.v_skip &&
				v.type == Vehicle.VEH_Train &&
				v.rail.track != 0x80 &&
				Math.abs(v.z_pos - tcc.v.z_pos) <= 6 &&
				Math.abs(v.x_pos - tcc.v.x_pos) < 6 &&
				Math.abs(v.y_pos - tcc.v.y_pos) < 6) {
			return v;
		} else {
			return null;
		}
	}

	static void SetVehicleCrashed(Vehicle v)
	{
		Vehicle u;

		if (v.rail.crash_anim_pos != 0)
			return;

		v.rail.crash_anim_pos++;

		u = v;
		//BEGIN_ENUM_WAGONS(v)
		do {
			v.vehstatus |= Vehicle.VS_CRASHED;
		} while ( (v=v.next) != null);
		//END_ENUM_WAGONS(v)

		Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, u.index, STATUS_BAR);
	}

	static int CountPassengersInTrain(final Vehicle  v)
	{
		int num = 0;
		//BEGIN_ENUM_WAGONS(v)
		do {
			if (v.cargo_type == AcceptedCargo.CT_PASSENGERS) num += v.cargo_count;
		} while ( (v=v.next) != null);
		//END_ENUM_WAGONS(v)
		return num;
	}

	/*
	 * Checks whether the specified train has a collision with another vehicle. If
	 * so, destroys this vehicle, and the other vehicle if its subtype has TS_Front.
	 * Reports the incident in a flashy news item, modifies station ratings and
	 * plays a sound.
	 */
	static void CheckTrainCollision(Vehicle v)
	{
		TrainCollideChecker tcc = new TrainCollideChecker();
		Vehicle coll;
		Vehicle realcoll;
		int num;

		/* can't collide in depot */
		if (v.rail.track == 0x80) return;

		assert(v.rail.track == 0x40 || TileIndex.TileVirtXY(v.x_pos, v.y_pos) == v.tile);

		tcc.v = v;
		tcc.v_skip = v.next;

		/* find colliding vehicle */
		realcoll = (Vehicle) Vehicle.VehicleFromPos(TileIndex.TileVirtXY(v.x_pos, v.y_pos), tcc, TrainCmd::FindTrainCollideEnum);
		if (realcoll == null) return;

		coll = realcoll.GetFirstVehicleInChain();

		/* it can't collide with its own wagons */
		if (v == coll ||
				( 0 != (v.rail.track & 0x40) && (v.direction & 2) != (realcoll.direction & 2)))
			return;

		//two drivers + passangers killed in train v
		num = 2 + CountPassengersInTrain(v);
		if (0 == (coll.vehstatus & Vehicle.VS_CRASHED))
			//two drivers + passangers killed in train coll (if it was not crashed already)
			num += 2 + CountPassengersInTrain(coll);

		SetVehicleCrashed(v);
		if (coll.IsFrontEngine()) SetVehicleCrashed(coll);

		Global.SetDParam(0, num);
		NewsItem.AddNewsItem(Str.STR_8868_TRAIN_CRASH_DIE_IN_FIREBALL,
				NewsItem.NEWS_FLAGS(NewsItem.NM_THIN, NewsItem.NF_VIEWPORT | NewsItem.NF_VEHICLE, NewsItem.NT_ACCIDENT, 0),
				v.index,
				0
				);

		Station.ModifyStationRatingAround(v.tile, v.owner, -160, 30);
		//SndPlayVehicleFx(SND_13_BIG_CRASH, v);
	}


	static Object CheckVehicleAtSignal(Vehicle v, Object data)
	{
		final VehicleAtSignalData vasd = (VehicleAtSignalData) data;

		if (v.type == Vehicle.VEH_Train && v.IsFrontEngine() &&
				v.tile.tile == vasd.tile) {
			int diff = (v.direction - vasd.direction + 2) & 7;

			if (diff == 2 || (v.cur_speed <= 5 && diff <= 4))
				return v;
		}
		return null;
	}

	static void TrainController(Vehicle v)
	{
		Vehicle prev;
		GetNewVehiclePosResult gp = new GetNewVehiclePosResult();
		int r, tracks,ts;
		int i, enterdir, newdir, dir;
		byte chosen_dir;
		byte chosen_track;
		byte old_z;

		/* For every vehicle after and including the given vehicle */
		for (prev = GetPrevVehicleInChain(v); v != null; prev = v, v = v.next) {
			v.BeginVehicleMove();

			if (v.rail.track != 0x40) {
				/* Not inside tunnel */
				if (GetNewVehiclePos(v, gp)) {
					/* Staying in the old tile */
					if (v.rail.track == 0x80) {
						/* inside depot */
						gp.x = v.x_pos;
						gp.y = v.y_pos;
					} else {
						/* is not inside depot */

						if ((prev == null) && (!TrainCheckIfLineEnds(v)))
							return;

						r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
						if (r & 0x8) {
							//debug("%x & 0x8", r);
							goto invalid_rail;
						}
						if (r & 0x2) {
							TrainEnterStation(v, r >> 8);
							return;
						}

						if (v.current_order.type == Order.OT_LEAVESTATION) {
							v.current_order.type = Order.OT_NOTHING;
							v.current_order.flags = 0;
							Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
						}
					}
				} else {
					/* A new tile is about to be entered. */

					byte bits;
					/* Determine what direction we're entering the new tile from */
					dir = GetNewVehicleDirectionByTile(gp.new_tile, gp.old_tile);
					enterdir = dir >> 1;
		assert(enterdir==0 || enterdir==1 || enterdir==2 || enterdir==3);

		/* Get the status of the tracks in the new tile and mask
		 * away the bits that aren't reachable. */
		ts = GetTileTrackStatus(gp.new_tile, TRANSPORT_RAIL) & _reachable_tracks[enterdir];

		/* Combine the from & to directions.
		 * Now, the lower byte contains the track status, and the byte at bit 16 contains
		 * the signal status. */
		tracks = ts|(ts >> 8);
		bits = tracks & 0xFF;
		if (Global._patches.new_pathfinding_all && Global._patches.forbid_90_deg && prev == null)
			/* We allow wagons to make 90 deg turns, because forbid_90_deg
			 * can be switched on halfway a turn */
			bits &= ~TrackCrossesTracks(FIND_FIRST_BIT(v.rail.track));

		if ( bits == 0) {
			//debug("%x == 0", bits);
			goto invalid_rail;
		}

		/* Check if the new tile contrains tracks that are compatible
		 * with the current train, if not, bail out. */
		if (!CheckCompatibleRail(v, gp.new_tile)) {
			//debug("!CheckCompatibleRail(%p, %x)", v, gp.new_tile);
			goto invalid_rail;
		}

		if (prev == null) {
			byte trackdir;
			/* Currently the locomotive is active. Determine which one of the
			 * available tracks to choose */
			chosen_track = 1 << ChooseTrainTrack(v, gp.new_tile, enterdir, bits);
			assert(chosen_track & tracks);

			trackdir = TrackEnterdirToTrackdir(FIND_FIRST_BIT(chosen_track), enterdir);
			assert(trackdir != 0xff);

			if (PBSIsPbsSignal(gp.new_tile,trackdir) && PBSIsPbsSegment(gp.new_tile,trackdir)) {
				// encountered a pbs signal, and possible a pbs block
				Global.DEBUG_pbs(3, "pbs: (%i) arrive AT signal, tile:%x  pbs_stat:%i",v.unitnumber, gp.new_tile, v.rail.pbs_status);

				if (v.rail.pbs_status == PBS_STAT_NONE) {
					// we havent planned a path already, so try to find one now
					NPFFindStationOrTileData fstd;
					NPFFoundTargetData ftd;

					NPFFillWithOrderData(&fstd, v);

					Global.DEBUG_pbs(2, "pbs: (%i) choose signal (TC), tile:%x, trackdir:%i",v.unitnumber,  gp.new_tile, trackdir);
					ftd = NPFRouteToStationOrTile(gp.new_tile, trackdir, &fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_GREEN);

					if (v.rail.force_proceed != 0)
						goto green_light;

					if (ftd.best_trackdir == 0xFF)
						goto red_light;

					// we found a way out of the pbs block
					if (NPFGetFlag(&ftd.node, NPF_FLAG_PBS_EXIT)) {
						if (NPFGetFlag(&ftd.node, NPF_FLAG_PBS_BLOCKED) || NPFGetFlag(&ftd.node, NPF_FLAG_PBS_RED))
							goto red_light;
						else {
							v.rail.pbs_end_tile = ftd.node.tile;
							v.rail.pbs_end_trackdir = ftd.node.direction;
							goto green_light;
						}

					};

				} else {
					// we have already planned a path through this pbs block
					// on entering the block, we reset our status
					v.rail.pbs_status = PBS_STAT_NONE;
					goto green_light;
				};
				Global.DEBUG_pbs(3, "pbs: (%i) no green light found, or was no pbs-block",v.unitnumber);
			};

			/* Check if it's a red signal and that force proceed is not clicked. */
			if ( (tracks>>16)&chosen_track && v.rail.force_proceed == 0) goto red_light;
		} else {

			/* The wagon is active, simply follow the prev vehicle. */
			chosen_track = (byte)(_matching_tracks[GetDirectionToVehicle(prev, gp.x, gp.y)] & bits);
		}
		green_light:
			if (v.next == null)
				PBSClearTrack(gp.old_tile, FIND_FIRST_BIT(v.rail.track));

		/* make sure chosen track is a valid track */
		assert(chosen_track==1 || chosen_track==2 || chosen_track==4 || chosen_track==8 || chosen_track==16 || chosen_track==32);

		/* Update XY to reflect the entrance to the new tile, and select the direction to use */
		{
			final byte *b = _initial_tile_subcoord[FIND_FIRST_BIT(chosen_track)][enterdir];
			gp.x = (gp.x & ~0xF) | b[0];
			gp.y = (gp.y & ~0xF) | b[1];
			chosen_dir = b[2];
		}

		/* Call the landscape function and tell it that the vehicle entered the tile */
		r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
		if (r&0x8){
			//debug("%x & 0x8", r);
			goto invalid_rail;
		}

		if (v.IsFrontEngine()) v.load_unload_time_rem = 0;

		if (!(r&0x4)) {
			v.tile = gp.new_tile;
			v.rail.track = chosen_track;
			assert(v.rail.track);
		}

		if (v.IsFrontEngine())
			TrainMovedChangeSignals(gp.new_tile, enterdir);

		/* Signals can only change when the first
		 * (above) or the last vehicle moves. */
		if (v.next == null)
			TrainMovedChangeSignals(gp.old_tile, (enterdir) ^ 2);

		if (prev == null) {
			AffectSpeedByDirChange(v, chosen_dir);
		}

		v.direction = chosen_dir;
				}
			} else {
				/* in tunnel */
				GetNewVehiclePos(v, &gp);

				// Check if to exit the tunnel...
				if (!IsTunnelTile(gp.new_tile) ||
						!(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y)&0x4) ) {
					v.x_pos = gp.x;
					v.y_pos = gp.y;
					v.VehiclePositionChanged();
					continue;
				}
			}

			/* update image of train, as well as delta XY */
			newdir = GetNewVehicleDirection(v, gp.x, gp.y);
			UpdateTrainDeltaXY(v, newdir);
			v.cur_image = GetTrainImage(v, newdir);

			v.x_pos = gp.x;
			v.y_pos = gp.y;

			/* update the Z position of the vehicle */
			old_z = AfterSetTrainPos(v, (gp.new_tile != gp.old_tile));

			if (prev == null) {
				/* This is the first vehicle in the train */
				AffectSpeedByZChange(v, old_z);
			}
		}
		return;

		invalid_rail:
			/* We've reached end of line?? */
			if (prev != null) error("!Disconnecting train");
		goto reverse_train_direction;

		red_light: {
			/* We're in front of a red signal ?? */
			/* find the first set bit in ts. need to do it in 2 steps, since
			 * FIND_FIRST_BIT only handles 6 bits at a time. */
			i = FindFirstBit2x64(ts);

			if (!(_m[gp.new_tile].m3 & SignalAgainstTrackdir(i))) {
				v.cur_speed = 0;
				v.subspeed = 0;
				v.progress = 255-100;
				if (++v.load_unload_time_rem < Global._patches.wait_oneway_signal * 20)
					return;
			} else if (_m[gp.new_tile].m3 & SignalAlongTrackdir(i)){
				v.cur_speed = 0;
				v.subspeed = 0;
				v.progress = 255-10;
				if (++v.load_unload_time_rem < Global._patches.wait_twoway_signal * 73) {
					TileIndex o_tile = gp.new_tile + TileOffsByDir(enterdir);
					VehicleAtSignalData vasd;
					vasd.tile = o_tile;
					vasd.direction = dir ^ 4;

					/* check if a train is waiting on the other side */
					if (VehicleFromPos(o_tile, &vasd, CheckVehicleAtSignal) == null)
						return;
				}
			}
		}

		reverse_train_direction:
			v.load_unload_time_rem = 0;
		v.cur_speed = 0;
		v.subspeed = 0;
		ReverseTrainDirection(v);
	}

	//extern TileIndex CheckTunnelBusy(TileIndex tile, int *length);

	/**
	 * Deletes/Clears the last wagon of a crashed train. It takes the engine of the
	 * train, then goes to the last wagon and deletes that. Each call to this function
	 * will remove the last wagon of a crashed train. If this wagon was on a crossing,
	 * or inside a tunnel, recalculate the signals as they might need updating
	 * @param v the @Vehicle of which last wagon is to be removed
	 */
	static void DeleteLastWagon(Vehicle v)
	{
		Vehicle u = v;

		/* Go to the last wagon and delete the link pointing there
		 * *u is then the one-before-last wagon, and *v the last
		 * one which will physicially be removed */
		for (; v.next != null; v = v.next) u = v;
		u.next = null;

		Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);
		Window.DeleteWindowById(Window.WC_VEHICLE_VIEW, v.index);
		RebuildVehicleLists();
		Window.InvalidateWindow(Window.WC_COMPANY, v.owner);

		v.BeginVehicleMove();
		EndVehicleMove(v);
		v.DeleteVehicle();

		// clear up reserved pbs tracks
		if (PBSTileReserved(v.tile) & v.rail.track) {
			if (v == u) {
				PBSClearPath(v.tile, FIND_FIRST_BIT(v.rail.track), v.rail.pbs_end_tile, v.rail.pbs_end_trackdir);
				PBSClearPath(v.tile, FIND_FIRST_BIT(v.rail.track) + 8, v.rail.pbs_end_tile, v.rail.pbs_end_trackdir);
			};
			if (v.tile != u.tile) {
				PBSClearTrack(v.tile, FIND_FIRST_BIT(v.rail.track));
			};
		}

		if (!(v.rail.track & 0xC0))
			SetSignalsOnBothDir(v.tile, FIND_FIRST_BIT(v.rail.track));

		/* Check if the wagon was on a road/rail-crossing and disable it if no
		 * others are on it */
		DisableTrainCrossing(v.tile);

		if (v.rail.track == 0x40) { // inside a tunnel
			TileIndex endtile = CheckTunnelBusy(v.tile, null);

			if (endtile == INVALID_TILE) // tunnel is busy (error returned)
				return;

			switch (v.direction) {
			case 1:
			case 5:
				SetSignalsOnBothDir(v.tile, 0);
				SetSignalsOnBothDir(endtile, 0);
				break;

			case 3:
			case 7:
				SetSignalsOnBothDir(v.tile, 1);
				SetSignalsOnBothDir(endtile, 1);
				break;

			default:
				break;
			}
		}
	}

	static void ChangeTrainDirRandomly(Vehicle v)
	{
		static int8 _random_dir_change[4] = { -1, 0, 0, 1};

		do {
			//I need to buffer the train direction
			if (!(v.rail.track & 0x40))
				v.direction = (v.direction + _random_dir_change[BitOps.GB(Hal.Random(), 0, 2)]) & 7;
			if (!(v.vehstatus & Vehicle.VS_HIDDEN)) {
				v.BeginVehicleMove();
				UpdateTrainDeltaXY(v, v.direction);
				v.cur_image = GetTrainImage(v, v.direction);
				AfterSetTrainPos(v, false);
			}
		} while ( (v=v.next) != null);
	}

	static void HandleCrashedTrain(Vehicle v)
	{
		int state = ++v.rail.crash_anim_pos, index;
		int r;
		Vehicle u;

		if (state == 4 && v.rail.track != 0x40) {
			CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
		}

		if (state <= 200 && BitOps.CHANCE16R(1, 7, r)) {
			index = (r * 10 >> 16);

			u = v;
			do {
				if (--index < 0) {
					r = Hal.Random();

					CreateEffectVehicleRel(u,
							BitOps.GB(r,  8, 3) + 2,
							BitOps.GB(r, 16, 3) + 2,
							BitOps.GB(r,  0, 3) + 5,
							EV_EXPLOSION_SMALL);
					break;
				}
			} while ( (u=u.next) != null);
		}

		if (state <= 240 && !(v.tick_counter&3)) {
			ChangeTrainDirRandomly(v);
		}

		if (state >= 4440 && !(v.tick_counter&0x1F)) {
			DeleteLastWagon(v);
			Window.InvalidateWindow(Window.WC_REPLACE_VEHICLE, Vehicle.VEH_Train);
		}
	}

	static void HandleBrokenTrain(Vehicle v)
	{
		if (v.breakdown_ctr != 1) {
			v.breakdown_ctr = 1;
			v.cur_speed = 0;

			if (v.breakdowns_since_last_service != 255)
				v.breakdowns_since_last_service++;

			Window.InvalidateWindow(Window.WC_VEHICLE_VIEW, v.index);
			Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);

			//SndPlayVehicleFx((GameOptions._opt.landscape != Landscape.LT_CANDY) ?			SND_10_TRAIN_BREAKDOWN : SND_3A_COMEDY_BREAKDOWN_2, v);

			if (!(v.vehstatus & Vehicle.VS_HIDDEN)) {
				Vehicle u = CreateEffectVehicleRel(v, 4, 4, 5, EV_BREAKDOWN_SMOKE);
				if (u != null) u.special.unk0 = v.breakdown_delay * 2;
			}
		}

		if (!(v.tick_counter & 3)) {
			if (!--v.breakdown_delay) {
				v.breakdown_ctr = 0;
				Window.InvalidateWindow(Window.WC_VEHICLE_VIEW, v.index);
			}
		}
	}


	static boolean TrainCheckIfLineEnds(Vehicle v)
	{
		TileIndex tile;
		int x,y;
		int break_speed;
		int t;
		int ts;
		byte trackdir;

		t = v.breakdown_ctr;
		if (t > 1) {
			v.vehstatus |= Vehicle.VS_TRAIN_SLOWING;

			break_speed = _breakdown_speeds[BitOps.GB(~t, 4, 4)];
			if (break_speed < v.cur_speed) v.cur_speed = break_speed;
		} else {
			v.vehstatus &= ~Vehicle.VS_TRAIN_SLOWING;
		}

		if (v.rail.track & 0x40) return true; // exit if inside a tunnel
		if (v.rail.track & 0x80) return true; // exit if inside a depot

		tile = v.tile;

		// tunnel entrance?
		if (tile.IsTunnelTile() && BitOps.GB(tile.getMap().m5, 0, 2) * 2 + 1 == v.direction)
			return true;

		// depot?
		/* XXX -- When enabled, this makes it possible to crash trains of others
	     (by building a depot right against a station) */
		/*	if (tile.IsTileType( TileTypes.MP_RAILWAY) && (tile.getMap().m5 & 0xFC) == 0xC0)
		return true;*/

		/* Determine the non-diagonal direction in which we will exit this tile */
		t = v.direction >> 1;
			if (!(v.direction & 1) && v.rail.track != _state_dir_table[t]) {
				t = (t - 1) & 3;
			}
			/* Calculate next tile */
			tile += TileOffsByDir(t);
			// determine the track status on the next tile.
			ts = GetTileTrackStatus(tile, TRANSPORT_RAIL) & _reachable_tracks[t];

			// if there are tracks on the new tile, pick one (trackdir will only be used when its a signal tile, in which case only 1 trackdir is accessible for us)
			if (ts & TRACKDIR_BIT_MASK)
				trackdir = FindFirstBit2x64(ts & TRACKDIR_BIT_MASK);
			else
				trackdir = INVALID_TRACKDIR;

			/* Calc position within the current tile ?? */
			x = v.x_pos & 0xF;
			y = v.y_pos & 0xF;

			switch (v.direction) {
			case 0:
				x = (~x) + (~y) + 24;
				break;
			case 7:
				x = y;
				/* fall through */
			case 1:
				x = (~x) + 16;
				break;
			case 2:
				x = (~x) + y + 8;
				break;
			case 3:
				x = y;
				break;
			case 4:
				x = x + y - 8;
				break;
			case 6:
				x = (~y) + x + 8;
				break;
			}

			if (BitOps.GB(ts, 0, 16) != 0) {
				/* If we approach a rail-piece which we can't enter, don't enter it! */
				if (x + 4 > 15 && !CheckCompatibleRail(v, tile)) {
					v.cur_speed = 0;
					ReverseTrainDirection(v);
					return false;
				}
				if ((ts &= (ts >> 16)) == 0) {
					// make a rail/road crossing red
					if (tile.IsTileType( TileTypes.MP_STREET) && IsLevelCrossing(tile)) {
						if (BitOps.GB(tile.getMap().m5, 2, 1) == 0) {
							tile.getMap().m5 = BitOps.RETSB(tile.getMap().m5, 2, 1, 1);
							SndPlayVehicleFx(SND_0E_LEVEL_CROSSING, v);
							tile.MarkTileDirtyByTile();
						}
					}
					return true;
				}
			} else if (x + 4 > 15) {
				v.cur_speed = 0;
				ReverseTrainDirection(v);
				return false;
			}

			if  (v.rail.pbs_status == PBS_STAT_HAS_PATH)
				return true;

			if ((trackdir != INVALID_TRACKDIR) && (PBSIsPbsSignal(tile,trackdir) && PBSIsPbsSegment(tile,trackdir)) && !(v.tile.IsTileType( TileTypes.MP_STATION) && (v.current_order.station == _m[v.tile].m2))) {
				NPFFindStationOrTileData fstd;
				NPFFoundTargetData ftd;

				NPFFillWithOrderData(&fstd, v);

				Global.DEBUG_pbs(2, "pbs: (%i) choose signal (CEOL), tile:%x  trackdir:%i", v.unitnumber, tile, trackdir);
				ftd = NPFRouteToStationOrTile(tile, trackdir, &fstd, TRANSPORT_RAIL, v.owner, v.rail.railtype, PBS_MODE_GREEN);

				if (ftd.best_trackdir != 0xFF && NPFGetFlag(&ftd.node, NPF_FLAG_PBS_EXIT)) {
					if (!(NPFGetFlag(&ftd.node, NPF_FLAG_PBS_BLOCKED) || NPFGetFlag(&ftd.node, NPF_FLAG_PBS_RED))) {
						v.rail.pbs_status = PBS_STAT_HAS_PATH;
						v.rail.pbs_end_tile = ftd.node.tile;
						v.rail.pbs_end_trackdir = ftd.node.direction;
						return true;
					}
				};
			};

			// slow down
			v.vehstatus |= Vehicle.VS_TRAIN_SLOWING;
			break_speed = _breakdown_speeds[x & 0xF];
			if (!(v.direction&1)) break_speed >>= 1;
			if (break_speed < v.cur_speed) v.cur_speed = break_speed;

			return true;
	}

	static void TrainLocoHandler(Vehicle v, boolean mode)
	{
		int j;

		/* train has crashed? */
		if (v.rail.crash_anim_pos != 0) {
			if (!mode) HandleCrashedTrain(v);
			return;
		}

		if (v.rail.force_proceed != 0)
			v.rail.force_proceed--;

		/* train is broken down? */
		if (v.breakdown_ctr != 0) {
			if (v.breakdown_ctr <= 2) {
				HandleBrokenTrain(v);
				return;
			}
			v.breakdown_ctr--;
		}

		if (BitOps.HASBIT(v.rail.flags, Vehicle.VRF_REVERSING) && v.cur_speed == 0) {
			ReverseTrainDirection(v);
		}

		/* exit if train is stopped */
		if (v.vehstatus & Vehicle.VS_STOPPED && v.cur_speed == 0)
			return;


		if (ProcessTrainOrder(v)) {
			v.load_unload_time_rem = 0;
			v.cur_speed = 0;
			v.subspeed = 0;
			ReverseTrainDirection(v);
			return;
		}

		HandleTrainLoading(v, mode);

		if (v.current_order.type == Order.OT_LOADING)
			return;

		if (CheckTrainStayInDepot(v))
			return;

		if (!mode) HandleLocomotiveSmokeCloud(v);

		j = UpdateTrainSpeed(v);
		if (j == 0) {
			// if the vehicle has speed 0, update the last_speed field.
			if (v.cur_speed != 0)
				return;
		} else {
			TrainCheckIfLineEnds(v);

			do {
				TrainController(v);
				CheckTrainCollision(v);
				if (v.cur_speed <= 0x100)
					break;
			} while (--j != 0);
		}

		SetLastSpeed(v, v.cur_speed);
	}


	void Train_Tick(Vehicle v)
	{
		if (_age_cargo_skip_counter == 0 && v.cargo_days != 0xff)
			v.cargo_days++;

		v.tick_counter++;

		if (v.IsFrontEngine()) {
			TrainLocoHandler(v, false);

			// make sure vehicle wasn't deleted.
			if (v.type == Vehicle.VEH_Train && v.IsFrontEngine())
				TrainLocoHandler(v, true);
		} else if (v.IsFreeWagon() && HASBITS(v.vehstatus, Vehicle.VS_CRASHED)) {
			// Delete flooded standalone wagon
			if (++v.rail.crash_anim_pos >= 4400)
				v.DeleteVehicle();
		}
	}



	// Validation for the news item "Train is waiting in depot"
	static boolean ValidateTrainInDepot( int data_a, int data_b )
	{
		Vehicle v = Vehicle.GetVehicle(data_a);
		return  (v.rail.track == 0x80 && (v.vehstatus | Vehicle.VS_STOPPED));
	}

	static void TrainEnterDepot(Vehicle v, TileIndex tile)
	{
		SetSignalsOnBothDir(tile, _depot_track_ind[BitOps.GB(tile.getMap().m5, 0, 2)]);

		if (!v.IsFrontEngine()) v = v.GetFirstVehicleInChain();

		v.VehicleServiceInDepot();

		Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);

		v.load_unload_time_rem = 0;
		v.cur_speed = 0;

		TriggerVehicle(v, VEHICLE_TRIGGER_DEPOT);

		if (v.current_order.type == Order.OT_GOTO_DEPOT) {
			Order t;

			Window.InvalidateWindow(Window.WC_VEHICLE_VIEW, v.index);

			t = v.current_order;
			v.current_order.type = Order.OT_DUMMY;
			v.current_order.flags = 0;

			if (BitOps.HASBIT(t.flags, OFB_PART_OF_ORDERS)) { // Part of the orderlist?
				v.rail.days_since_order_progr = 0;
				v.cur_order_index++;
			} else if (BitOps.HASBIT(t.flags, OFB_HALandscape.LT_IN_DEPOT)) { // User initiated?
				v.vehstatus |= Vehicle.VS_STOPPED;
				if (v.owner == Global._local_player) {
					Global.SetDParam(0, v.unitnumber);
					NewsItem.AddValidatedNewsItem(
							Str.STR_8814_TRAIN_IS_WAITING_IN_DEPOT,
							NewsItem.NEWS_FLAGS(NewsItem.NM_SMALL, NewsItem.NF_VIEWPORT|NewsItem.NF_VEHICLE, NewsItem.NT_ADVICE, 0),
							v.index,
							0,
							ValidateTrainInDepot);
				}
			}
		}
		Window.InvalidateWindowClasses(Window.WC_TRAINS_LIST);
	}

	static void CheckIfTrainNeedsService(Vehicle v)
	{
		final Depot  depot;
		TrainFindDepotData tfdd;

		if (PBSTileReserved(v.tile) & v.rail.track)     return;
		if (v.rail.pbs_status == PBS_STAT_HAS_PATH)      return;
		if (Global._patches.servint_trains == 0)                   return;
		if (!v.VehicleNeedsService())                        return;
		if (v.vehstatus & Vehicle.VS_STOPPED)                      return;
		if (Global._patches.gotodepot && v.VehicleHasDepotOrders()) return;

		// Don't interfere with a depot visit scheduled by the user, or a
		// depot visit by the order list.
		if (v.current_order.type == Order.OT_GOTO_DEPOT &&
				(v.current_order.flags & (Order.OF_HALT_IN_DEPOT | Order.OF_PART_OF_ORDERS)) != 0)
			return;

		tfdd = FindClosestTrainDepot(v);
		/* Only go to the depot if it is not too far out of our way. */
		if (tfdd.best_length == (int)-1 || tfdd.best_length > 16 ) {
			if (v.current_order.type == Order.OT_GOTO_DEPOT) {
				/* If we were already heading for a depot but it has
				 * suddenly moved farther away, we continue our normal
				 * schedule? */
				v.current_order.type = Order.OT_DUMMY;
				v.current_order.flags = 0;
				Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
			}
			return;
		}

		depot = Depot.GetDepotByTile(tfdd.tile);

		if (v.current_order.type == Order.OT_GOTO_DEPOT &&
				v.current_order.station != depot.index &&
				!BitOps.CHANCE16(3,16))
			return;

		v.current_order.type = Order.OT_GOTO_DEPOT;
		v.current_order.flags = Order.OF_NON_STOP;
		v.current_order.station = depot.index;
		v.dest_tile = tfdd.tile;
		Window.InvalidateWindowWidget(Window.WC_VEHICLE_VIEW, v.index, STATUS_BAR);
	}

	static int GetTrainRunningCost(final Vehicle v)
	{
		int cost = 0;

		do {
			final RailVehicleInfo rvi = RailVehInfo(v.engine_type);
			if (rvi.running_cost_base)
				cost += rvi.running_cost_base * Global._price.running_rail[rvi.engclass];
		} while ( (v=v.next) != null );

		return cost;
	}

	void OnNewDay_Train(Vehicle v)
	{
		TileIndex tile;

		if ((++v.day_counter & 7) == 0)
			v.DecreaseVehicleValue();

		if (v.IsFrontEngine()) {
			v.CheckVehicleBreakdown();
			v.AgeVehicle();

			CheckIfTrainNeedsService(v);

			// check if train hasn't advanced in its order list for a set number of days
			if (Global._patches.lost_train_days && v.num_orders && !(v.vehstatus & (Vehicle.VS_STOPPED | Vehicle.VS_CRASHED) ) && ++v.rail.days_since_order_progr >= Global._patches.lost_train_days && v.owner == Global._local_player) {
				v.rail.days_since_order_progr = 0;
				Global.SetDParam(0, v.unitnumber);
				NewsItem.AddNewsItem(
						Str.STR_TRAIN_IS_LOST,
						NewsItem.NEWS_FLAGS(NewsItem.NM_SMALL, NewsItem.NF_VIEWPORT|NewsItem.NF_VEHICLE, NewsItem.NT_ADVICE, 0),
						v.index,
						0);
			}

			Order.CheckOrders(v.index, OC_INIT);

			/* update destination */
			if (v.current_order.type == Order.OT_GOTO_STATION &&
					(tile = Station.GetStation(v.current_order.station).train_tile) != 0) {
				v.dest_tile = tile;
			}

			if ((v.vehstatus & Vehicle.VS_STOPPED) == 0) {
				/* running costs */
				int cost = GetTrainRunningCost(v) / 364;

				v.profit_this_year -= cost >> 8;

				Player.SET_EXPENSES_TYPE(Player.EXPENSES_TRAIN_RUN);
				Player.SubtractMoneyFromPlayerFract(v.owner, cost);

				Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);
				Window.InvalidateWindowClasses(Window.WC_TRAINS_LIST);
			}
		}
	}

	void TrainsYearlyLoop()
	{
		//Vehicle v;

		//FOR_ALL_VEHICLES(v) {
		Iterator<Vehicle> it = Vehicle.getIterator();
		while(it.hasNext())
		{
			final Vehicle  v = it.next();
			if (v.type == Vehicle.VEH_Train && v.IsFrontEngine()) {

				// show warning if train is not generating enough income last 2 years (corresponds to a red icon in the vehicle list)
				if (Global._patches.train_income_warn && v.owner == Global._local_player && v.age >= 730 && v.profit_this_year < 0) {
					Global.SetDParam(1, v.profit_this_year);
					Global.SetDParam(0, v.unitnumber.id);
					NewsItem.AddNewsItem(
							Str.STR_TRAIN_IS_UNPROFITABLE,
							NewsItem.NEWS_FLAGS(NewsItem.NM_SMALL, NewsItem.NF_VIEWPORT|NewsItem.NF_VEHICLE, NewsItem.NT_ADVICE, 0),
							v.index,
							0);
				}

				v.profit_last_year = v.profit_this_year;
				v.profit_this_year = 0;
				Window.InvalidateWindow(Window.WC_VEHICLE_DETAILS, v.index);
			}
		}
	}


	void InitializeTrains()
	{
		_age_cargo_skip_counter = 1;
	}

}