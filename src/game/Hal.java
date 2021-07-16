public abstract class Hal
{
    // graphics
	abstract void start_video(String parm);
	abstract void stop_video();
	abstract void make_dirty(int left, int top, int width, int height);
	abstract void main_loop();
	abstract boolean change_resolution(int w, int h);
    
    void toggle_fullscreen(boolean fullscreen) { } // TODO

	public static DrawPixelInfo _screen = new DrawPixelInfo();
	public static DrawPixelInfo _cur_dpi = new DrawPixelInfo();

	public static Rect _invalid_rect = new Rect();
	public static CursorVars _cursor = new CursorVars();


	void SetDirtyBlocks(int left, int top, int right, int bottom)
{
	byte b[];
	int width;
	int height;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > _screen.width) right = _screen.width;
	if (bottom > _screen.height) bottom = _screen.height;

	if (left >= right || top >= bottom) return;

	if (left   < _invalid_rect.left  ) _invalid_rect.left   = left;
	if (top    < _invalid_rect.top   ) _invalid_rect.top    = top;
	if (right  > _invalid_rect.right ) _invalid_rect.right  = right;
	if (bottom > _invalid_rect.bottom) _invalid_rect.bottom = bottom;

	left >>= 6;
	top  >>= 3;

	b = _dirty_blocks + top * DIRTY_BYTES_PER_LINE + left;

	width  = ((right  - 1) >> 6) - left + 1;
	height = ((bottom - 1) >> 3) - top  + 1;

	assert(width > 0 && height > 0);

	do {
		int i = width;

		do { b[--i] = 0xFF; } while (i);

		b += DIRTY_BYTES_PER_LINE;
	} while (--height != 0);
}


void MarkWholeScreenDirty()
{
	SetDirtyBlocks(0, 0, _screen.width, _screen.height);
}

boolean FillDrawPixelInfo(DrawPixelInfo n,  DrawPixelInfo o, int left, int top, int width, int height)
{
	int t;

	if (o == null) o = _cur_dpi;

	n.zoom = 0;

	assert(width > 0);
	assert(height > 0);

	n.left = 0;
	if ((left -= o.left) < 0) {
		width += left;
		if (width < 0) return false;
		n.left = -left;
		left = 0;
	}

	if ((t=width + left - o.width) > 0) {
		width -= t;
		if (width < 0) return false;
	}
	n.width = width;

	n.top = 0;
	if ((top -= o.top) < 0) {
		height += top;
		if (height < 0) return false;
		n.top = -top;
		top = 0;
	}

	n.dst_ptr = o.dst_ptr + left + top * (n.pitch = o.pitch);

	if ((t=height + top - o.height) > 0) {
		height -= t;
		if (height < 0) return false;
	}
	n.height = height;

	return true;
}

void SetCursorSprite(CursorID cursor)
{
	CursorVars cv =_cursor;
	Sprite p;

	if (cv.sprite == cursor) return;

	p = GetSprite(cursor & SPRITE_MASK);
	cv.sprite = cursor;
	cv.size.y = p.height;
	cv.size.x = p.width;
	cv.offs.x = p.x_offs;
	cv.offs.y = p.y_offs;

	cv.dirty = true;
}

void SwitchAnimatedCursor()
{
	CursorVars cv = _cursor;
	CursorID cur = cv.animate_cur;
	CursorID sprite;

	// ANIM_CURSOR_END is 0xFFFF in table/animcursors.h
	if (cur == null || cur.id == 0xFFFF) cur = cv.animate_list;

	sprite = cur[0];
	cv.animate_timeout = cur[1];
	cv.animate_cur = new CursorId( cur.id + 2);

	SetCursorSprite(sprite);
}

void CursorTick()
{
	if (_cursor.animate_timeout != 0 && --_cursor.animate_timeout == 0)
		SwitchAnimatedCursor();
}

void SetMouseCursor(CursorID cursor)
{
	// Turn off animation
	_cursor.animate_timeout = 0;
	// Set cursor
	SetCursorSprite(cursor);
}

void SetAnimatedMouseCursor( CursorID table)
{
	_cursor.animate_list = table;
	_cursor.animate_cur = null;
	SwitchAnimatedCursor();
}

boolean ChangeResInGame(int w, int h)
{
	return
		(_screen.width == w && _screen.height == h) ||
		change_resolution(w, h);
}

void ToggleFullScreen(boolean fs) {toggle_fullscreen(fs);}
/*
static int  compare_res(const void *pa, const void *pb)
{
	int x = ((const uint16*)pa)[0] - ((const uint16*)pb)[0];
	if (x != 0) return x;
	return ((const uint16*)pa)[1] - ((const uint16*)pb)[1];
}

void SortResolutions(int count)
{
	qsort(_resolutions, count, sizeof(_resolutions[0]), compare_res);
}
*/
int GetDrawStringPlayerColor(PlayerID player)
{
	// Get the color for DrawString-subroutines which matches the color
	//  of the player
	if (player == OWNER_SPECTATOR || player == OWNER_SPECTATOR - 1) return 1;
	return (_color_list[_player_colors[player]].window_color_1b) | IS_PALETTE_COLOR;
}


}



class DrawPixelInfo {
	Pixel *dst_ptr;
	int left, top, width, height;
	int pitch;
	int zoom;
}

class CursorVars {
	Point pos, size, offs, delta;
	Point draw_pos, draw_size;
	CursorID sprite;

	int wheel; // mouse wheel movement
	CursorID animate_list, animate_cur;
	int animate_timeout;

	boolean visible;
	boolean dirty;
	boolean fix_at;
} 
