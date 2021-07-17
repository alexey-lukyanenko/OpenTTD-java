/* $Id: window.h 3336 2005-12-24 15:01:17Z tron $ */

#ifndef WINDOW_H
#define WINDOW_H

typedef union WindowEvent WindowEvent;

//typedef void WindowProc(Window *w, WindowEvent *e);

/* How the resize system works:
    First, you need to add a WWT_RESIZEBOX to the widgets, and you need
     to add the flag WDF_RESIZABLE to the window. Now the window is ready
     to resize itself.
    As you may have noticed, all widgets have a RESIZE_XXX in their line.
     This lines controls how the widgets behave on resize. RESIZE_NONE means
     it doesn't do anything. Any other option let's one of the borders
     move with the changed width/height. So if a widget has
     RESIZE_RIGHT, and the window is made 5 pixels wider by the user,
     the right of the window will also be made 5 pixels wider.
    Now, what if you want to clamp a widget to the bottom? Give it the flag
     RESIZE_TB. This is RESIZE_TOP + RESIZE_BOTTOM. Now if the window gets
     5 pixels bigger, both the top and bottom gets 5 bigger, so the whole
     widgets moves downwards without resizing, and appears to be clamped
     to the bottom. Nice aint it?
   You should know one more thing about this system. Most windows can't
    handle an increase of 1 pixel. So there is a step function, which
    let the windowsize only be changed by X pixels. You configure this
    after making the window, like this:
      w->resize.step_height = 10;
    Now the window will only change in height in steps of 10.
   You can also give a minimum width and height. The default value is
    the default height/width of the window itself. You can change this
    AFTER window-creation, with:
     w->resize.width or w->resize.height.
   That was all.. good luck, and enjoy :) -- TrueLight */

enum {
	RESIZE_NONE   = 0,

	RESIZE_LEFT   = 1,
	RESIZE_RIGHT  = 2,
	RESIZE_TOP    = 4,
	RESIZE_BOTTOM = 8,

	RESIZE_LR     = RESIZE_LEFT  | RESIZE_RIGHT,
	RESIZE_RB     = RESIZE_RIGHT | RESIZE_BOTTOM,
	RESIZE_TB     = RESIZE_TOP   | RESIZE_BOTTOM,
	RESIZE_LRB    = RESIZE_LEFT  | RESIZE_RIGHT  | RESIZE_BOTTOM,
	RESIZE_LRTB   = RESIZE_LEFT  | RESIZE_RIGHT  | RESIZE_TOP | RESIZE_BOTTOM,
	RESIZE_RTB    = RESIZE_RIGHT | RESIZE_TOP    | RESIZE_BOTTOM,
};
/*
typedef struct Widget {
	byte type;
	byte resize_flag;
	byte color;
	uint16 left, right, top, bottom;
	uint16 unkA;
	StringID tooltips;
} Widget;
*/
enum FrameFlags {
	FR_TRANSPARENT  = 0x01,  ///< Makes the background transparent if set
	FR_NOBORDER     = 0x08,  ///< Hide border (draws just a solid box)
	FR_BORDERONLY   = 0x10,  ///< Draw border only, no background
	FR_LOWERED      = 0x20,  ///< If set the frame is lowered and the background color brighter (ie. buttons when pressed)
	FR_DARKENED     = 0x40,  ///< If set the background is darker, allows for lowered frames with normal background color when used with FR_LOWERED (ie. dropdown boxes)
};

/* XXX - outside "byte event" so you can set event directly without going into
 * the union elements at first. Because of this every first element of the union
 * MUST BE 'byte event'. Whoever did this must get shot! Scheduled for immediate
 * rewrite after 0.4.0 */
union WindowEvent {
	byte event;
	struct {
		byte event;
		Point pt;
		int widget;
	} click;

	struct {
		byte event;
		Point pt;
		TileIndex tile;
		TileIndex starttile;
		int userdata;
	} place;

	struct {
		byte event;
		Point pt;
		int widget;
	} dragdrop;

	struct {
		byte event;
		Point size;
		Point diff;
	} sizing;

	struct {
		byte event;
		char *str;
	} edittext;

	struct {
		byte event;
		Point pt;
	} popupmenu;

	struct {
		byte event;
		int button;
		int index;
	} dropdown;

	struct {
		byte event;
		Point pt;
		int widget;
	} mouseover;

	struct {
		byte event;
		bool cont;   // continue the search? (default true)
		byte ascii;  // 8-bit ASCII-value of the key
		uint16 keycode;// untranslated key (including shift-state)
	} keypress;

	struct {
		byte event;
		uint msg;    // message to be sent
		uint wparam; // additional message-specific information
		uint lparam; // additional message-specific information
	} message;
};

enum WindowKeyCodes {
	WKC_SHIFT = 0x8000,
	WKC_CTRL  = 0x4000,
	WKC_ALT   = 0x2000,
	WKC_META  = 0x1000,

	// Special ones
	WKC_NONE = 0,
	WKC_ESC=1,
	WKC_BACKSPACE = 2,
	WKC_INSERT = 3,
	WKC_DELETE = 4,

	WKC_PAGEUP = 5,
	WKC_PAGEDOWN = 6,
	WKC_END = 7,
	WKC_HOME = 8,

	// Arrow keys
	WKC_LEFT = 9,
	WKC_UP = 10,
	WKC_RIGHT = 11,
	WKC_DOWN = 12,

	// Return & tab
	WKC_RETURN = 13,
	WKC_TAB = 14,

	// Numerical keyboard
	WKC_NUM_0 = 16,
	WKC_NUM_1 = 17,
	WKC_NUM_2 = 18,
	WKC_NUM_3 = 19,
	WKC_NUM_4 = 20,
	WKC_NUM_5 = 21,
	WKC_NUM_6 = 22,
	WKC_NUM_7 = 23,
	WKC_NUM_8 = 24,
	WKC_NUM_9 = 25,
	WKC_NUM_DIV = 26,
	WKC_NUM_MUL = 27,
	WKC_NUM_MINUS = 28,
	WKC_NUM_PLUS = 29,
	WKC_NUM_ENTER = 30,
	WKC_NUM_DECIMAL = 31,

	// Space
	WKC_SPACE = 32,

	// Function keys
	WKC_F1 = 33,
	WKC_F2 = 34,
	WKC_F3 = 35,
	WKC_F4 = 36,
	WKC_F5 = 37,
	WKC_F6 = 38,
	WKC_F7 = 39,
	WKC_F8 = 40,
	WKC_F9 = 41,
	WKC_F10 = 42,
	WKC_F11 = 43,
	WKC_F12 = 44,

	// backquote is the key left of "1"
	// we only store this key here, no matter what character is really mapped to it
	// on a particular keyboard. (US keyboard: ` and ~ ; German keyboard: ^ and �)
	WKC_BACKQUOTE = 45,
	WKC_PAUSE     = 46,

	// 0-9 are mapped to 48-57
	// A-Z are mapped to 65-90
	// a-z are mapped to 97-122
};

typedef struct WindowDesc {
	int16 left, top, width, height;
	WindowClass cls;
	WindowClass parent_cls;
	uint32 flags;
	const Widget *widgets;
	WindowProc *proc;
} WindowDesc;

enum {
	WDF_STD_TOOLTIPS   = 1, /* use standard routine when displaying tooltips */
	WDF_DEF_WIDGET     = 2,	/* default widget control for some widgets in the on click event */
	WDF_STD_BTN        = 4,	/* default handling for close and drag widgets (widget no 0 and 1) */

	WDF_UNCLICK_BUTTONS=16, /* Unclick buttons when the window event times out */
	WDF_STICKY_BUTTON  =32, /* Set window to sticky mode; they are not closed unless closed with 'X' (widget 2) */
	WDF_RESIZABLE      =64, /* A window can be resized */
};

/* can be used as x or y coordinates to cause a specific placement */
enum {
	WDP_AUTO = -1,
	WDP_CENTER = -2,
};

typedef struct Textbuf {
	char *buf;                  /* buffer in which text is saved */
	uint16 maxlength, maxwidth; /* the maximum size of the buffer. Maxwidth specifies screensize in pixels */
	uint16 length, width;       /* the current size of the buffer. Width specifies screensize in pixels */
	bool caret;                 /* is the caret ("_") visible or not */
	uint16 caretpos;            /* the current position of the caret in the buffer */
	uint16 caretxoffs;          /* the current position of the caret in pixels */
} Textbuf;

typedef struct querystr_d {
	StringID caption;
	WindowClass wnd_class;
	WindowNumber wnd_num;
	Textbuf text;
	const char* orig;
} querystr_d;

//#define WP(ptr,str) (*(str*)(ptr)->custom)
// querystr_d is the largest struct that comes in w->custom
//  because 64-bit systems use 64-bit pointers, it is bigger on a 64-bit system
//  than on a 32-bit system. Therefore, the size is calculated from querystr_d
//  instead of a hardcoded number.
// if any struct becomes bigger the querystr_d, it should be replaced.
//#define WINDOW_CUSTOM_SIZE sizeof(querystr_d)
/*
typedef struct Scrollbar {
	uint16 count, cap, pos;
} Scrollbar;
/*
typedef struct ResizeInfo {
	uint width; /* Minimum width and height * /
	uint height;

	uint step_width; /* In how big steps the width and height go * 
	uint step_height;
} ResizeInfo;

typedef struct WindowMessage {
		int msg;
		int wparam;
		int lparam;
} WindowMessage;
/*
struct Window {
	uint16 flags4;
	WindowClass window_class;
	WindowNumber window_number;

	int left, top;
	int width, height;

	Scrollbar hscroll, vscroll, vscroll2;
	ResizeInfo resize;

	byte caption_color;

	uint32 click_state, disabled_state, hidden_state;
	WindowProc *wndproc;
	ViewPort *viewport;
	const Widget *original_widget;
 	Widget *widget;
	uint32 desc_flags;

	WindowMessage message;
	byte custom[WINDOW_CUSTOM_SIZE];
};
*/

/*
typedef struct {
	byte item_count; /* follow_vehicle * /
	byte sel_index;		/* scrollpos_x * /
	byte main_button; /* scrollpos_y * /
	byte action_id;
	StringID string_id; /* unk30 * /
	uint16 checked_items; /* unk32 * /
	byte disabled_items;
} menu_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(menu_d));

typedef struct {
	int16 data_1, data_2, data_3;
	int16 data_4, data_5;
	bool close;
	byte byte_1;
} def_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(def_d));

typedef struct {
	void *data;
} void_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(void_d));

typedef struct {
	uint16 base;
	uint16 count;
} tree_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(tree_d));

typedef struct {
	byte refresh_counter;
} plstations_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(plstations_d));

typedef struct {
	StringID string_id;
} tooltips_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(tooltips_d));

typedef struct {
	byte railtype;
	byte sel_index;
	EngineID sel_engine;
	EngineID rename_engine;
} buildtrain_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(buildtrain_d));

typedef struct {
	byte vehicletype;
	byte sel_index[2];
	EngineID sel_engine[2];
	uint16 count[2];
} replaceveh_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(replaceveh_d));

typedef struct {
	VehicleID sel;
} traindepot_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(traindepot_d));

typedef struct {
	int sel;
} order_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(order_d));

typedef struct {
	byte tab;
} traindetails_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(traindetails_d));

typedef struct {
	int32 scroll_x;
	int32 scroll_y;
	int32 subscroll;
} smallmap_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(traindetails_d));

typedef struct {
	uint32 face;
	byte gender;
} facesel_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(facesel_d));

typedef struct {
	int sel;
	byte cargo;
} refit_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(refit_d));

typedef struct {
	VehicleID follow_vehicle;
	int32 scrollpos_x;
	int32 scrollpos_y;
} vp_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(vp_d) + 3 * sizeof(byte)); // + 3 * byte is a hack from Miham

// vp2_d is the same as vp_d, except for the data_# values..
typedef struct {
	uint16 follow_vehicle;
	int32 scrollpos_x;
	int32 scrollpos_y;
	byte data_1;
	byte data_2;
	byte data_3;
} vp2_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(vp2_d));

typedef struct {
	uint16 follow_vehicle;
	int32 scrollpos_x;
	int32 scrollpos_y;
	NewsItem *ni;
} news_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(news_d));

typedef struct {
	uint32 background_img;
	int8 rank;
} highscore_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(highscore_d));

typedef struct {
	int height;
	uint16 counter;
} scroller_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(scroller_d));

typedef enum VehicleListFlags {
	VL_DESC    = 0x01,
	VL_RESORT  = 0x02,
	VL_REBUILD = 0x04
} VehicleListFlags;

typedef struct vehiclelist_d {
	SortStruct *sort_list;
	uint16 list_length;
	byte sort_type;
	VehicleListFlags flags;
	uint16 resort_timer;
} vehiclelist_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(vehiclelist_d));

typedef struct message_d {
	int msg;
	int wparam;
	int lparam;
} message_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(message_d));

typedef struct dropdown_d {
	uint32 disabled_state;
	uint32 hidden_state;
	WindowClass parent_wnd_class;
	WindowNumber parent_wnd_num;
	byte parent_button;
	byte num_items;
	byte selected_index;
	const StringID *items;
	byte click_delay;
	bool drag_mode;
} dropdown_d;
assert_compile(WINDOW_CUSTOM_SIZE >= sizeof(dropdown_d));
*/
enum WindowEvents {
	WE_CLICK = 0,
	WE_PAINT = 1,
	WE_MOUSELOOP = 2,
	WE_TICK = 3,
	WE_4 = 4,
	WE_TIMEOUT = 5,
	WE_PLACE_OBJ = 6,
	WE_ABORT_PLACE_OBJ = 7,
	WE_DESTROY = 8,
	WE_ON_EDIT_TEXT = 9,
	WE_POPUPMENU_SELECT = 10,
	WE_POPUPMENU_OVER = 11,
	WE_DRAGDROP = 12,
	WE_PLACE_DRAG = 13,
	WE_PLACE_MOUSEUP = 14,
	WE_PLACE_PRESIZE = 15,
	WE_DROPDOWN_SELECT = 16,
	WE_RCLICK = 17,
	WE_KEYPRESS = 18,
	WE_CREATE = 19,
	WE_MOUSEOVER = 20,
	WE_ON_EDIT_TEXT_CANCEL = 21,
	WE_RESIZE = 22,
	WE_MESSAGE = 23
};


/****************** THESE ARE NOT WIDGET TYPES!!!!! *******************/
enum WindowWidgetBehaviours {
	WWB_PUSHBUTTON = 1 << 5,
	WWB_NODISBUTTON = 2 << 5,
};


enum WindowWidgetTypes {
	WWT_EMPTY = 0,

	WWT_IMGBTN = 1,						/* button with image */
	WWT_PANEL = WWT_IMGBTN,
	WWT_PANEL_2 = 2,					/* button with diff image when clicked */

	WWT_TEXTBTN = 3,					/* button with text */
	WWT_4 = 4,								/* button with diff text when clicked */
	WWT_5 = 5,								/* label */
	WWT_6 = 6,								/* combo box text area */
	WWT_MATRIX = 7,
	WWT_SCROLLBAR = 8,
	WWT_FRAME = 9,						/* frame */
	WWT_CAPTION = 10,

	WWT_HSCROLLBAR = 11,
	WWT_STICKYBOX = 12,
	WWT_SCROLL2BAR = 13,				/* 2nd vertical scrollbar*/
	WWT_RESIZEBOX = 14,
	WWT_CLOSEBOX = 15,
	WWT_LAST = 16,						/* Last Item. use WIDGETS_END to fill up padding!! */

	WWT_MASK = 31,

	WWT_PUSHTXTBTN	= WWT_TEXTBTN	| WWB_PUSHBUTTON,
	WWT_PUSHIMGBTN	= WWT_IMGBTN	| WWB_PUSHBUTTON,
	WWT_NODISTXTBTN = WWT_TEXTBTN	| WWB_NODISBUTTON,
};

#define WIDGETS_END WWT_LAST,   RESIZE_NONE,     0,     0,     0,     0,     0, 0, STR_NULL

enum WindowFlags {
	WF_TIMEOUT_SHL = 0,
	WF_TIMEOUT_MASK = 7,
	WF_DRAGGING = 1 << 3,
	WF_SCROLL_UP = 1 << 4,
	WF_SCROLL_DOWN = 1 << 5,
	WF_SCROLL_MIDDLE = 1 << 6,
	WF_HSCROLL = 1 << 7,
	WF_SIZING = 1 << 8,
	WF_STICKY = 1 << 9,

	WF_DISABLE_VP_SCROLL = 1 << 10,

	WF_WHITE_BORDER_ONE = 1 << 11,
	WF_WHITE_BORDER_MASK = 3 << 11,
	WF_SCROLL2 = 1 << 13,
};

/* window.c */
void DrawOverlappedWindow(Window *w, int left, int top, int right, int bottom);
void CallWindowEventNP(Window *w, int event);
void CallWindowTickEvent(void);
void SetWindowDirty(const Window* w);
void SendWindowMessage(WindowClass wnd_class, WindowNumber wnd_num, uint msg, uint wparam, uint lparam);

Window *FindWindowById(WindowClass cls, WindowNumber number);
void DeleteWindow(Window *w);
Window *BringWindowToFrontById(WindowClass cls, WindowNumber number);
Window *BringWindowToFront(Window *w);
Window *StartWindowDrag(Window *w);
Window *StartWindowSizing(Window *w);
Window *FindWindowFromPt(int x, int y);

bool IsWindowOfPrototype(const Window* w, const Widget* widget);
void AssignWidgetToWindow(Window *w, const Widget *widget);
Window *AllocateWindow(
							int x,
							int y,
							int width,
							int height,
							WindowProc *proc,
							WindowClass cls,
							const Widget *widget);

Window *AllocateWindowDesc(const WindowDesc *desc);
Window *AllocateWindowDescFront(const WindowDesc *desc, int value);

Window *AllocateWindowAutoPlace(
	int width,
	int height,
	WindowProc *proc,
	WindowClass cls,
	const Widget *widget);

Window *AllocateWindowAutoPlace2(
	WindowClass exist_class,
	WindowNumber exist_num,
	int width,
	int height,
	WindowProc *proc,
	WindowClass cls,
	const Widget *widget);

void DrawWindowViewport(Window *w);

void InitWindowSystem(void);
void UnInitWindowSystem(void);
void ResetWindowSystem(void);
int GetMenuItemIndex(const Window *w, int x, int y);
void InputLoop(void);
void UpdateWindows(void);
void InvalidateWidget(const Window* w, byte widget_index);

void GuiShowTooltips(StringID string_id);

void UnclickWindowButtons(Window *w);
void UnclickSomeWindowButtons(Window *w, uint32 mask);
void RelocateAllWindows(int neww, int newh);
int PositionMainToolbar(Window *w);

/* widget.c */
int GetWidgetFromPos(const Window *w, int x, int y);
void DrawWindowWidgets(const Window *w);
void ShowDropDownMenu(Window *w, const StringID *strings, int selected, int button, uint32 disabled_mask, uint32 hidden_mask);

void HandleButtonClick(Window *w, byte widget);

Window *GetCallbackWnd(void);
void DeleteNonVitalWindows(void);
void DeleteAllNonVitalWindows(void);
void HideVitalWindows(void);
void ShowVitalWindows(void);

/* window.c */
VARDEF Window _windows[25];
VARDEF Window *_last_window;

VARDEF Point _cursorpos_drag_start;

VARDEF bool _left_button_down;
VARDEF bool _left_button_clicked;

VARDEF bool _right_button_down;
VARDEF bool _right_button_clicked;

VARDEF int _alloc_wnd_parent_num;

VARDEF int _scrollbar_start_pos;
VARDEF int _scrollbar_size;
VARDEF byte _scroller_click_timeout;

VARDEF bool _scrolling_scrollbar;
VARDEF bool _scrolling_viewport;
VARDEF bool _popup_menu_active;

VARDEF byte _special_mouse_mode;
enum SpecialMouseMode {
	WSM_NONE = 0,
	WSM_DRAGDROP = 1,
	WSM_SIZING = 2,
	WSM_PRESIZE = 3,
};

void ScrollbarClickHandler(Window *w, const Widget *wi, int x, int y);

#endif /* WINDOW_H */
