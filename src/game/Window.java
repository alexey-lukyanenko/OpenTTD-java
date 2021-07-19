package game;

import java.util.ArrayList;
import java.util.List;
import java.util.ListIterator;
import java.util.function.BiConsumer;

import game.util.BitOps;
import game.util.WindowConstants;
import game.util.wcustom.*;

public class Window extends WindowConstants
{
	int flags4;
	WindowClass window_class;
	WindowNumber window_number;

	int left, top;
	int width, height;

	Scrollbar hscroll, vscroll, vscroll2;
	ResizeInfo resize;

	byte caption_color;

	int click_state, disabled_state, hidden_state;

	ViewPort viewport;
	Widget [] original_widget;
	List<Widget> widget;
	int desc_flags;

	WindowMessage message;
	//byte custom[WINDOW_CUSTOM_SIZE];
	//byte custom[];
	AbstractWinCustom custom;

	BiConsumer<Window,WindowEvent> wndproc;

	public Window() {
		left = top = width = height = flags4 = 0;
		caption_color = 0;
		click_state = disabled_state = hidden_state = 0;
		desc_flags = 0;

		window_class = null;
		window_number = null;

		hscroll  = null;
		vscroll  = null;
		vscroll2 = null;
		resize = null;


		viewport = null;
		original_widget = null;
		//widget = null;

		message = null;
		custom = null;
		wndproc = null;

		List<Widget> widget =  new ArrayList<Widget>();

	}	

	
	// ------------------------------------
	// static state
	
	
	//static Window _windows[] = new Window[25];
	static List<Window> _windows = new ArrayList<Window>();

	
	
	 Point _cursorpos_drag_start = new Point();

	 boolean _left_button_down;
	 boolean _left_button_clicked;

	 boolean _right_button_down;
	 boolean _right_button_clicked;

	// XXX added parameter to AllocateWindowDesc
	// int _alloc_wnd_parent_num;

	 int _scrollbar_start_pos;
	 int _scrollbar_size;
	 byte _scroller_click_timeout;

		/**
		 * TODO Controlled fro Widget
		 */
		public static boolean _scrolling_scrollbar = false;

		/**
		 * TODO control it from outside
		 */
		public static boolean _scrolling_viewport = false;
	 boolean _popup_menu_active;

	 byte _special_mouse_mode;

	
	
	
	
	// -----------------------------------
	
	
	
	
	//WindowProc *wndproc;
	//abstract void WindowProc( WindowEvent e);
	//void wndproc(WindowEvent e) { WindowProc(e); }

	void CallWindowEventNP(WindowEvents event)
	{
		WindowEvent e = new WindowEvent();

		e.event = event;
		wndproc.accept(this,e);
	}


	void SetWindowDirty()
	{
		Global.hal.SetDirtyBlocks(left, top, left + width, top + height);
	}

	/** Returns the index for the widget located at the given position
	 * relative to the window. It includes all widget-corner pixels as well.
	 * @param *w Window to look inside
	 * @param  x,y Window client coordinates
	 * @return A widget index, or -1 if no widget was found.
	 */
	int GetWidgetFromPos(int x, int y)
	{
		//Widget wi;
		int found_index = -1;

		// Go through the widgets and check if we find the widget that the coordinate is
		// inside.
		//for (index = 0,wi = widget; wi.type != WWT_LAST; index++, wi++) 
		int index = -1;
		for(Widget wi : widget)
		{
			index++;
			if (wi.type == WWT_EMPTY || wi.type == WWT_FRAME) continue;

			boolean hidden = BitOps.HASBIT(hidden_state,index);

			if (x >= wi.left && x <= wi.right && y >= wi.top &&  y <= wi.bottom &&
					!hidden) {
				found_index = index;
			}
		}

		return found_index;
	}








	// delta between mouse cursor and upper left corner of dragged window
	static Point _drag_delta;

	void HandleButtonClick(int widget)
	{
		click_state |= (1 << widget);
		flags4 |= 5 << WF_TIMEOUT_SHL;
		InvalidateWidget(widget);
	}

	static void DispatchLeftClickEvent(Window  w, int x, int y)
	{
		WindowEvent e;
		final Widget wi;

		e.pt.x = x;
		e.pt.y = y;
		e.event = WindowEvents.WE_CLICK;

		if (0 != (w.desc_flags & WindowDesc.WDF_DEF_WIDGET)) {
			e.widget = w.GetWidgetFromPos(x, y);
			if (e.widget < 0) return; /* exit if clicked outside of widgets */

			wi = w.widget.get(e.widget);

			/* don't allow any interaction if the button has been disabled */
			if (BitOps.HASBIT(w.disabled_state, e.widget))
				return;

			if (0 != (wi.type & 0xE0)) {
				/* special widget handling for buttons*/
				switch((int)wi.type) {
				case WWT_IMGBTN  | WWB_PUSHBUTTON: /* WWT_PUSHIMGBTN */
				case WWT_TEXTBTN | WWB_PUSHBUTTON: /* WWT_PUSHTXTBTN */
					w.HandleButtonClick(e.widget);
					break;
				case WWT_NODISTXTBTN:
					break;
				}
			} else if (wi.type == WWT_SCROLLBAR || wi.type == WWT_SCROLL2BAR || wi.type == WWT_HSCROLLBAR) {
				w.ScrollbarClickHandler(wi, e.pt.x, e.pt.y);
			}

			if (0 != (w.desc_flags & WindowDesc.WDF_STD_BTN)) {
				if (e.widget == 0) { /* 'X' */
					w.DeleteWindow();
					return;
				}

				if (e.widget == 1) { /* 'Title bar' */
					w.StartWindowDrag(); // if not return then w = StartWindowDrag(w); to get correct pointer
					return;
				}
			}

			if (0 != (w.desc_flags & WindowDesc.WDF_RESIZABLE) && wi.type == WWT_RESIZEBOX) {
				w.StartWindowSizing(); // if not return then w = StartWindowSizing(w); to get correct pointer
				return;
			}

			if (0 != (w.desc_flags & WindowDesc.WDF_STICKY_BUTTON) && wi.type == WWT_STICKYBOX) {
				w.flags4 ^= WF_STICKY;
				w.InvalidateWidget(e.widget);
				return;
			}
		}

		w.wndproc.accept(w, e);
	}

	static void DispatchRightClickEvent(Window  w, int x, int y)
	{
		WindowEvent e;

		/* default tooltips handler? */
		if (0 != (w.desc_flags & WindowDesc.WDF_STD_TOOLTIPS)) {
			e.widget = w.GetWidgetFromPos(x, y);
			if (e.widget < 0)
				return; /* exit if clicked outside of widgets */

			if (w.widget.get(e.widget).tooltips != null) {
				GuiShowTooltips(w.widget.get(e.widget).tooltips);
				return;
			}
		}

		e.event = WindowEvents.WE_RCLICK;
		e.pt.x = x;
		e.pt.y = y;
		w.wndproc.accept(w, e);
	}

	/** Dispatch the mousewheel-action to the window which will scroll any
	 * compatible scrollbars if the mouse is pointed over the bar or its contents
	 * @param *w Window
	 * @param widget the widget where the scrollwheel was used
	 * @param wheel scroll up or down
	 */
	static void DispatchMouseWheelEvent(Window  w, int widget, int wheel)
	{
		Widget wi1, wi2;
		Scrollbar sb;

		if (widget < 0) return;

		wi1 = w.widget.get(widget);
		wi2 = w.widget.get(widget + 1);

		/* The listbox can only scroll if scrolling was done on the scrollbar itself,
		 * or on the listbox (and the next item is (must be) the scrollbar)
		 * XXX - should be rewritten as a widget-dependent scroller but that's
		 * not happening until someone rewrites the whole widget-code */
		if ((sb = w.vscroll,  wi1.type == WWT_SCROLLBAR)  || (sb = w.vscroll2, wi1.type == WWT_SCROLL2BAR)  ||
				(sb = w.vscroll2, wi2.type == WWT_SCROLL2BAR) || (sb = w.vscroll, wi2.type == WWT_SCROLLBAR) ) {

			if (sb.count > sb.cap) {
				int pos = BitOps.clamp(sb.pos + wheel, 0, sb.count - sb.cap);
				if (pos != sb.pos) {
					sb.pos = pos;
					w.SetWindowDirty();
				}
			}
		}
	}


	void DrawOverlappedWindowForAll(int left, int top, int right, int bottom)
	{
		Window w;
		DrawPixelInfo bk;
		Global._cur_dpi = bk;

		for (Window w : _windows) {
			if (right > w.left &&
					bottom > w.top &&
					left < w.left + w.width &&
					top < w.top + w.height) {
				DrawOverlappedWindow(w, left, top, right, bottom);
			}
		}
	}

	void DrawOverlappedWindow(Window w, int left, int top, int right, int bottom)
	{
		final Window  v = w;
		int x;

		while (++v != _last_window) {
			if (right > v.left &&
					bottom > v.top &&
					left < v.left + v.width &&
					top < v.top + v.height) {
				if (left < (x=v.left)) {
					DrawOverlappedWindow(w, left, top, x, bottom);
					DrawOverlappedWindow(w, x, top, right, bottom);
					return;
				}

				if (right > (x=v.left + v.width)) {
					DrawOverlappedWindow(w, left, top, x, bottom);
					DrawOverlappedWindow(w, x, top, right, bottom);
					return;
				}

				if (top < (x=v.top)) {
					DrawOverlappedWindow(w, left, top, right, x);
					DrawOverlappedWindow(w, left, x, right, bottom);
					return;
				}

				if (bottom > (x=v.top + v.height)) {
					DrawOverlappedWindow(w, left, top, right, x);
					DrawOverlappedWindow(w, left, x, right, bottom);
					return;
				}

				return;
			}
		}

		{
			DrawPixelInfo dp = _cur_dpi;
			dp.width = right - left;
			dp.height = bottom - top;
			dp.left = left - w.left;
			dp.top = top - w.top;
			dp.pitch = _screen.pitch;
			dp.dst_ptr = _screen.dst_ptr + top * _screen.pitch + left;
			dp.zoom = 0;
			CallWindowEventNP(w, WindowEvents.WE_PAINT);
		}
	}

	/*
	void CallWindowEventNP(Window w, int event)
	{
		WindowEvent e;

		e.event = event;
		w.wndproc(w, &e);
	}


	void SetWindowDirty(final Window  w)
	{
		if (w == null) return;
		Global.hal.SetDirtyBlocks(w.left, w.top, w.left + w.width, w.top + w.height);
	}
	 */
	void DeleteWindow()
	{
		WindowClass wc;
		WindowNumber wn;
		ViewPort vp;
		Window v;
		int count;

		//if (w == null) return;

		if (ViewPort._thd.place_mode != 0 && ViewPort._thd.window_class == window_class && ViewPort._thd.window_number == window_number) {
			ResetObjectToPlace();
		}

		wc = window_class;
		wn = window_number;

		CallWindowEventNP(this, WindowEvents.WE_DESTROY);

		Window w = FindWindowById(wc, wn);

		vp = w.viewport;
		w.viewport = null;
		if (vp != null) {
			//_active_viewports &= ~(1 << (vp - _viewports));
			vp.width = 0;
			vp.removeFromAll();
		}

		w.SetWindowDirty();

		//free(w.widget);

		//v = --_last_window;
		//count = (byte*)v - (byte*)w;
		//memmove(w, w + 1, count);
		_windows.remove(w);
	}

	static Window FindWindowById(WindowClass cls, WindowNumber number)
	{
		//Window w;

		for (Window w : _windows) {
			if (w.window_class == cls && w.window_number == number) return w;
		}

		return null;
	}

	static Window FindWindowById(int cls, int number)
	{
		for (Window w : _windows) {
			if (w.window_class.v == cls && w.window_number.n == number) 
				return w;
		}

		return null;
	}

	static void DeleteWindowById(WindowClass cls, WindowNumber number)
	{
		FindWindowById(cls, number).DeleteWindow();
	}

	static void DeleteWindowById(int cls, int number)
	{
		FindWindowById(cls, number).DeleteWindow();
	}

	static void DeleteWindowByClass(WindowClass cls)
	{

		for(int i = 0; i < _windows.size();) 
		{
			Window w = _windows.get(i);

			if (w.window_class == cls) {
				w.DeleteWindow();
				i = 0;
			} else {
				i++;
			}
		}
	}

	static Window BringWindowToFrontById(WindowClass cls, WindowNumber number)
	{
		Window w = FindWindowById(cls, number);

		if (w != null) {
			w.flags4 |= WF_WHITE_BORDER_MASK;
			w.SetWindowDirty();
			w.BringWindowToFront();
		}

		return w;
	}

	static Window BringWindowToFrontById(int cls, int number)
	{
		Window w = FindWindowById(cls, number);

		if (w != null) {
			w.flags4 |= WF_WHITE_BORDER_MASK;
			w.SetWindowDirty();
			w.BringWindowToFront();
		}

		return w;
	}

	public boolean IsVitalWindow()
	{
		WindowClass wc = window_class;
		return (wc.v == WC_MAIN_TOOLBAR || wc.v == WC_STATUS_BAR || wc == WC_NEWS_WINDOW || wc == WC_SEND_NETWORK_MSG);
	}

	/** On clicking on a window, make it the frontmost window of all. However
	 * there are certain windows that always need to be on-top; these include
	 * - Toolbar, Statusbar (always on)
	 * - New window, Chatbar (only if open)
	 * @param w window that is put into the foreground
	 */
	Window BringWindowToFront(Window w)
	{
		Window v;
		Window temp;

		v = _last_window;
		do {
			if (--v < _windows) return w;
		} while (IsVitalWindow(v));

		if (w == v) return w;

		assert(w < v);

		temp = *w;
		memmove(w, w + 1, (v - w) * sizeof(Window));
		*v = temp;

		SetWindowDirty(v);

		return v;
	}

	/** We have run out of windows, so find a suitable candidate for replacement.
	 * Keep all important windows intact. These are
	 * - Main window (gamefield), Toolbar, Statusbar (always on)
	 * - News window, Chatbar (when on)
	 * - Any sticked windows since we wanted to keep these
	 * @return w pointer to the window that is going to be deleted
	 */
	static Window FindDeletableWindow()
	{
		//Window w;

		/*for (w = _windows; w < endof(_windows); w++) {
			if (w.window_class.v != WC_MAIN_WINDOW && !IsVitalWindow(w) && !(w.flags4 & WF_STICKY)) {
				return w;
			}
		}*/
		return null;  // in java we can't
	}

	/** A window must be freed, and all are marked as important windows. Ease the
	 * restriction a bit by allowing to delete sticky windows. Keep important/vital
	 * windows intact (Main window, Toolbar, Statusbar, News Window, Chatbar)
	 * @see FindDeletableWindow()
	 * @return w Pointer to the window that is being deleted
	 */
	static private Window ForceFindDeletableWindow()
	{
		//Window w;

		for (Window w : _windows) {
			//assert(w < _last_window);
			if (w.window_class.v != WC_MAIN_WINDOW && !w.IsVitalWindow()) 
				return w;
		}
		assert false;
		return null;
	}

	static boolean IsWindowOfPrototype(final Window  w, final Widget[] widget)
	{
		return (w.original_widget == widget);
	}

	/* Copies 'widget' to 'w.widget' to allow for resizable windows */
	void AssignWidgetToWindow(final Widget[] nwidget)
	{
		/*
		w.original_widget = widget;

		if (widget != null) {
			int index = 1;
			final Widget wi;

			for (wi = widget; wi.type != WWT_LAST; wi++) index++;

			w.widget = realloc(w.widget, sizeof(*w.widget) * index);
			memcpy(w.widget, widget, sizeof(*w.widget) * index);
		} else {
			w.widget = null;
		}
		 */
		original_widget = nwidget;
		widget.clear(); // XXX really?
		if(nwidget != null)
		{
			for( Widget ww : nwidget)
			{
				if(ww.type != WWT_LAST)
					widget.add(ww);
			}
		}
		//else			widget.clear(); // XXX really?
	}

	/** Open a new window. If there is no space for a new window, close an open
	 * window. Try to avoid stickied windows, but if there is no else, close one of
	 * those as well. Then make sure all created windows are below some always-on-top
	 * ones. Finally set all variables and call the WE_CREATE event
	 * @param x offset in pixels from the left of the screen
	 * @param y offset in pixels from the top of the screen
	 * @param width width in pixels of the window
	 * @param height height in pixels of the window
	 * @param *proc @see WindowProc function to call when any messages/updates happen to the window
	 * @param cls @see WindowClass class of the window, used for identification and grouping
	 * @param *widget @see Widget pointer to the window layout and various elements
	 * @return @see Window pointer of the newly created window
	 */
	Window AllocateWindow(
			int x, int y, int width, int height,
			BiConsumer<Window,WindowEvent> proc, WindowClass cls, final Widget[] widget)
	{
		Window w = new Window();

		/* TODO limit windows count?
		// We have run out of windows, close one and use that as the place for our new one
		if (w >= endof(_windows)) {
			w = FindDeletableWindow();

			if (w == null) w = ForceFindDeletableWindow();

			DeleteWindow(w);
			w = _last_window;
		} */


		/* XXX - This very strange construction makes sure that the chatbar is always
		 * on top of other windows. Why? It is created as last_window (so, on top).
		 * Any other window will go below toolbar/statusbar/news window, which implicitely
		 * also means it is below the chatbar. Very likely needs heavy improvement
		 * to de-braindeadize * /
		if (w != _windows && cls != WC_SEND_NETWORK_MSG) {
			Window v;

			// * XXX - if not this order (toolbar/statusbar and then news), game would
			// * crash because it will try to copy a negative size for the news-window.
			// * Eg. window was already moved BELOW news (which is below toolbar/statusbar)
			// * and now needs to move below those too. That is a negative move. 
			v = FindWindowById(WC_MAIN_TOOLBAR, 0);
			if (v != null) {
				memmove(v+1, v, (byte*)w - (byte*)v);
				w = v;
			}

			v = FindWindowById(WC_STATUS_BAR, 0);
			if (v != null) {
				memmove(v+1, v, (byte*)w - (byte*)v);
				w = v;
			}

			v = FindWindowById(WC_NEWS_WINDOW, 0);
			if (v != null) {
				memmove(v+1, v, (byte*)w - (byte*)v);
				w = v;
			}
		} */

		// Set up window properties
		//memset(w, 0, sizeof(Window));
		w.window_class = cls;
		w.flags4 = WF_WHITE_BORDER_MASK; // just opened windows have a white border
		w.caption_color = (byte) 0xFF;
		w.left = x;
		w.top = y;
		w.width = width;
		w.height = height;
		w.wndproc = proc;
		w.AssignWidgetToWindow(widget);
		w.resize.width = width;
		w.resize.height = height;
		w.resize.step_width = 1;
		w.resize.step_height = 1;

		_windows.add(w);
		//_last_window++;

		w.SetWindowDirty();

		w.CallWindowEventNP(WindowEvents.WE_CREATE);

		return w;
	}

	Window AllocateWindowAutoPlace2(
			WindowClass exist_class,
			WindowNumber exist_num,
			int width,
			int height,
			BiConsumer<Window,WindowEvent> proc,
			WindowClass cls,
			final Widget[] widget)
	{
		Window w;
		int x;

		w = FindWindowById(exist_class, exist_num);
		if (w == null || w.left >= (Global.hal._screen.width-20) || w.left <= -60 || w.top >= (Global.hal._screen.height-20)) {
			return AllocateWindowAutoPlace(width,height,proc,cls,widget);
		}

		x = w.left;
		if (x > Global.hal._screen.width - width) x = Global.hal._screen.width - width - 20;

		return AllocateWindow(x + 10, w.top + 10, width, height, proc, cls, widget);
	}



	static SizeRect _awap_r;

	static boolean IsGoodAutoPlace1(int left, int top)
	{
		int right,bottom;
		//Window w;

		_awap_r.left= left;
		_awap_r.top = top;
		right = _awap_r.width + left;
		bottom = _awap_r.height + top;

		if (left < 0 || top < 22 || right > Global.hal._screen.width || bottom > Global.hal._screen.height)
			return false;

		// Make sure it is not obscured by any window.
		for (Window w : _windows) {
			if (w.window_class.v == WC_MAIN_WINDOW) continue;

			if (right > w.left &&
					w.left + w.width > left &&
					bottom > w.top &&
					w.top + w.height > top) {
				return false;
			}
		}

		return true;
	}

	static boolean IsGoodAutoPlace2(int left, int top)
	{
		int width,height;
		//Window w;

		_awap_r.left= left;
		_awap_r.top = top;
		width = _awap_r.width;
		height = _awap_r.height;

		if (left < -(width>>2) || left > Global.hal._screen.width - (width>>1))
			return false;
		if (top < 22 || top > Global.hal._screen.height - (height>>2))
			return false;

		// Make sure it is not obscured by any window.
		for (Window w : _windows) {
			if (w.window_class.v == WC_MAIN_WINDOW) continue;

			if (left + width > w.left &&
					w.left + w.width > left &&
					top + height > w.top &&
					w.top + w.height > top) {
				return false;
			}
		}

		return true;
	}

	static Point GetAutoPlacePosition(int width, int height)
	{
		//Window w;
		Point pt;

		_awap_r.width = width;
		_awap_r.height = height;

		if (IsGoodAutoPlace1(0, 24)) 
		{
			//goto ok_pos;
			pt.x = _awap_r.left;
			pt.y = _awap_r.top;
			return pt;
		}

		for (Window w : _windows) {
			if (w.window_class.v == WC_MAIN_WINDOW) continue;

			if (IsGoodAutoPlace1(w.left+w.width+2,w.top)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left-   width-2,w.top)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left,w.top+w.height+2)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left,w.top-   height-2)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left+w.width+2,w.top+w.height-height)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left-   width-2,w.top+w.height-height)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left+w.width-width,w.top+w.height+2)) goto ok_pos;
			if (IsGoodAutoPlace1(w.left+w.width-width,w.top-   height-2)) goto ok_pos;
		}

		for (Window w : _windows) {
			if (w.window_class.v == WC_MAIN_WINDOW) continue;

			if (IsGoodAutoPlace2(w.left+w.width+2,w.top)) goto ok_pos;
			if (IsGoodAutoPlace2(w.left-   width-2,w.top)) goto ok_pos;
			if (IsGoodAutoPlace2(w.left,w.top+w.height+2)) goto ok_pos;
			if (IsGoodAutoPlace2(w.left,w.top-   height-2)) goto ok_pos;
		}

		{
			int left=0,top=24;

			//restart:;
			while(true)
			{
				boolean again = false;
				for (Window w : _windows) {
					if (w.left == left && w.top == top) {
						left += 5;
						top += 5;
						//goto restart;
						again = true;
						break;
					}
					if(!again) break;
				}
			}
			pt.x = left;
			pt.y = top;
			return pt;
		}

		//ok_pos:;
		pt.x = _awap_r.left;
		pt.y = _awap_r.top;
		return pt;
	}

	Window AllocateWindowAutoPlace(
			int width,
			int height,
			BiConsumer<Window,WindowEvent> proc,
			WindowClass cls,
			final Widget[] widget) {

		Point pt = GetAutoPlacePosition(width, height);
		return AllocateWindow(pt.x, pt.y, width, height, proc, cls, widget);
	}
	/**
	 * 
	 * @param desc
	 * @param value win number?
	 * @return
	 */
	Window AllocateWindowDescFront(final WindowDesc desc, int value)
	{
		Window w;

		if (BringWindowToFrontById(desc.cls.v, value) != null) return null;
		w = AllocateWindowDesc(desc,0);
		w.window_number = new WindowNumber(value);
		return w;
	}

	/**
	 * 
	 * @param desc
	 * @param parentWindowNum parent window number or 0 if not needed
	 * @return
	 * 
	 * @apiNote NB! _alloc_wnd_parent_num is not used anymore,
	 */

	Window AllocateWindowDesc(WindowDesc desc, int parentWindowNum)
	{
		Point pt;
		Window w = null;
		final DrawPixelInfo _screen = Global.hal._screen;

		//if (desc.parent_cls != WC_MAIN_WINDOW &&
		//		(w = FindWindowById(desc.parent_cls, _alloc_wnd_parent_num), _alloc_wnd_parent_num=0, w) != null &&
		//		w.left < _screen.width-20 && w.left > -60 && w.top < _screen.height-20) {
		//	pt.x = w.left + 10;
		//	if (pt.x > _screen.width + 10 - desc.width)
		//		pt.x = (_screen.width + 10 - desc.width) - 20;
		//	pt.y = w.top + 10;
		if (desc.parent_cls.v != WC_MAIN_WINDOW && parentWindowNum != 0)
			w = FindWindowById(desc.parent_cls.v, parentWindowNum);

		if (desc.parent_cls.v != WC_MAIN_WINDOW && 
				parentWindowNum != 0 && w != null &&
				w.left < _screen.width-20 && 
				w.left > -60 && w.top < _screen.height-20) 
		{
			pt.x = w.left + 10;
			if (pt.x > _screen.width + 10 - desc.width)
				pt.x = (_screen.width + 10 - desc.width) - 20;
			pt.y = w.top + 10;
		} else if (desc.cls.v == WC_BUILD_TOOLBAR) { // open Build Toolbars aligned
			/* Override the position if a toolbar is opened according to the place of the maintoolbar
			 * The main toolbar (WC_MAIN_TOOLBAR) is 640px in width */
			switch (Global._patches.toolbar_pos) {
			case 1:  pt.x = ((_screen.width + 640) >> 1) - desc.width; break;
			case 2:  pt.x = _screen.width - desc.width; break;
			default: pt.x = 640 - desc.width;
			}
			pt.y = desc.top;
		} else {
			pt.x = desc.left;
			pt.y = desc.top;
			if (pt.x == WDP_AUTO) {
				pt = GetAutoPlacePosition(desc.width, desc.height);
			} else {
				if (pt.x == WDP_CENTER) pt.x = (_screen.width - desc.width) >> 1;
				if (pt.y == WDP_CENTER) pt.y = (_screen.height - desc.height) >> 1;
				else if(pt.y < 0) pt.y = _screen.height + pt.y; // if y is negative, it's from the bottom of the screen
			}
		}

		w = AllocateWindow(pt.x, pt.y, desc.width, desc.height, desc.proc, desc.cls, desc.widgets);
		w.desc_flags = desc.flags;
		return w;
	}

	static public Window FindWindowFromPt(int x, int y)
	{		
		ListIterator<Window> i = _windows.listIterator(_windows.size());

		while (i.hasPrevious()) {
			Window w = i.previous();
			if (BitOps.IS_INSIDE_1D(x, w.left, w.width) &&
					BitOps.IS_INSIDE_1D(y, w.top, w.height)) {
				return w;
			}
		}

		return null;
	}

	public static void InitWindowSystem()
	{
		IConsoleClose();

		//memset(&_windows, 0, sizeof(_windows));
		//_last_window = _windows;
		//memset(_viewports, 0, sizeof(_viewports));
		//_active_viewports = 0;
		ViewPort._viewports.clear();
		_no_scroll = 0;
	}

	static void UnInitWindowSystem()
	{

		/*
		//Window w;
		// delete all malloced widgets
		for (Window w : _windows) {
			//free(w.widget);
			w.widget = null;
		}*/
	}

	static void ResetWindowSystem()
	{
		UnInitWindowSystem();
		InitWindowSystem();
		ViewPort._thd.pos.x = 0;
		ViewPort._thd.pos.y = 0;
	}

	static void DecreaseWindowCounters()
	{
		//Window w;
		ListIterator<Window> i = _windows.listIterator(_windows.size());

		//for (w = _last_window; w != _windows;) {
		//	--w;
		while (i.hasPrevious()) {
			Window w = i.previous();
			// Unclick scrollbar buttons if they are pressed.
			if (0 != (w.flags4 & (WF_SCROLL_DOWN | WF_SCROLL_UP))) {
				w.flags4 &= ~(WF_SCROLL_DOWN | WF_SCROLL_UP);
				w.SetWindowDirty();
			}
			w.CallWindowEventNP(WindowEvents.WE_MOUSELOOP);
		}

		i = _windows.listIterator(_windows.size());
		//for (w = _last_window; w != _windows;) {
		//	--w;
		while (i.hasPrevious()) {
			Window w = i.previous();

			if ( (0 != (w.flags4&WF_TIMEOUT_MASK)) && ( 0 == (--w.flags4&WF_TIMEOUT_MASK))) {
				w.CallWindowEventNP(WindowEvents.WE_TIMEOUT);
				if (0 != (w.desc_flags & WindowDesc.WDF_UNCLICK_BUTTONS)) 
					w.UnclickWindowButtons();
			}
		}
	}

	static Window GetCallbackWnd()
	{
		return FindWindowById(ViewPort._thd.window_class, ViewPort._thd.window_number);
	}

	static void HandlePlacePresize()
	{
		Window w;
		WindowEvent e;

		if (_special_mouse_mode != WSM_PRESIZE) return;

		w = GetCallbackWnd();
		if (w == null) return;

		e.pt = GetTileBelowCursor();
		if (e.pt.x == -1) {
			ViewPort._thd.selend.x = -1;
			return;
		}
		e.tile = TileVirtXY(e.pt.x, e.pt.y);
		e.event = WindowEvents.WE_PLACE_PRESIZE;
		w.wndproc.accept(w, e);
	}

	static boolean HandleDragDrop()
	{
		Window w;
		WindowEvent e;

		if (_special_mouse_mode != WSM_DRAGDROP) return true;

		if (_left_button_down) return false;

		w = GetCallbackWnd();

		ResetObjectToPlace();

		if (w != null) {
			// send an event in client coordinates.
			e.event = WindowEvents.WE_DRAGDROP;
			e.pt.x = _cursor.pos.x - w.left;
			e.pt.y = _cursor.pos.y - w.top;
			e.widget = w.GetWidgetFromPos(e.pt.x, e.pt.y);
			w.wndproc.accept(w, e);
		}
		return false;
	}

	static boolean HandlePopupMenu()
	{
		Window w;
		WindowEvent e;

		if (!_popup_menu_active) return true;

		w = FindWindowById(WC_TOOLBAR_MENU, 0);
		if (w == null) {
			_popup_menu_active = false;
			return false;
		}

		if (_left_button_down) {
			e.event = WindowEvents.WE_POPUPMENU_OVER;
			e.pt = _cursor.pos;
		} else {
			_popup_menu_active = false;
			e.event = WindowEvents.WE_POPUPMENU_SELECT;
			e.pt = _cursor.pos;
		}

		w.wndproc.accept(w, e);

		return false;
	}

	static Window last_w = null;
	static boolean HandleMouseOver()
	{
		Window w;
		WindowEvent e;

		w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);

		// We changed window, put a MOUSEOVER event to the last window
		if (last_w != null && last_w != w) {
			e.event = WindowEvents.WE_MOUSEOVER;
			e.pt.x = -1;
			e.pt.y = -1;
			if (last_w.wndproc != null) last_w.wndproc.accept(last_w, e);
		}
		last_w = w;

		if (w != null) {
			// send an event in client coordinates.
			e.event = WindowEvents.WE_MOUSEOVER;
			e.pt.x = _cursor.pos.x - w.left;
			e.pt.y = _cursor.pos.y - w.top;
			if (w.widget != null) {
				e.widget = w.GetWidgetFromPos(e.pt.x, e.pt.y);
			}
			w.wndproc.accept(w, e);
		}

		// Mouseover never stops execution
		return true;
	}


	static boolean _dragging_window = false;

	static boolean HandleWindowDragging()
	{
		//Window w;
		// Get out immediately if no window is being dragged at all.
		if (!_dragging_window) return true;

		// Otherwise find the window...
		for (Window w : _windows) {
			if (0 != (w.flags4 & WF_DRAGGING)) {
				final Widget t = w.widget.get(1); // the title bar ... ugh
				//final Window v;
				int x;
				int y;
				int nx;
				int ny;

				// Stop the dragging if the left mouse button was released
				if (!_left_button_down) {
					w.flags4 &= ~WF_DRAGGING;
					break;
				}

				w.SetWindowDirty();

				x = _cursor.pos.x + _drag_delta.x;
				y = _cursor.pos.y + _drag_delta.y;
				nx = x;
				ny = y;

				if (Global._patches.window_snap_radius != 0) {
					int hsnap = Global._patches.window_snap_radius;
					int vsnap = Global._patches.window_snap_radius;
					int delta;

					//for (v = _windows; v != _last_window; ++v) 
					for (Window v : _windows ) 
					{
						if (v == w) continue; // Don't snap at yourself

						if (y + w.height > v.top && y < v.top + v.height) {
							// Your left border <. other right border
							delta = Math.abs(v.left + v.width - x);
							if (delta <= hsnap) {
								nx = v.left + v.width;
								hsnap = delta;
							}

							// Your right border <. other left border
							delta = Math.abs(v.left - x - w.width);
							if (delta <= hsnap) {
								nx = v.left - w.width;
								hsnap = delta;
							}
						}

						if (w.top + w.height >= v.top && w.top <= v.top + v.height) {
							// Your left border <. other left border
							delta = Math.abs(v.left - x);
							if (delta <= hsnap) {
								nx = v.left;
								hsnap = delta;
							}

							// Your right border <. other right border
							delta = Math.abs(v.left + v.width - x - w.width);
							if (delta <= hsnap) {
								nx = v.left + v.width - w.width;
								hsnap = delta;
							}
						}

						if (x + w.width > v.left && x < v.left + v.width) {
							// Your top border <. other bottom border
							delta = Math.abs(v.top + v.height - y);
							if (delta <= vsnap) {
								ny = v.top + v.height;
								vsnap = delta;
							}

							// Your bottom border <. other top border
							delta = Math.abs(v.top - y - w.height);
							if (delta <= vsnap) {
								ny = v.top - w.height;
								vsnap = delta;
							}
						}

						if (w.left + w.width >= v.left && w.left <= v.left + v.width) {
							// Your top border <. other top border
							delta = Math.abs(v.top - y);
							if (delta <= vsnap) {
								ny = v.top;
								vsnap = delta;
							}

							// Your bottom border <. other bottom border
							delta = Math.abs(v.top + v.height - y - w.height);
							if (delta <= vsnap) {
								ny = v.top + v.height - w.height;
								vsnap = delta;
							}
						}
					}
				}

				DrawPixelInfo _screen = Global.hal._screen;

				// Make sure the window doesn't leave the screen
				// 13 is the height of the title bar
				nx = BitOps.clamp(nx, 13 - t.right, _screen.width - 13 - t.left);
				ny = BitOps.clamp(ny, 0, _screen.height - 13);

				// Make sure the title bar isn't hidden by behind the main tool bar
				Window v = FindWindowById(WC_MAIN_TOOLBAR, 0);
				if (v != null) {
					int v_bottom = v.top + v.height;
					int v_right = v.left + v.width;
					if (ny + t.top >= v.top && ny + t.top < v_bottom) {
						if ((v.left < 13 && nx + t.left < v.left) ||
								(v_right > _screen.width - 13 && nx + t.right > v_right)) {
							ny = v_bottom;
						} else {
							if (nx + t.left > v.left - 13 &&
									nx + t.right < v_right + 13) {
								if (w.top >= v_bottom) {
									ny = v_bottom;
								} else if (w.left < nx) {
									nx = v.left - 13 - t.left;
								} else {
									nx = v_right + 13 - t.right;
								}
							}
						}
					}
				}

				if (w.viewport != null) {
					w.viewport.left += nx - w.left;
					w.viewport.top  += ny - w.top;
				}
				w.left = nx;
				w.top  = ny;

				w.SetWindowDirty();
				return false;
			} else if (0 != (w.flags4 & WF_SIZING)) {
				WindowEvent e;
				int x, y;

				/* Stop the sizing if the left mouse button was released */
				if (!_left_button_down) {
					w.flags4 &= ~WF_SIZING;
					SetWindowDirty(w);
					break;
				}

				x = _cursor.pos.x - _drag_delta.x;
				y = _cursor.pos.y - _drag_delta.y;

				/* X and Y has to go by step.. calculate it.
				 * The cast to int is necessary else x/y are implicitly casted to
				 * unsigned int, which won't work. */
				if (w.resize.step_width > 1) x -= x % (int)w.resize.step_width;

				if (w.resize.step_height > 1) y -= y % (int)w.resize.step_height;

				/* Check if we don't go below the minimum set size */
				if ((int)w.width + x < (int)w.resize.width)
					x = w.resize.width - w.width;
				if ((int)w.height + y < (int)w.resize.height)
					y = w.resize.height - w.height;

				/* Window already on size */
				if (x == 0 && y == 0) return false;

				/* Now find the new cursor pos.. this is NOT _cursor, because
				    we move in steps. */
				_drag_delta.x += x;
				_drag_delta.y += y;

				w.SetWindowDirty();

				/* Scroll through all the windows and update the widgets if needed */
				{
					//Widget wi = w.widget;
					boolean resize_height = false;
					boolean resize_width = false;

					//while (wi.type != WWT_LAST) 
					for(Widget wi : w.widget)
					{
						if (wi.resize_flag != RESIZE_NONE) {
							/* Resize this Widget */
							if (0 != (wi.resize_flag & RESIZE_LEFT)) {
								wi.left += x;
								resize_width = true;
							}
							if (0 != (wi.resize_flag & RESIZE_RIGHT)) {
								wi.right += x;
								resize_width = true;
							}

							if (0 != (wi.resize_flag & RESIZE_TOP)) {
								wi.top += y;
								resize_height = true;
							}
							if (0 != (wi.resize_flag & RESIZE_BOTTOM)) {
								wi.bottom += y;
								resize_height = true;
							}
						}
						//wi++;
					}

					/* We resized at least 1 widget, so let's rezise the window totally */
					if (resize_width)  w.width  = x + w.width;
					if (resize_height) w.height = y + w.height;
				}

				e.event = WindowEvents.WE_RESIZE;
				e.size.x = x + w.width;
				e.size.y = y + w.height;
				e.diff.x = x;
				e.diff.y = y;
				w.wndproc.accept(w, e);

				w.SetWindowDirty();
				return false;
			}
		}

		_dragging_window = false;
		return false;
	}

	void StartWindowDrag()
	{
		flags4 |= WF_DRAGGING;
		_dragging_window = true;

		_drag_delta.x = left - _cursor.pos.x;
		_drag_delta.y = top  - _cursor.pos.y;

		BringWindowToFront();
		DeleteWindowById(WC_DROPDOWN_MENU, 0);
	}

	void StartWindowSizing()
	{
		flags4 |= WF_SIZING;
		_dragging_window = true;

		_drag_delta.x = _cursor.pos.x;
		_drag_delta.y = _cursor.pos.y;

		BringWindowToFront();
		DeleteWindowById(WC_DROPDOWN_MENU, 0);
		SetWindowDirty();
		//eturn w;
	}


	static boolean HandleScrollbarScrolling()
	{
		Window w;
		int i;
		int pos;
		Scrollbar sb;

		// Get out quickly if no item is being scrolled
		if (!_scrolling_scrollbar) return true;

		// Find the scrolling window
		for (Window w : _windows) {
			if (0 != (w.flags4 & WF_SCROLL_MIDDLE)) {
				// Abort if no button is clicked any more.
				if (!_left_button_down) {
					w.flags4 &= ~WF_SCROLL_MIDDLE;
					SetWindowDirty(w);
					break;
				}

				if (0 != (w.flags4 & WF_HSCROLL)) {
					sb = w.hscroll;
					i = _cursor.pos.x - _cursorpos_drag_start.x;
				} else if (w.flags4 & WF_SCROLL2){
					sb = w.vscroll2;
					i = _cursor.pos.y - _cursorpos_drag_start.y;
				} else {
					sb = w.vscroll;
					i = _cursor.pos.y - _cursorpos_drag_start.y;
				}

				// Find the item we want to move to and make sure it's inside bounds.
				pos = Math.min(Math.max(0, i + _scrollbar_start_pos) * sb.count / _scrollbar_size, Math.max(0, sb.count - sb.cap));
				if (pos != sb.pos) {
					sb.pos = pos;
					w.SetWindowDirty();
				}
				return false;
			}
		}

		_scrolling_scrollbar = false;
		return false;
	}


	static boolean HandleViewportScroll()
	{
		Window w;
		ViewPort vp;
		int dx,dy, x, y, sub;

		if (!_scrolling_viewport) return true;

		if (!_right_button_down) {
			//stop_capt:;
			_cursor.fix_at = false;
			_scrolling_viewport = false;
			return true;
		}

		w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);
		if (w == null) //goto stop_capt;
		{
			//stop_capt:;
			_cursor.fix_at = false;
			_scrolling_viewport = false;
			return true;
		}

		if (_patches.reverse_scroll) {
			dx = -_cursor.delta.x;
			dy = -_cursor.delta.y;
		} else {
			dx = _cursor.delta.x;
			dy = _cursor.delta.y;
		}

		if (w.window_class.v != WC_SMALLMAP) {
			vp = IsPtInWindowViewport(w, _cursor.pos.x, _cursor.pos.y);
			if (vp == null)
				//goto stop_capt;
			{
				//stop_capt:;
				_cursor.fix_at = false;
				_scrolling_viewport = false;
				return true;
			}

			WP(w,vp_d).scrollpos_x += dx << vp.zoom;
			WP(w,vp_d).scrollpos_y += dy << vp.zoom;

			_cursor.delta.x = _cursor.delta.y = 0;
			return false;
		} else {
			// scroll the smallmap ?
			int hx;
			int hy;
			int hvx;
			int hvy;

			_cursor.fix_at = true;

			x = WP(w,smallmap_d).scroll_x;
			y = WP(w,smallmap_d).scroll_y;

			sub = WP(w,smallmap_d).subscroll + dx;

			x -= (sub >> 2) << 4;
			y += (sub >> 2) << 4;
			sub &= 3;

			x += (dy >> 1) << 4;
			y += (dy >> 1) << 4;

			if (dy & 1) {
				x += 16;
				sub += 2;
				if (sub > 3) {
					sub -= 4;
					x -= 16;
					y += 16;
				}
			}

			hx = (w.widget[4].right  - w.widget[4].left) / 2;
			hy = (w.widget[4].bottom - w.widget[4].top ) / 2;
			hvx = hx * -4 + hy * 8;
			hvy = hx *  4 + hy * 8;
			if (x < -hvx) {
				x = -hvx;
				sub = 0;
			}
			if (x > (int)MapMaxX() * 16 - hvx) {
				x = MapMaxX() * 16 - hvx;
				sub = 0;
			}
			if (y < -hvy) {
				y = -hvy;
				sub = 0;
			}
			if (y > (int)MapMaxY() * 16 - hvy) {
				y = MapMaxY() * 16 - hvy;
				sub = 0;
			}

			WP(w,smallmap_d).scroll_x = x;
			WP(w,smallmap_d).scroll_y = y;
			WP(w,smallmap_d).subscroll = sub;

			_cursor.delta.x = _cursor.delta.y = 0;

			w.SetWindowDirty();
			return false;
		}
	}

	static Window MaybeBringWindowToFront(Window w)
	{
		Window u;

		if (w.window_class.v == WC_MAIN_WINDOW ||
				w.IsVitalWindow() ||
				w.window_class.v == WC_TOOLTIPS ||
				w.window_class.v == WC_DROPDOWN_MENU) {
			return w;
		}

		for (u = w; ++u != _last_window;) {
			if (u.window_class == WC_MAIN_WINDOW ||
					IsVitalWindow(u) ||
					u.window_class == WC_TOOLTIPS ||
					u.window_class == WC_DROPDOWN_MENU) {
				continue;
			}

			if (w.left + w.width <= u.left ||
					u.left + u.width <= w.left ||
					w.top  + w.height <= u.top ||
					u.top + u.height <= w.top) {
				continue;
			}

			return w.BringWindowToFront();
		}

		return w;
	}

	/** Send a message from one window to another. The receiving window is found by
	 * @param w @see Window pointer pointing to the other window
	 * @param msg Specifies the message to be sent
	 * @param wparam Specifies additional message-specific information
	 * @param lparam Specifies additional message-specific information
	 */
	static void SendWindowMessageW(Window  w, int msg, int wparam, int lparam)
	{
		WindowEvent e;

		e.event  = WindowEvents.WE_MESSAGE;
		e.msg    = msg;
		e.wparam = wparam;
		e.lparam = lparam;

		w.wndproc.accept(w, e);
	}

	/** Send a message from one window to another. The receiving window is found by
	 * @param wnd_class @see WindowClass class AND
	 * @param wnd_num @see WindowNumber number, mostly 0
	 * @param msg Specifies the message to be sent
	 * @param wparam Specifies additional message-specific information
	 * @param lparam Specifies additional message-specific information
	 */
	static void SendWindowMessage(WindowClass wnd_class, WindowNumber wnd_num, int msg, int wparam, int lparam)
	{
		Window w = FindWindowById(wnd_class, wnd_num);
		if (w != null) SendWindowMessageW(w, msg, wparam, lparam);
	}

	static void HandleKeypress(int key)
	{
		//Window w;
		WindowEvent we = new WindowEvent();
		/* Stores if a window with a textfield for typing is open
		 * If this is the case, keypress events are only passed to windows with text fields and
		 * to thein this main toolbar. */
		boolean query_open = false;

		// Setup event
		we.event = WindowEvents.WE_KEYPRESS;
		we.ascii = (byte) (key & 0xFF);
		we.keycode = key >> 16;
			we.cont = true;

			// check if we have a query string window open before allowing hotkeys
			if (FindWindowById(WC_QUERY_STRING,     0) != null ||
					FindWindowById(WC_SEND_NETWORK_MSG, 0) != null ||
					FindWindowById(WC_CONSOLE,          0) != null ||
					FindWindowById(WC_SAVELOAD,         0) != null) {
				query_open = true;
			}

			// Call the event, start with the uppermost window.
			for (int i = _windows.size()-1; i >= 0 ; i-- ) 
			{
				Window w = _windows.get(i);
				// if a query window is open, only call the event for certain window types
				if (query_open &&
						w.window_class.v != WC_QUERY_STRING &&
						w.window_class.v != WC_SEND_NETWORK_MSG &&
						w.window_class.v != WC_CONSOLE &&
						w.window_class.v != WC_SAVELOAD) {
					continue;
				}
				w.wndproc.accept(w, we);
				if (!we.cont) break;
			}

			if (we.cont) {
				Window w = FindWindowById(WC_MAIN_TOOLBAR, 0);
				// When there is no toolbar w is null, check for that
				if (w != null) w.wndproc.accept(w, we);
			}
	}

	//extern void UpdateTileSelection();
	//extern boolean VpHandlePlaceSizingDrag();

	private static final int  scrollspeed = 3;

	static void MouseLoop(int click, int mousewheel)
	{
		int x,y;
		Window w;
		ViewPort vp;

		DecreaseWindowCounters();
		HandlePlacePresize();
		UpdateTileSelection();
		if (!VpHandlePlaceSizingDrag())  return;
		if (!HandleDragDrop())           return;
		if (!HandlePopupMenu())          return;
		if (!HandleWindowDragging())     return;
		if (!HandleScrollbarScrolling()) return;
		if (!HandleViewportScroll())     return;
		if (!HandleMouseOver())          return;

		x = _cursor.pos.x;
		y = _cursor.pos.y;


		if (click == 0 && mousewheel == 0) {
			if (_patches.autoscroll && Global._game_mode != GM_MENU) {
				w = FindWindowFromPt(x, y);
				if (w == null || w.flags4 & WF_DISABLE_VP_SCROLL ) return;
				vp = IsPtInWindowViewport(w, x, y);
				if (vp) {
					vp_d vpd = (vp_d)w.custom;
					x -= vp.left;
					y -= vp.top;
					//here allows scrolling in both x and y axis
					//#define scrollspeed 3
					if (x - 15 < 0) {
						vpd.scrollpos_x += (x - 15) * scrollspeed << vp.zoom;
					} else if (15 - (vp.width - x) > 0) {
						vpd.scrollpos_x += (15 - (vp.width - x)) * scrollspeed << vp.zoom;
					}
					if (y - 15 < 0) {
						vpd.scrollpos_y += (y - 15) * scrollspeed << vp.zoom;
					} else if (15 - (vp.height - y) > 0) {
						vpd.scrollpos_y += (15 - (vp.height - y)) * scrollspeed << vp.zoom;
					}
					//#undef scrollspeed
				}
			}
			return;
		}

		w = FindWindowFromPt(x, y);
		if (w == null) return;
		w = MaybeBringWindowToFront(w);
		vp = IsPtInWindowViewport(w, x, y);
		if (vp != null) {
			if (Global._game_mode == GM_MENU) return;

			// only allow zooming in-out in main window, or in viewports
			if (mousewheel &&
					!(w.flags4 & WF_DISABLE_VP_SCROLL) && (
							w.window_class.v == WC_MAIN_WINDOW ||
							w.window_class.v == WC_EXTRA_VIEW_PORT
							)) {
				ZoomInOrOutToCursorWindow(mousewheel < 0,w);
			}

			if (click == 1) {
				Global.DEBUG_misc( 2, "cursor: 0x%X (%d)", _cursor.sprite, _cursor.sprite);
				if (ViewPort._thd.place_mode != 0 &&
						// query button and place sign button work in pause mode
						_cursor.sprite != SPR_CURSOR_QUERY &&
						_cursor.sprite != SPR_CURSOR_SIGN &&
						_pause != 0 &&
						!_cheats.build_in_pause.value) {
					return;
				}

				if (ViewPort._thd.place_mode == 0) {
					HandleViewportClicked(vp, x, y);
				} else {
					PlaceObject();
				}
			} else if (click == 2) {
				if (!(w.flags4 & WF_DISABLE_VP_SCROLL)) {
					_scrolling_viewport = true;
					_cursor.fix_at = true;
				}
			}
		} else {
			if (mousewheel)
				DispatchMouseWheelEvent(w, GetWidgetFromPos(w, x - w.left, y - w.top), mousewheel);

			switch (click) {
			case 1: DispatchLeftClickEvent(w, x - w.left, y - w.top);  break;
			case 2: DispatchRightClickEvent(w, x - w.left, y - w.top); break;
			}
		}
	}

	void InputLoop()
	{
		int click;
		int mousewheel;

		Global._current_player = Global._local_player;

		// Handle pressed keys
		if (_pressed_key != 0) {
			int key = _pressed_key; _pressed_key = 0;
			HandleKeypress(key);
		}

		// Mouse event?
		click = 0;
		if (_left_button_down && !_left_button_clicked) {
			_left_button_clicked = true;
			click = 1;
		} else if (_right_button_clicked) {
			_right_button_clicked = false;
			click = 2;
		}

		mousewheel = 0;
		if (_cursor.wheel) {
			mousewheel = _cursor.wheel;
			_cursor.wheel = 0;
		}

		MouseLoop(click, mousewheel);
	}


	static int _we4_timer;

	static void UpdateWindows()
	{
		Window w;
		int t,i;

		t = _we4_timer + 1;
		if (t >= 100) 
		{
			for (i = _windows.size()-1; i >= 0; i--) 
			{
				w = _windows.get(i);
				w.CallWindowEventNP(WindowEvents.WE_4);
			}
			t = 0;
		}
		_we4_timer = t;

		for (i = _windows.size()-1; i >= 0; i--) 
		{
			w = _windows.get(i);
			if (0 != (w.flags4 & WF_WHITE_BORDER_MASK)) {
				w.flags4 -= WF_WHITE_BORDER_ONE;
				if ( 0 == (w.flags4 & WF_WHITE_BORDER_MASK)) {
					w.SetWindowDirty();
				}
			}
		}

		DrawDirtyBlocks();

		for (Window w : _windows) {
			if (w.viewport != null) UpdateViewportPosition(w);
		}
		DrawTextMessage();
		// Redraw mouse cursor in case it was hidden
		DrawMouseCursor();
	}


	int GetMenuItemIndex(final Window w, int x, int y)
	{
		if ((x -= w.left) >= 0 && x < w.width && (y -= w.top + 1) >= 0) {
			y /= 10;

			menu_d md = (menu_d) w.custom;
			//if (y < WP(w, final menu_d).item_count &&
			//		!HASBIT(WP(w, final menu_d).disabled_items, y)) 
			if (y < md.item_count &&
					!BitOps.HASBIT(md.disabled_items, y)) 
			{
				return y;
			}
		}
		return -1;
	}

	static void InvalidateWindow(WindowClass cls, WindowNumber number)
	{
		//final Window  w;

		for (Window w : _windows) {
			if (w.window_class == cls && w.window_number == number) SetWindowDirty(w);
		}
	}

	void InvalidateWidget(int widget_index)
	{
		final Widget wi = widget.get(widget_index);

		/* Don't redraw the window if the widget is invisible or of no-type */
		if (wi.type == WWT_EMPTY || BitOps.HASBIT(hidden_state, widget_index)) return;

		Global.hal.SetDirtyBlocks(left + wi.left, top + wi.top, left + wi.right + 1, top + wi.bottom + 1);
	}

	static void InvalidateWindowWidget(WindowClass cls, WindowNumber number, byte widget_index)
	{
		//final Window  w;

		for (Window w : _windows) {
			if (w.window_class == cls && w.window_number == number) {
				w.InvalidateWidget(widget_index);
			}
		}
	}

	static void InvalidateWindowClasses(WindowClass cls)
	{
		//final Window  w;

		for (Window w : _windows) {
			if (w.window_class == cls) w.SetWindowDirty();
		}
	}


	static void CallWindowTickEvent()
	{
		int i;
		for (i = _windows.size()-1; i >= 0; i--) {
			Window w = _windows.get(i);
			w.CallWindowEventNP(WindowEvents.WE_TICK);
		}
	}

	static void DeleteNonVitalWindows()
	{
		for (int i = 0; i < _windows.size();) 
		{
			Window w = _windows.get(i);
			if (w.window_class.v != WC_MAIN_WINDOW &&
					w.window_class.v != WC_SELECT_GAME &&
					w.window_class.v != WC_MAIN_TOOLBAR &&
					w.window_class.v != WC_STATUS_BAR &&
					w.window_class.v != WC_TOOLBAR_MENU &&
					w.window_class.v != WC_TOOLTIPS &&
					(w.flags4 & WF_STICKY) == 0) { // do not delete windows which are 'pinned'
				w.DeleteWindow();
				i = 0;
			} else {
				i++;
			}
		}
	}

	/* It is possible that a stickied window gets to a position where the
	 * 'close' button is outside the gaming area. You cannot close it then; except
	 * with this function. It closes all windows calling the standard function,
	 * then, does a little hacked loop of closing all stickied windows. Note
	 * that standard windows (status bar, etc.) are not stickied, so these aren't affected */
	static void DeleteAllNonVitalWindows()
	{
		//Window w;

		// Delete every window except for stickied ones
		DeleteNonVitalWindows();
		// Delete all sticked windows
		//for (w = _windows; w != _last_window;) {
		for (int i = 0; i < _windows.size();) 
		{
			Window w = _windows.get(i);
			if (0 != (w.flags4 & WF_STICKY)) {
				w.DeleteWindow();
				i = 0;
			} else
				i++;
		}
	}

	/* Delete all always on-top windows to get an empty screen */
	static void HideVitalWindows()
	{
		DeleteWindowById(WC_MAIN_TOOLBAR, 0);
		DeleteWindowById(WC_STATUS_BAR, 0);
	}

	static int PositionMainToolbar(Window w)
	{
		Global.DEBUG_misc( 1, "Repositioning Main Toolbar...");

		if (w == null || w.window_class.v != WC_MAIN_TOOLBAR)
			w = FindWindowById(WC_MAIN_TOOLBAR, 0);

		switch (Global._patches.toolbar_pos) {
		case 1:  w.left = (Global.hal._screen.width - w.width) >> 1; break;
		case 2:  w.left = Global.hal._screen.width - w.width; break;
		default: w.left = 0;
		}
		Global.hal.SetDirtyBlocks(0, 0, Global.hal._screen.width, w.height); // invalidate the whole top part
		return w.left;
	}

	static void RelocateAllWindows(int neww, int newh)
	{
		//Window w;

		for (Window w : _windows) {
			int left, top;

			if (w.window_class.v == WC_MAIN_WINDOW) {
				ViewPort vp = w.viewport;
				vp.width = w.width = neww;
				vp.height = w.height = newh;
				vp.virtual_width = neww << vp.zoom;
				vp.virtual_height = newh << vp.zoom;
				continue; // don't modify top,left
			}

			IConsoleResize();

			if (w.window_class.v == WC_MAIN_TOOLBAR) {
				top = w.top;
				left = PositionMainToolbar(w); // changes toolbar orientation
			} else if (w.window_class.v == WC_SELECT_GAME || w.window_class.v == WC_GAME_OPTIONS || w.window_class.v == WC_NETWORK_WINDOW){
				top = (newh - w.height) >> 1;
				left = (neww - w.width) >> 1;
			} else if (w.window_class.v == WC_NEWS_WINDOW) {
				top = newh - w.height;
				left = (neww - w.width) >> 1;
			} else if (w.window_class.v == WC_STATUS_BAR) {
				top = newh - w.height;
				left = (neww - w.width) >> 1;
			} else if (w.window_class.v == WC_SEND_NETWORK_MSG) {
				top = (newh - 26); // 26 = height of status bar + height of chat bar
				left = (neww - w.width) >> 1;
			} else {
				left = w.left;
				if (left + (w.width>>1) >= neww) left = neww - w.width;
				top = w.top;
				if (top + (w.height>>1) >= newh) top = newh - w.height;
			}

			if (w.viewport != null) {
				w.viewport.left += left - w.left;
				w.viewport.top += top - w.top;
			}

			w.left = left;
			w.top = top;
		}
	}


}

class WindowMessage {
	int msg;
	int wparam;
	int lparam;
}


class ResizeInfo {
	int width; /* Minimum width and height */
	int height;

	int step_width; /* In how big steps the width and height go */
	int step_height;
} 


class WindowClass  {

	public WindowClass(int cls) {
		v = cls;
	}

	int v;
}


class WindowNumber {
	public WindowNumber(int value) {
		n = value;
	}

	int n;
}


class Scrollbar {
	int count, cap, pos;
} ;




class Widget {
	byte type;
	byte resize_flag;
	byte color;
	int left, right, top, bottom;
	//int unkA;
	StringID tooltips;
} ;












/* XXX - outside "byte event" so you can set event directly without going into
 * the union elements at first. Because of this every first element of the union
 * MUST BE 'byte event'. Whoever did this must get shot! Scheduled for immediate
 * rewrite after 0.4.0 */
class WindowEvent {
	//int 
	WindowEvents event;
	Point pt;

	// click, dragdrop, mouseover
	int widget;

	// place
	TileIndex tile;
	TileIndex starttile;
	int userdata;

	// sizing
	Point size;
	Point diff;

	// edittext
	String str;

	// popupmenu;

	// dropdown
	int button;
	int index;

	// keypress
	boolean cont;   // continue the search? (default true)
	byte ascii;  // 8-bit ASCII-value of the key
	int keycode;// untranslated key (including shift-state)

	// message
	int msg;    // message to be sent
	int wparam; // additional message-specific information
	int lparam; // additional message-specific information
}

class SizeRect {
	int left,top,width,height;
} 

enum SpecialMouseMode {
	WSM_NONE = 0,
	WSM_DRAGDROP = 1,
	WSM_SIZING = 2,
	WSM_PRESIZE = 3,
}
