/* $Id: win32_v.c 3320 2005-12-19 00:19:12Z Darkvater $ */

#include "../stdafx.h"
#include "../openttd.h"
#include "../functions.h"
#include "../gfx.h"
#include "../macros.h"
#include "../network.h"
#include "../variables.h"
#include "../win32.h"
#include "../window.h"
#include "win32_v.h"
#include <windows.h>

static struct {
	HWND main_wnd;
	HBITMAP dib_sect;
	Pixel *bitmap_bits;
	Pixel *buffer_bits;
	Pixel *alloced_bits;
	HPALETTE gdi_palette;
	int width,height;
	int width_org, height_org;
	bool fullscreen;
	bool double_size;
	bool has_focus;
	bool running;
} _wnd;

static void MakePalette(void)
{
	LOGPALETTE *pal;
	uint i;

	pal = alloca(sizeof(LOGPALETTE) + (256-1) * sizeof(PALETTEENTRY));

	pal->palVersion = 0x300;
	pal->palNumEntries = 256;

	for (i = 0; i != 256; i++) {
		pal->palPalEntry[i].peRed   = _cur_palette[i].r;
		pal->palPalEntry[i].peGreen = _cur_palette[i].g;
		pal->palPalEntry[i].peBlue  = _cur_palette[i].b;
		pal->palPalEntry[i].peFlags = 0;

	}
	_wnd.gdi_palette = CreatePalette(pal);
	if (_wnd.gdi_palette == NULL)
		error("CreatePalette failed!\n");
}

static void UpdatePalette(HDC dc, uint start, uint count)
{
	RGBQUAD rgb[256];
	uint i;

	for (i = 0; i != count; i++) {
		rgb[i].rgbRed   = _cur_palette[start + i].r;
		rgb[i].rgbGreen = _cur_palette[start + i].g;
		rgb[i].rgbBlue  = _cur_palette[start + i].b;
		rgb[i].rgbReserved = 0;
	}

	SetDIBColorTable(dc, start, count, rgb);
}

typedef struct {
	byte vk_from;
	byte vk_count;
	byte map_to;
} VkMapping;

#define AS(x, z) {x, 0, z}
#define AM(x, y, z, w) {x, y - x, z}

#ifndef VK_OEM_3
#define VK_OEM_3 0xC0
#endif

static const VkMapping _vk_mapping[] = {
	// Pageup stuff + up/down
	AM(VK_PRIOR,VK_DOWN, WKC_PAGEUP, WKC_DOWN),
	// Map letters & digits
	AM('A','Z','A','Z'),
	AM('0','9','0','9'),

	AS(VK_ESCAPE,		WKC_ESC),
	AS(VK_PAUSE, WKC_PAUSE),
	AS(VK_BACK,			WKC_BACKSPACE),
	AM(VK_INSERT,VK_DELETE,WKC_INSERT, WKC_DELETE),

	AS(VK_SPACE,		WKC_SPACE),
	AS(VK_RETURN,		WKC_RETURN),
	AS(VK_TAB,			WKC_TAB),

	// Function keys
	AM(VK_F1, VK_F12,	WKC_F1, WKC_F12),

	// Numeric part.
	// What is the virtual keycode for numeric enter??
	AM(VK_NUMPAD0,VK_NUMPAD9, WKC_NUM_0, WKC_NUM_9),
	AS(VK_DIVIDE,			WKC_NUM_DIV),
	AS(VK_MULTIPLY,		WKC_NUM_MUL),
	AS(VK_SUBTRACT,		WKC_NUM_MINUS),
	AS(VK_ADD,				WKC_NUM_PLUS),
	AS(VK_DECIMAL,		WKC_NUM_DECIMAL)
};

static uint MapWindowsKey(uint sym)
{
	const VkMapping *map;
	uint key = 0;

	for (map = _vk_mapping; map != endof(_vk_mapping); ++map) {
		if ((uint)(sym - map->vk_from) <= map->vk_count) {
			key = sym - map->vk_from + map->map_to;
			break;
		}
	}

	if (GetAsyncKeyState(VK_SHIFT)   < 0) key |= WKC_SHIFT;
	if (GetAsyncKeyState(VK_CONTROL) < 0) key |= WKC_CTRL;
	if (GetAsyncKeyState(VK_MENU)    < 0) key |= WKC_ALT;
	return key;
}

static void MakeWindow(bool full_screen);
static bool AllocateDibSection(int w, int h);

static void ClientSizeChanged(int w, int h)
{
	if (_wnd.double_size) {
		w /= 2;
		h /= 2;
	}

	// allocate new dib section of the new size
	if (AllocateDibSection(w, h)) {
		// mark all palette colors dirty
		_pal_first_dirty = 0;
		_pal_last_dirty = 255;
		GameSizeChanged();

		// redraw screen
		if (_wnd.running) {
			_screen.dst_ptr = _wnd.buffer_bits;
			UpdateWindows();
		}
	}
}

extern void DoExitSave(void);

#ifdef _DEBUG
// Keep this function here..
// It allows you to redraw the screen from within the MSVC debugger
int RedrawScreenDebug(void)
{
	HDC dc,dc2;
	static int _fooctr;
	HBITMAP old_bmp;
	HPALETTE old_palette;

	_screen.dst_ptr = _wnd.buffer_bits;
	UpdateWindows();

	dc = GetDC(_wnd.main_wnd);
	dc2 = CreateCompatibleDC(dc);

	old_bmp = SelectObject(dc2, _wnd.dib_sect);
	old_palette = SelectPalette(dc, _wnd.gdi_palette, FALSE);
	BitBlt(dc, 0, 0, _wnd.width, _wnd.height, dc2, 0, 0, SRCCOPY);
	SelectPalette(dc, old_palette, TRUE);
	SelectObject(dc2, old_bmp);
	DeleteDC(dc2);
	ReleaseDC(_wnd.main_wnd, dc);

	return _fooctr++;
}
#endif

static LRESULT CALLBACK WndProcGdi(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc,dc2;
		HBITMAP old_bmp;
		HPALETTE old_palette;
		BeginPaint(hwnd, &ps);
		dc = ps.hdc;
		dc2 = CreateCompatibleDC(dc);
		old_bmp = SelectObject(dc2, _wnd.dib_sect);
		old_palette = SelectPalette(dc, _wnd.gdi_palette, FALSE);

		if (_pal_last_dirty != -1) {
			UpdatePalette(dc2, _pal_first_dirty, _pal_last_dirty - _pal_first_dirty + 1);
			_pal_last_dirty = -1;
		}

		BitBlt(dc, 0, 0, _wnd.width, _wnd.height, dc2, 0, 0, SRCCOPY);
		SelectPalette(dc, old_palette, TRUE);
		SelectObject(dc2, old_bmp);
		DeleteDC(dc2);
		EndPaint(hwnd, &ps);
		}
		return 0;

	case WM_PALETTECHANGED:
		if ((HWND)wParam == hwnd)
			return 0;
		// FALL THROUGH
	case WM_QUERYNEWPALETTE: {
		HDC hDC = GetWindowDC(hwnd);
		HPALETTE hOldPalette = SelectPalette(hDC, _wnd.gdi_palette, FALSE);
		UINT nChanged = RealizePalette(hDC);
		SelectPalette(hDC, hOldPalette, TRUE);
		ReleaseDC(hwnd, hDC);
		if (nChanged)
			InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_CLOSE:
		if (_game_mode == GM_MENU) { // do not ask to quit on the main screen
			_exit_game = true;
		} else if (_patches.autosave_on_exit) {
			DoExitSave();
			_exit_game = true;
		} else
			AskExitGame();

		return 0;

	case WM_LBUTTONDOWN:
		SetCapture(hwnd);
		_left_button_down = true;
		return 0;

	case WM_LBUTTONUP:
		ReleaseCapture();
		_left_button_down = false;
		_left_button_clicked = false;
		return 0;

	case WM_RBUTTONDOWN:
		SetCapture(hwnd);
		_right_button_down = true;
		_right_button_clicked = true;
		return 0;

	case WM_RBUTTONUP:
		ReleaseCapture();
		_right_button_down = false;
		return 0;

	case WM_MOUSEMOVE: {
		int x = (int16)LOWORD(lParam);
		int y = (int16)HIWORD(lParam);
		POINT pt;

		if (_wnd.double_size) {
			x /= 2;
			y /= 2;
		}

		if (_cursor.fix_at) {
			int dx = x - _cursor.pos.x;
			int dy = y - _cursor.pos.y;
			if (dx != 0 || dy != 0) {
				_cursor.delta.x += dx;
				_cursor.delta.y += dy;

				pt.x = _cursor.pos.x;
				pt.y = _cursor.pos.y;

				if (_wnd.double_size) {
					pt.x *= 2;
					pt.y *= 2;
				}
				ClientToScreen(hwnd, &pt);
				SetCursorPos(pt.x, pt.y);
			}
		} else {
			_cursor.delta.x += x - _cursor.pos.x;
			_cursor.delta.y += y - _cursor.pos.y;
			_cursor.pos.x = x;
			_cursor.pos.y = y;
			_cursor.dirty = true;
		}
		MyShowCursor(false);
		return 0;
	}

	case WM_KEYDOWN: {
		// this is the rewritten ascii input function
		// it disables windows deadkey handling --> more linux like :D
		unsigned short w = 0;
		int r = 0;
		byte ks[256];
		unsigned int scan = 0;
		uint16 scancode = (( lParam & 0xFF0000 ) >> 16 );

		GetKeyboardState(ks);
		r = ToAscii(wParam, scan, ks, &w, 0);
		if (r == 0) w = 0; // no translation was possible

		_pressed_key = w | MapWindowsKey(wParam) << 16;

		if (scancode == 41)
			_pressed_key = w | WKC_BACKQUOTE << 16;

		if ((_pressed_key >> 16) == ('D' | WKC_CTRL) && !_wnd.fullscreen) {
			_double_size ^= 1;
			_wnd.double_size = _double_size;
			ClientSizeChanged(_wnd.width, _wnd.height);
			MarkWholeScreenDirty();
		}
	} break;

	case WM_SYSKEYDOWN: /* user presses F10 or Alt, both activating the title-menu */
		switch (wParam) {
		case VK_RETURN: case 0x46: /* Full Screen on ALT + ENTER/F(VK_F) */
			ToggleFullScreen(!_wnd.fullscreen);
			return 0;
		case VK_MENU: /* Just ALT */
			return 0; // do nothing
		case VK_F10: /* F10, ignore activation of menu */
			_pressed_key = MapWindowsKey(wParam) << 16;
			return 0;
		default: /* ALT in combination with something else */
			_pressed_key = MapWindowsKey(wParam) << 16;
			break;
		}
		break;
	case WM_NCMOUSEMOVE:
		MyShowCursor(true);
		return 0;

	case WM_SIZE: {
		if (wParam != SIZE_MINIMIZED) {
			ClientSizeChanged(LOWORD(lParam), HIWORD(lParam));
		}
		return 0;
	}
	case WM_SIZING: {
		RECT* r = (RECT*)lParam;
		RECT r2;
		int w, h;

		SetRect(&r2, 0, 0, 0, 0);
		AdjustWindowRect(&r2, GetWindowLong(hwnd, GWL_STYLE), FALSE);

		w = r->right - r->left - (r2.right - r2.left);
		h = r->bottom - r->top - (r2.bottom - r2.top);
		if (_wnd.double_size) {
			w /= 2;
			h /= 2;
		}
		w = clamp(w, 64, MAX_SCREEN_WIDTH);
		h = clamp(h, 64, MAX_SCREEN_HEIGHT);
		if (_wnd.double_size) {
			w *= 2;
			h *= 2;
		}
		SetRect(&r2, 0, 0, w, h);

		AdjustWindowRect(&r2, GetWindowLong(hwnd, GWL_STYLE), FALSE);
		w = r2.right - r2.left;
		h = r2.bottom - r2.top;

		switch (wParam) {
		case WMSZ_BOTTOM:
			r->bottom = r->top + h;
			break;
		case WMSZ_BOTTOMLEFT:
			r->bottom = r->top + h;
			r->left = r->right - w;
			break;
		case WMSZ_BOTTOMRIGHT:
			r->bottom = r->top + h;
			r->right = r->left + w;
			break;
		case WMSZ_LEFT:
			r->left = r->right - w;
			break;
		case WMSZ_RIGHT:
			r->right = r->left + w;
			break;
		case WMSZ_TOP:
			r->top = r->bottom - h;
			break;
		case WMSZ_TOPLEFT:
			r->top = r->bottom - h;
			r->left = r->right - w;
			break;
		case WMSZ_TOPRIGHT:
			r->top = r->bottom - h;
			r->right = r->left + w;
			break;
		}
		return TRUE;
	}

// needed for wheel
#if !defined(WM_MOUSEWHEEL)
# define WM_MOUSEWHEEL                   0x020A
#endif  //WM_MOUSEWHEEL
#if !defined(GET_WHEEL_DELTA_WPARAM)
# define GET_WHEEL_DELTA_WPARAM(wparam) ((short)HIWORD(wparam))
#endif  //GET_WHEEL_DELTA_WPARAM

	case WM_MOUSEWHEEL: {
		int delta = GET_WHEEL_DELTA_WPARAM(wParam);

		if (delta < 0) {
			_cursor.wheel++;
		} else if (delta > 0) {
			_cursor.wheel--;
		}
		return 0;
	}

	case WM_ACTIVATEAPP:
		_wnd.has_focus = (bool)wParam;
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void RegisterWndClass(void)
{
	static bool registered;
	if (!registered) {
		HINSTANCE hinst = GetModuleHandle(NULL);
		WNDCLASS wnd = {
			0,
			WndProcGdi,
			0,
			0,
			hinst,
			LoadIcon(hinst, MAKEINTRESOURCE(100)),
			LoadCursor(NULL, IDC_ARROW),
			0,
			0,
			"OTTD"
		};
		registered = true;
		if (!RegisterClass(&wnd))
			error("RegisterClass failed");
	}
}

extern const char _openttd_revision[];

static void MakeWindow(bool full_screen)
{
	_fullscreen = full_screen;

	_wnd.double_size = _double_size && !full_screen;

	// recreate window?
	if ((full_screen || _wnd.fullscreen) && _wnd.main_wnd) {
		DestroyWindow(_wnd.main_wnd);
		_wnd.main_wnd = 0;
	}

	if (full_screen) {
		DEVMODE settings;
		memset(&settings, 0, sizeof(DEVMODE));
		settings.dmSize = sizeof(DEVMODE);
		settings.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (_fullscreen_bpp) {
			settings.dmBitsPerPel = _fullscreen_bpp;
			settings.dmFields |= DM_BITSPERPEL;
		}
		settings.dmPelsWidth = _wnd.width_org;
		settings.dmPelsHeight = _wnd.height_org;
		settings.dmDisplayFrequency = _display_hz;
		if (settings.dmDisplayFrequency != 0)
			settings.dmFields |= DM_DISPLAYFREQUENCY;
		if (ChangeDisplaySettings(&settings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
			MakeWindow(false);
			return;
		}
	} else if (_wnd.fullscreen) {
		// restore display?
		ChangeDisplaySettings(NULL, 0);
	}

	{
		RECT r;
		uint style;
		int x, y, w, h;

		_wnd.fullscreen = full_screen;
		if (_wnd.fullscreen) {
			style = WS_POPUP | WS_VISIBLE;
			SetRect(&r, 0, 0, _wnd.width_org, _wnd.height_org);
		} else {
			style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
			SetRect(&r, 0, 0, _wnd.width, _wnd.height);
		}

		AdjustWindowRect(&r, style, FALSE);
		w = r.right - r.left;
		h = r.bottom - r.top;
		x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
		y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

		if (_wnd.main_wnd) {
			SetWindowPos(_wnd.main_wnd, 0, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		} else {
			char Windowtitle[50];

			snprintf(Windowtitle, lengthof(Windowtitle), "OpenTTD %s", _openttd_revision);

			_wnd.main_wnd = CreateWindow("OTTD", Windowtitle, style, x, y, w, h, 0, 0, GetModuleHandle(NULL), 0);
			if (_wnd.main_wnd == NULL)
				error("CreateWindow failed");
		}
	}
	GameSizeChanged(); // invalidate all windows, force redraw
}

static bool AllocateDibSection(int w, int h)
{
	BITMAPINFO *bi;
	HDC dc;

	w = clamp(w, 64, MAX_SCREEN_WIDTH);
	h = clamp(h, 64, MAX_SCREEN_HEIGHT);

	if (w == _screen.width && h == _screen.height)
		return false;

	_screen.width = w;
	_screen.pitch = ALIGN(w, 4);
	_screen.height = h;

	if (_wnd.alloced_bits) {
		free(_wnd.alloced_bits);
		_wnd.alloced_bits = NULL;
	}

	bi = alloca(sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD)*256);
	memset(bi, 0, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD)*256);
	bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	if (_wnd.double_size) {
		w = ALIGN(w, 4);
		_wnd.alloced_bits = _wnd.buffer_bits = malloc(w * h);
		w *= 2;
		h *= 2;
	}

	bi->bmiHeader.biWidth = _wnd.width = w;
	bi->bmiHeader.biHeight = -(_wnd.height = h);

	bi->bmiHeader.biPlanes = 1;
	bi->bmiHeader.biBitCount = 8;
	bi->bmiHeader.biCompression = BI_RGB;

	if (_wnd.dib_sect)
		DeleteObject(_wnd.dib_sect);

	dc = GetDC(0);
	_wnd.dib_sect = CreateDIBSection(dc, bi, DIB_RGB_COLORS, (void**)&_wnd.bitmap_bits, NULL, 0);
	if (_wnd.dib_sect == NULL)
		error("CreateDIBSection failed");
	ReleaseDC(0, dc);

	if (!_wnd.double_size)
		_wnd.buffer_bits = _wnd.bitmap_bits;

	return true;
}

static const uint16 default_resolutions[][2] = {
	{ 640,  480},
	{ 800,  600},
	{1024,  768},
	{1152,  864},
	{1280,  800},
	{1280,  960},
	{1280, 1024},
	{1400, 1050},
	{1600, 1200},
	{1680, 1050},
	{1920, 1200}
};

static void FindResolutions(void)
{
	int i = 0, n = 0;
	DEVMODE dm;

	while (EnumDisplaySettings(NULL, i++, &dm) != 0) {
		if (dm.dmBitsPerPel == 8 && IS_INT_INSIDE(dm.dmPelsWidth, 640, MAX_SCREEN_WIDTH + 1) &&
				IS_INT_INSIDE(dm.dmPelsHeight, 480, MAX_SCREEN_HEIGHT + 1)){
			int j;
			for (j = 0; j < n; j++) {
				if (_resolutions[j][0] == dm.dmPelsWidth && _resolutions[j][1] == dm.dmPelsHeight) break;
			}

			/* In the previous loop we have checked already existing/added resolutions if
			 * they are the same as the new ones. If this is not the case (j == n); we have
			 * looped all and found none, add the new one to the list. If we have reached the
			 * maximum amount of resolutions, then quit querying the display */
			if (j == n) {
				_resolutions[j][0] = dm.dmPelsWidth;
				_resolutions[j][1] = dm.dmPelsHeight;
				if (++n == lengthof(_resolutions)) break;
			}
		}
	}

	/* We have found no resolutions, show the default list */
	if (n == 0) {
		memcpy(_resolutions, default_resolutions, sizeof(default_resolutions));
		n = lengthof(default_resolutions);
	}

	_num_resolutions = n;
	SortResolutions(_num_resolutions);
}


static const char *Win32GdiStart(const char * const *parm)
{
	memset(&_wnd, 0, sizeof(_wnd));

	RegisterWndClass();

	MakePalette();

	FindResolutions();

	// fullscreen uses those
	_wnd.width_org = _cur_resolution[0];
	_wnd.height_org = _cur_resolution[1];

	AllocateDibSection(_cur_resolution[0], _cur_resolution[1]);
	MarkWholeScreenDirty();

	MakeWindow(_fullscreen);

	return NULL;
}

static void Win32GdiStop(void)
{
	if (_wnd.fullscreen) ChangeDisplaySettings(NULL, 0);
	if (_double_size) {
		_cur_resolution[0] *= 2;
		_cur_resolution[1] *= 2;
	}

	MyShowCursor(true);
	DeleteObject(_wnd.gdi_palette);
	DeleteObject(_wnd.dib_sect);
	DestroyWindow(_wnd.main_wnd);
}

// simple upscaler by 2
static void filter(int left, int top, int width, int height)
{
	uint p = _screen.pitch;
	const Pixel *s = _wnd.buffer_bits + top * p + left;
	Pixel *d = _wnd.bitmap_bits + top * p * 4 + left * 2;

	for (; height > 0; height--) {
		int i;

		for (i = 0; i != width; i++) {
			d[i * 2] = d[i * 2 + 1] = d[i * 2 + p * 2] = d[i * 2 + 1 + p * 2] = s[i];
		}
		s += p;
		d += p * 4;
	}
}

static void Win32GdiMakeDirty(int left, int top, int width, int height)
{
	RECT r = { left, top, left + width, top + height };

	if (_wnd.double_size) {
		filter(left, top, width, height);
		r.left *= 2;
		r.top *= 2;
		r.right *= 2;
		r.bottom *= 2;
	}
	InvalidateRect(_wnd.main_wnd, &r, FALSE);
}

static void CheckPaletteAnim(void)
{
	if (_pal_last_dirty == -1)
		return;
	InvalidateRect(_wnd.main_wnd, NULL, FALSE);
}

static void Win32GdiMainLoop(void)
{
	MSG mesg;
	uint32 next_tick = GetTickCount() + 30, cur_ticks;

	_wnd.running = true;

	while(true) {
		while (PeekMessage(&mesg, NULL, 0, 0, PM_REMOVE)) {
			InteractiveRandom(); // randomness
			TranslateMessage(&mesg);
			DispatchMessage(&mesg);
		}
		if (_exit_game) return;

#if defined(_DEBUG)
		if (_wnd.has_focus && GetAsyncKeyState(VK_SHIFT) < 0) {
			if (
#else
		if (_wnd.has_focus && GetAsyncKeyState(VK_TAB) < 0) {
			/* Disable speeding up game with ALT+TAB (if syskey is pressed, the
			 * real key is in the upper 16 bits (see WM_SYSKEYDOWN in WndProcGdi()) */
			if ((_pressed_key >> 16) & WKC_TAB &&
#endif
			    !_networking && _game_mode != GM_MENU)
				_fast_forward |= 2;
		} else if (_fast_forward & 2)
			_fast_forward = 0;

		cur_ticks = GetTickCount();
		if ((_fast_forward && !_pause) || cur_ticks > next_tick)
			next_tick = cur_ticks;

		if (cur_ticks == next_tick) {
			next_tick += 30;
			_ctrl_pressed = _wnd.has_focus && GetAsyncKeyState(VK_CONTROL)<0;
			_shift_pressed = _wnd.has_focus && GetAsyncKeyState(VK_SHIFT)<0;
#ifdef _DEBUG
			_dbg_screen_rect = _wnd.has_focus && GetAsyncKeyState(VK_CAPITAL)<0;
#endif

			// determine which directional keys are down
			if (_wnd.has_focus) {
				_dirkeys =
					(GetAsyncKeyState(VK_LEFT) < 0 ? 1 : 0) +
					(GetAsyncKeyState(VK_UP) < 0 ? 2 : 0) +
					(GetAsyncKeyState(VK_RIGHT) < 0 ? 4 : 0) +
					(GetAsyncKeyState(VK_DOWN) < 0 ? 8 : 0);
			} else
				_dirkeys = 0;

			GameLoop();
			_cursor.delta.x = _cursor.delta.y = 0;

			if (_force_full_redraw)
				MarkWholeScreenDirty();

			GdiFlush();
			_screen.dst_ptr = _wnd.buffer_bits;
			UpdateWindows();
			CheckPaletteAnim();
		} else {
			Sleep(1);
			GdiFlush();
			_screen.dst_ptr = _wnd.buffer_bits;
			DrawTextMessage();
			DrawMouseCursor();
		}
	}
}

static bool Win32GdiChangeRes(int w, int h)
{
	_wnd.width = _wnd.width_org = w;
	_wnd.height = _wnd.height_org = h;

	MakeWindow(_fullscreen); // _wnd.fullscreen screws up ingame resolution switching

	return true;
}

static void Win32GdiFullScreen(bool full_screen) {MakeWindow(full_screen);}

const HalVideoDriver _win32_video_driver = {
	Win32GdiStart,
	Win32GdiStop,
	Win32GdiMakeDirty,
	Win32GdiMainLoop,
	Win32GdiChangeRes,
	Win32GdiFullScreen,
};
