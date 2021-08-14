package game;

import game.struct.VQueueItem;

public abstract class VehicleQueue 
{

	VQueueItem top;
	VQueueItem bottom;

	// Dirty means "position" in VQueueItems is incorrect
	// and needs to be rebuilt.
	boolean dirty;	
	int size;

	// Offset for "position" in queue - allows for O(1) pushes & pops
	int offset;

	// This comment is left here for historical reasons - [dz]
	// Ahh, yes! Classic C functional programming!
	// Should really be converted to C++, though . . .
	abstract boolean	push(Vehicle item);
	abstract Vehicle	pop();
	abstract Vehicle	getTop();
	abstract void		clean();
	abstract void		clear();
	abstract void		del(Vehicle item);
	abstract int		getPos(Vehicle item);

	public VehicleQueue() {
		size = 0;
		offset = 0;
		top = null;
		bottom = null;
		dirty = false;
	}

	static public VehicleQueue new_VQueue()
	{
		return new VQImpl();
	}

}





class VQImpl extends VehicleQueue
{

	@Override
	boolean push(Vehicle v) {
		VQueueItem newItem;

		// Do not push NULLs
		assert(v != null);

		if(size == 0x7FFFFFFF)
			return false;

		newItem = new VQueueItem();

		if(newItem == null)
			return false;

		if(size == 0) {
			assert(top == null);
			assert(bottom == null);
			newItem.data = v;
			newItem.position = 1;
			newItem.queue = this;
			newItem.above = null;
			newItem.below = null;
			bottom = newItem;
			top = newItem;
			v.queue_item = newItem;
			size = 1;
			return true;
		}

		bottom.below = newItem;
		newItem.above = bottom;
		newItem.below = null;
		newItem.data = v;
		newItem.position = bottom.position + 1;
		newItem.queue = this;
		v.queue_item = newItem;
		bottom = newItem;
		size++;
		return true;
		
	}

	@Override
	Vehicle pop() {
		Vehicle v;
		VQueueItem oldItem;
		
		if(size == 0)
			return null;

		oldItem = top;

		assert(oldItem != null);

		top = oldItem.below;
		if(top != null)
			top.above = null;

		// Had one item, now empty
		if(size == 1) {
			bottom = null;
			top = null;
			size = 0;
			offset = 0;
			dirty = false;
			v = oldItem.data;
			v.queue_item = null;
			//free(oldItem);
			return v;
		}

		// Top was above bottom - now top *IS* bottom
		if(size == 2)
			bottom.above = null;

		v = oldItem.data;

		v.queue_item = null;

		//free(oldItem);

		offset++;
		size--;

		if(offset > 0x7FFFFFFF) {
			dirty = true;
			clean();
		}
		return v;
	}

	@Override
	Vehicle getTop() {
		if(size != 0)
			return top.data;
		else
			return null;
	}

	@Override
	void clean() {
		boolean done;
		int currentSize;
		VQueueItem currItem;
		done = false;

		// Empty queue
		if(top == null) {
			assert(bottom == null);
			assert(size == 0);
			offset = 0;
			dirty = false;
			return;
		}

		currItem = top;
		offset = 0;

		currentSize = 1;
		while(done == false) {
			currItem.position = currentSize;
			currItem = currItem.below;
			if(currItem == null) {
				done = true;
				assert(size == currentSize);
			}
			currentSize++;
		}

		// Congrats! We now have a clean queue!
		dirty = false;
	}

	@Override
	void clear() {
		while(pop() != null) {
			// What? Expecting something? The clearing is done
			// in the while statement above - I don't need anything here!
		}
		clean();
		return;
	}

	// This is one of the special functions - allows item to take itself off
	// the queue no matter where in the queue it is!
	@Override
	void del(Vehicle v) {
		VQueueItem current;
		VQueueItem above;
		VQueueItem below;

		current = v.queue_item;
		if(current == null)
			return;

		if(current == top)
			top = current.below;

		if(current == bottom)
			bottom = current.above;

		above = current.above;
		below = current.below;

		if(above != null)
			above.below = below;
		
		if(below != null)
			below.above = above;

		v.queue_item = null;

		//free(current);

		size--;
		dirty = true;
	}

	@Override
	int getPos(Vehicle v) {
		if(dirty)
			clean();

		if(v.queue_item == null)
			return 0;

		return v.queue_item.position - offset;
	}

}
