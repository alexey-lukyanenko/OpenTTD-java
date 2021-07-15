/* $Id: gfx.h 3191 2005-11-16 08:35:26Z tron $ */

#ifndef GFX_H
#define GFX_H

typedef byte Pixel;

typedef struct ColorList {
	byte unk0, unk1, unk2;
	byte window_color_1a, window_color_1b;
	byte window_color_bga, window_color_bgb;
	byte window_color_2;
} ColorList;
/*
struct DrawPixelInfo {
	Pixel *dst_ptr;
	int left, top, width, height;
	int pitch;
	uint16 zoom;
};


typedef struct CursorVars {
	Point pos, size, offs, delta;
	Point draw_pos, draw_size;
	CursorID sprite;

	int wheel; // mouse wheel movement
	const CursorID *animate_list, *animate_cur;
	uint animate_timeout;

	bool visible;
	bool dirty;
	bool fix_at;
} CursorVars;
*/

void RedrawScreenRect(int left, int top, int right, int bottom);
void GfxScroll(int left, int top, int width, int height, int xo, int yo);


// XXX doesn't really belong here, but the only
// consumers always use it in conjunction with DoDrawString()
#define UPARROW   "\x80"
#define DOWNARROW "\xAA"


int DrawStringCentered(int x, int y, StringID str, uint16 color);
int DrawStringCenteredTruncated(int xl, int xr, int y, StringID str, uint16 color);
int DoDrawStringCentered(int x, int y, const char *str, uint16 color);

int DrawString(int x, int y, StringID str, uint16 color);
int DrawStringTruncated(int x, int y, StringID str, uint16 color, uint maxw);

int DoDrawString(const char *string, int x, int y, uint16 color);
int DoDrawStringTruncated(const char *str, int x, int y, uint16 color, uint maxw);

void DrawStringCenterUnderline(int x, int y, StringID str, uint16 color);
void DrawStringCenterUnderlineTruncated(int xl, int xr, int y, StringID str, uint16 color);

void DrawStringRightAligned(int x, int y, StringID str, uint16 color);
void DrawStringRightAlignedTruncated(int x, int y, StringID str, uint16 color, uint maxw);

void GfxFillRect(int left, int top, int right, int bottom, int color);
void GfxDrawLine(int left, int top, int right, int bottom, int color);
void DrawFrameRect(int left, int top, int right, int bottom, int color, int flags);
uint16 GetDrawStringPlayerColor(PlayerID player);

int GetStringWidth(const char *str);
void LoadStringWidthTable(void);
void DrawStringMultiCenter(int x, int y, StringID str, int maxw);
void DrawStringMultiLine(int x, int y, StringID str, int maxw);
void DrawDirtyBlocks(void);
void SetDirtyBlocks(int left, int top, int right, int bottom);
void MarkWholeScreenDirty(void);

void GfxInitPalettes(void);

bool FillDrawPixelInfo(DrawPixelInfo* n, const DrawPixelInfo* o, int left, int top, int width, int height);

/* window.c */
void DrawOverlappedWindowForAll(int left, int top, int right, int bottom);

void SetMouseCursor(uint cursor);
void SetAnimatedMouseCursor(const CursorID *table);
void CursorTick(void);
void DrawMouseCursor(void);
void ScreenSizeChanged(void);
void UndrawMouseCursor(void);
bool ChangeResInGame(int w, int h);
void SortResolutions(int count);
void ToggleFullScreen(bool fs);

/* gfx.c */
#define ASCII_LETTERSTART 32
VARDEF int _stringwidth_base;
VARDEF byte _stringwidth_table[0x2A0];
static inline byte GetCharacterWidth(uint key)
{
	assert(key >= ASCII_LETTERSTART && key - ASCII_LETTERSTART < lengthof(_stringwidth_table));
	return _stringwidth_table[key - ASCII_LETTERSTART];
}

//VARDEF DrawPixelInfo _screen;
//VARDEF DrawPixelInfo *_cur_dpi;
VARDEF ColorList _color_list[16];
//VARDEF CursorVars _cursor;

VARDEF int _pal_first_dirty;
VARDEF int _pal_last_dirty;

VARDEF bool _use_dos_palette;

typedef struct Colour {
	byte r;
	byte g;
	byte b;
} Colour;

extern Colour _cur_palette[256];


typedef enum StringColorFlags {
	IS_PALETTE_COLOR = 0x100, // color value is already a real palette color index, not an index of a StringColor
} StringColorFlags;

#ifdef _DEBUG
extern bool _dbg_screen_rect;
#endif

#endif /* GFX_H */
