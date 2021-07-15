/* $Id: gfx.c 3298 2005-12-14 06:28:48Z tron $ */

#include "stdafx.h"
#include "openttd.h"
#include "functions.h"
#include "player.h"
#include "spritecache.h"
#include "strings.h"
#include "string.h"
#include "gfx.h"
#include "table/palettes.h"
#include "table/sprites.h"
#include "hal.h"
#include "variables.h"

#ifdef _DEBUG
bool _dbg_screen_rect;
#endif

Colour _cur_palette[256];

static void GfxMainBlitter(const Sprite *sprite, int x, int y, int mode);

static int _stringwidth_out;
static Pixel _cursor_backup[64 * 64];
//static Rect _invalid_rect;
static const byte *_color_remap_ptr;
static byte _string_colorremap[3];

#define DIRTY_BYTES_PER_LINE (MAX_SCREEN_WIDTH / 64)
static byte _dirty_blocks[DIRTY_BYTES_PER_LINE * MAX_SCREEN_HEIGHT / 8];



void memcpy_pitch(void *d, void *s, int w, int h, int spitch, int dpitch)
{
	byte *dp = (byte*)d;
	byte *sp = (byte*)s;

	assert(h >= 0);
	for (; h != 0; --h) {
		memcpy(dp, sp, w);
		dp += dpitch;
		sp += spitch;
	}
}


void GfxScroll(int left, int top, int width, int height, int xo, int yo)
{
	const Pixel *src;
	Pixel *dst;
	int p;
	int ht;

	if (xo == 0 && yo == 0) return;

	if (_cursor.visible) UndrawMouseCursor();
	UndrawTextMessage();

	p = _screen.pitch;

	if (yo > 0) {
		// Calculate pointers
		dst = _screen.dst_ptr + (top + height - 1) * p + left;
		src = dst - yo * p;

		// Decrease height and increase top
		top += yo;
		height -= yo;
		assert(height > 0);

		// Adjust left & width
		if (xo >= 0) {
			dst += xo;
			left += xo;
			width -= xo;
		} else {
			src -= xo;
			width += xo;
		}

		for (ht = height; ht > 0; --ht) {
			memcpy(dst, src, width);
			src -= p;
			dst -= p;
		}
	} else {
		// Calculate pointers
		dst = _screen.dst_ptr + top * p + left;
		src = dst - yo * p;

		// Decrese height. (yo is <=0).
		height += yo;
		assert(height > 0);

		// Adjust left & width
		if (xo >= 0) {
			dst += xo;
			left += xo;
			width -= xo;
		} else {
			src -= xo;
			width += xo;
		}

		// the y-displacement may be 0 therefore we have to use memmove,
		// because source and destination may overlap
		for (ht = height; ht > 0; --ht) {
			memmove(dst, src, width);
			src += p;
			dst += p;
		}
	}
	// This part of the screen is now dirty.
	_video_driver->make_dirty(left, top, width, height);
}


void GfxFillRect(int left, int top, int right, int bottom, int color)
{
	const DrawPixelInfo* dpi = _cur_dpi;
	Pixel *dst;
	const int otop = top;
	const int oleft = left;

	if (dpi->zoom != 0) return;
	if (left > right || top > bottom) return;
	if (right < dpi->left || left >= dpi->left + dpi->width) return;
	if (bottom < dpi->top || top >= dpi->top + dpi->height) return;

	if ( (left -= dpi->left) < 0) left = 0;
	right = right - dpi->left + 1;
	if (right > dpi->width) right = dpi->width;
	right -= left;
	assert(right > 0);

	if ( (top -= dpi->top) < 0) top = 0;
	bottom = bottom - dpi->top + 1;
	if (bottom > dpi->height) bottom = dpi->height;
	bottom -= top;
	assert(bottom > 0);

	dst = dpi->dst_ptr + top * dpi->pitch + left;

	if (!(color & PALETTE_MODIFIER_GREYOUT)) {
		if (!(color & USE_COLORTABLE)) {
			do {
				memset(dst, color, right);
				dst += dpi->pitch;
			} while (--bottom);
		} else {
			/* use colortable mode */
			const byte* ctab = GetNonSprite(color & COLORTABLE_MASK) + 1;

			do {
				int i;
				for (i = 0; i != right; i++) dst[i] = ctab[dst[i]];
				dst += dpi->pitch;
			} while (--bottom);
		}
	} else {
		byte bo = (oleft - left + dpi->left + otop - top + dpi->top) & 1;
		do {
			int i;
			for (i = (bo ^= 1); i < right; i += 2) dst[i] = (byte)color;
			dst += dpi->pitch;
		} while (--bottom > 0);
	}
}

static void GfxSetPixel(int x, int y, int color)
{
	const DrawPixelInfo* dpi = _cur_dpi;
	if ((x-=dpi->left) < 0 || x>=dpi->width || (y-=dpi->top)<0 || y>=dpi->height)
		return;
	dpi->dst_ptr[y * dpi->pitch + x] = color;
}

void GfxDrawLine(int x, int y, int x2, int y2, int color)
{
	int dy;
	int dx;
	int stepx;
	int stepy;
	int frac;

	// Check clipping first
	{
		DrawPixelInfo *dpi = _cur_dpi;
		int t;

		if (x < dpi->left && x2 < dpi->left) return;

		if (y < dpi->top && y2 < dpi->top) return;

		t = dpi->left + dpi->width;
		if (x > t && x2 > t) return;

		t = dpi->top + dpi->height;
		if (y > t && y2 > t) return;
	}

	dy = (y2 - y) * 2;
	if (dy < 0) {
		dy = -dy;
		stepy = -1;
	} else {
		stepy = 1;
	}

	dx = (x2 - x) * 2;
	if (dx < 0) {
		dx = -dx;
		stepx = -1;
	} else {
		stepx = 1;
	}

	GfxSetPixel(x, y, color);
	if (dx > dy) {
		frac = dy - (dx >> 1);
		while (x != x2) {
			if (frac >= 0) {
				y += stepy;
				frac -= dx;
			}
			x += stepx;
			frac += dy;
			GfxSetPixel(x, y, color);
		}
	} else {
		frac = dx - (dy >> 1);
		while (y != y2) {
			if (frac >= 0) {
				x += stepx;
				frac -= dy;
			}
			y += stepy;
			frac += dx;
			GfxSetPixel(x, y, color);
		}
	}
}

// ASSIGNMENT OF ASCII LETTERS < 32
// 0 - end of string
// 1 - SETX <BYTE>
// 2 - SETXY <BYTE> <BYTE>
// 3-7 -
// 8 - TINYFONT
// 9 - BIGFONT
// 10 - newline
// 11-14 -
// 15-31 - 17 colors


enum {
	ASCII_SETX = 1,
	ASCII_SETXY = 2,

	ASCII_TINYFONT = 8,
	ASCII_BIGFONT = 9,
	ASCII_NL = 10,

	ASCII_COLORSTART = 15,
};

/** Truncate a given string to a maximum width if neccessary.
 * If the string is truncated, add three dots ('...') to show this.
 * @param *dest string that is checked and possibly truncated
 * @param maxw maximum width in pixels of the string
 * @return new width of (truncated) string */
static int TruncateString(char *str, int maxw)
{
	int w = 0;
	int base = _stringwidth_base;
	int ddd, ddd_w;

	byte c;
	char *ddd_pos;

	base = _stringwidth_base;
	ddd_w = ddd = GetCharacterWidth(base + '.') * 3;

	for (ddd_pos = str; (c = *str++) != '\0'; ) {
		if (c >= ASCII_LETTERSTART) {
			w += GetCharacterWidth(base + c);

			if (w >= maxw) {
				// string got too big... insert dotdotdot
				ddd_pos[0] = ddd_pos[1] = ddd_pos[2] = '.';
				ddd_pos[3] = 0;
				return ddd_w;
			}
		} else {
			if (c == ASCII_SETX) str++;
			else if (c == ASCII_SETXY) str += 2;
			else if (c == ASCII_TINYFONT) {
				base = 224;
				ddd = GetCharacterWidth(base + '.') * 3;
			} else if (c == ASCII_BIGFONT) {
				base = 448;
				ddd = GetCharacterWidth(base + '.') * 3;
			}
		}

		// Remember the last position where three dots fit.
		if (w + ddd < maxw) {
			ddd_w = w + ddd;
			ddd_pos = str;
		}
	}

	return w;
}

static inline int TruncateStringID(StringID src, char *dest, int maxw)
{
	GetString(dest, src);
	return TruncateString(dest, maxw);
}

/* returns right coordinate */
int DrawString(int x, int y, StringID str, uint16 color)
{
	char buffer[512];

	GetString(buffer, str);
	return DoDrawString(buffer, x, y, color);
}

int DrawStringTruncated(int x, int y, StringID str, uint16 color, uint maxw)
{
	char buffer[512];
	TruncateStringID(str, buffer, maxw);
	return DoDrawString(buffer, x, y, color);
}


void DrawStringRightAligned(int x, int y, StringID str, uint16 color)
{
	char buffer[512];

	GetString(buffer, str);
	DoDrawString(buffer, x - GetStringWidth(buffer), y, color);
}

void DrawStringRightAlignedTruncated(int x, int y, StringID str, uint16 color, uint maxw)
{
	char buffer[512];

	TruncateStringID(str, buffer, maxw);
	DoDrawString(buffer, x - GetStringWidth(buffer), y, color);
}


int DrawStringCentered(int x, int y, StringID str, uint16 color)
{
	char buffer[512];
	int w;

	GetString(buffer, str);

	w = GetStringWidth(buffer);
	DoDrawString(buffer, x - w / 2, y, color);

	return w;
}

int DrawStringCenteredTruncated(int xl, int xr, int y, StringID str, uint16 color)
{
	char buffer[512];
	int w = TruncateStringID(str, buffer, xr - xl);
	return DoDrawString(buffer, (xl + xr - w) / 2, y, color);
}

int DoDrawStringCentered(int x, int y, const char *str, uint16 color)
{
	int w = GetStringWidth(str);
	DoDrawString(str, x - w / 2, y, color);
	return w;
}

void DrawStringCenterUnderline(int x, int y, StringID str, uint16 color)
{
	int w = DrawStringCentered(x, y, str, color);
	GfxFillRect(x - (w >> 1), y + 10, x - (w >> 1) + w, y + 10, _string_colorremap[1]);
}

void DrawStringCenterUnderlineTruncated(int xl, int xr, int y, StringID str, uint16 color)
{
	int w = DrawStringCenteredTruncated(xl, xr, y, str, color);
	GfxFillRect((xl + xr - w) / 2, y + 10, (xl + xr + w) / 2, y + 10, _string_colorremap[1]);
}

static uint32 FormatStringLinebreaks(char *str, int maxw)
{
	int num = 0;
	int base = _stringwidth_base;
	int w;
	char *last_space;
	byte c;

	for(;;) {
		w = 0;
		last_space = NULL;

		for(;;) {
			c = *str++;
			if (c == ASCII_LETTERSTART) last_space = str;

			if (c >= ASCII_LETTERSTART) {
				w += GetCharacterWidth(base + (byte)c);
				if (w > maxw) {
					str = last_space;
					if (str == NULL)
						return num + (base << 16);
					break;
				}
			} else {
				if (c == 0) return num + (base << 16);
				if (c == ASCII_NL) break;

				if (c == ASCII_SETX) str++;
				else if (c == ASCII_SETXY) str += 2;
				else if (c == ASCII_TINYFONT) base = 224;
				else if (c == ASCII_BIGFONT) base = 448;
			}
		}

		num++;
		str[-1] = '\0';
	}
}

void DrawStringMultiCenter(int x, int y, StringID str, int maxw)
{
	char buffer[512];
	uint32 tmp;
	int num, w, mt;
	const char *src;
	byte c;

	GetString(buffer, str);

	tmp = FormatStringLinebreaks(buffer, maxw);
	num = GB(tmp, 0, 16);

	switch (GB(tmp, 16, 16)) {
		case   0: mt = 10; break;
		case 244: mt =  6; break;
		default:  mt = 18; break;
	}

	y -= (mt >> 1) * num;

	src = buffer;

	for(;;) {
		w = GetStringWidth(src);
		DoDrawString(src, x - (w>>1), y, 0xFE);
		_stringwidth_base = _stringwidth_out;

		for(;;) {
			c = *src++;
			if (c == 0) {
				y += mt;
				if (--num < 0) {
					_stringwidth_base = 0;
					return;
				}
				break;
			} else if (c == ASCII_SETX) {
				src++;
			} else if (c == ASCII_SETXY) {
				src+=2;
			}
		}
	}
}

void DrawStringMultiLine(int x, int y, StringID str, int maxw)
{
	char buffer[512];
	uint32 tmp;
	int num, mt;
	const char *src;
	byte c;

	GetString(buffer, str);

	tmp = FormatStringLinebreaks(buffer, maxw);
	num = GB(tmp, 0, 16);

	switch (GB(tmp, 16, 16)) {
		case   0: mt = 10; break;
		case 244: mt =  6; break;
		default:  mt = 18; break;
	}

	src = buffer;

	for(;;) {
		DoDrawString(src, x, y, 0xFE);
		_stringwidth_base = _stringwidth_out;

		for(;;) {
			c = *src++;
			if (c == 0) {
				y += mt;
				if (--num < 0) {
					_stringwidth_base = 0;
					return;
				}
				break;
			} else if (c == ASCII_SETX) {
				src++;
			} else if (c == ASCII_SETXY) {
				src+=2;
			}
		}
	}
}

int GetStringWidth(const char *str)
{
	int w = 0;
	byte c;
	int base = _stringwidth_base;
	for (c = *str; c != '\0'; c = *(++str)) {
		if (c >= ASCII_LETTERSTART) {
			w += GetCharacterWidth(base + c);
		} else {
			if (c == ASCII_SETX) str++;
			else if (c == ASCII_SETXY) str += 2;
			else if (c == ASCII_TINYFONT) base = 224;
			else if (c == ASCII_BIGFONT) base = 448;
		}
	}
	return w;
}

void DrawFrameRect(int left, int top, int right, int bottom, int ctab, int flags)
{
	byte color_2 = _color_list[ctab].window_color_1a;
	byte color_interior = _color_list[ctab].window_color_bga;
	byte color_3 = _color_list[ctab].window_color_bgb;
	byte color = _color_list[ctab].window_color_2;

	if (!(flags & 0x8)) {
		if (!(flags & 0x20)) {
			GfxFillRect(left, top, left, bottom - 1, color);
			GfxFillRect(left + 1, top, right - 1, top, color);
			GfxFillRect(right, top, right, bottom - 1, color_2);
			GfxFillRect(left, bottom, right, bottom, color_2);
			if (!(flags & 0x10)) {
				GfxFillRect(left + 1, top + 1, right - 1, bottom - 1, color_interior);
			}
		} else {
			GfxFillRect(left, top, left, bottom, color_2);
			GfxFillRect(left + 1, top, right, top, color_2);
			GfxFillRect(right, top + 1, right, bottom - 1, color);
			GfxFillRect(left + 1, bottom, right, bottom, color);
			if (!(flags & 0x10)) {
				GfxFillRect(left + 1, top + 1, right - 1, bottom - 1,
					flags & 0x40 ? color_interior : color_3);
			}
		}
	} else if (flags & 0x1) {
		// transparency
		GfxFillRect(left, top, right, bottom, 0x322 | USE_COLORTABLE);
	} else {
		GfxFillRect(left, top, right, bottom, color_interior);
	}
}

int DoDrawString(const char *string, int x, int y, uint16 real_color)
{
	DrawPixelInfo *dpi = _cur_dpi;
	int base = _stringwidth_base;
	byte c;
	byte color;
	int xo = x, yo = y;

	color = real_color & 0xFF;

	if (color != 0xFE) {
		if (x >= dpi->left + dpi->width ||
				x + _screen.width*2 <= dpi->left ||
				y >= dpi->top + dpi->height ||
				y + _screen.height <= dpi->top)
					return x;

		if (color != 0xFF) {
switch_color:;
			if (real_color & IS_PALETTE_COLOR) {
				_string_colorremap[1] = color;
				_string_colorremap[2] = 215;
			} else {
				_string_colorremap[1] = _string_colormap[color].text;
				_string_colorremap[2] = _string_colormap[color].shadow;
			}
			_color_remap_ptr = _string_colorremap;
		}
	}

check_bounds:
	if (y + 19 <= dpi->top || dpi->top + dpi->height <= y) {
skip_char:;
		for(;;) {
			c = *string++;
			if (c < ASCII_LETTERSTART) goto skip_cont;
		}
	}

	for(;;) {
		c = *string++;
skip_cont:;
		if (c == 0) {
			_stringwidth_out = base;
			return x;
		}
		if (c >= ASCII_LETTERSTART) {
			if (x >= dpi->left + dpi->width) goto skip_char;
			if (x + 26 >= dpi->left) {
				GfxMainBlitter(GetSprite(base + 2 + c - ASCII_LETTERSTART), x, y, 1);
			}
			x += GetCharacterWidth(base + c);
		} else if (c == ASCII_NL) { // newline = {}
			x = xo;
			y += 10;
			if (base != 0) {
				y -= 4;
				if (base != 0xE0)
					y += 12;
			}
			goto check_bounds;
		} else if (c >= ASCII_COLORSTART) { // change color?
			color = (byte)(c - ASCII_COLORSTART);
			goto switch_color;
		} else if (c == ASCII_SETX) { // {SETX}
			x = xo + (byte)*string++;
		} else if (c == ASCII_SETXY) {// {SETXY}
			x = xo + (byte)*string++;
			y = yo + (byte)*string++;
		} else if (c == ASCII_TINYFONT) { // {TINYFONT}
			base = 0xE0;
		} else if (c == ASCII_BIGFONT) { // {BIGFONT}
			base = 0x1C0;
		} else {
			printf("Unknown string command character %d\n", c);
		}
	}
}

int DoDrawStringTruncated(const char *str, int x, int y, uint16 color, uint maxw)
{
	char buffer[512];
	ttd_strlcpy(buffer, str, sizeof(buffer));
	TruncateString(buffer, maxw);
	return DoDrawString(buffer, x, y, color);
}

void DrawSprite(uint32 img, int x, int y)
{
	if (img & PALETTE_MODIFIER_COLOR) {
		_color_remap_ptr = GetNonSprite(GB(img, PALETTE_SPRITE_START, PALETTE_SPRITE_WIDTH)) + 1;
		GfxMainBlitter(GetSprite(img & SPRITE_MASK), x, y, 1);
	} else if (img & PALETTE_MODIFIER_TRANSPARENT) {
		_color_remap_ptr = GetNonSprite(GB(img, PALETTE_SPRITE_START, PALETTE_SPRITE_WIDTH)) + 1;
		GfxMainBlitter(GetSprite(img & SPRITE_MASK), x, y, 2);
	} else {
		GfxMainBlitter(GetSprite(img & SPRITE_MASK), x, y, 0);
	}
}

typedef struct BlitterParams {
	int start_x, start_y;
	const byte* sprite;
	const byte* sprite_org;
	Pixel *dst;
	int mode;
	int width, height;
	int width_org;
	int height_org;
	int pitch;
	byte info;
} BlitterParams;

static void GfxBlitTileZoomIn(BlitterParams *bp)
{
	const byte* src_o = bp->sprite;
	const byte* src;
	int num, skip;
	byte done;
	Pixel *dst;
	const byte* ctab;

	if (bp->mode & 1) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);

		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if ( (skip -= bp->start_x) > 0) {
					dst += skip;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;

				for (; num >= 4; num -=4) {
					dst[3] = ctab[src[3]];
					dst[2] = ctab[src[2]];
					dst[1] = ctab[src[1]];
					dst[0] = ctab[src[0]];
					dst += 4;
					src += 4;
				}
				for (; num != 0; num--) *dst++ = ctab[*src++];
			} while (!(done & 0x80));

			bp->dst += bp->pitch;
		} while (--bp->height != 0);
	} else if (bp->mode & 2) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src_o += num + 2;

				dst = bp->dst;

				if ( (skip -= bp->start_x) > 0) {
					dst += skip;
				} else {
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;
				for (; num != 0; num--) {
					*dst = ctab[*dst];
					dst++;
				}
			} while (!(done & 0x80));

			bp->dst += bp->pitch;
		} while (--bp->height != 0);
	} else {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if ( (skip -= bp->start_x) > 0) {
					dst += skip;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}
#if defined(_WIN32)
				if (num & 1) *dst++ = *src++;
				if (num & 2) { *(uint16*)dst = *(uint16*)src; dst += 2; src += 2; }
				if (num >>= 2) {
					do {
						*(uint32*)dst = *(uint32*)src;
						dst += 4;
						src += 4;
					} while (--num != 0);
				}
#else
				memcpy(dst, src, num);
#endif
			} while (!(done & 0x80));

			bp->dst += bp->pitch;
		} while (--bp->height != 0);
	}
}

static void GfxBlitZoomInUncomp(BlitterParams *bp)
{
	const byte *src = bp->sprite;
	Pixel *dst = bp->dst;
	int height = bp->height;
	int width = bp->width;
	int i;

	assert(height > 0);
	assert(width > 0);

	if (bp->mode & 1) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			do {
				for (i = 0; i != width; i++) {
					byte b = ctab[src[i]];

					if (b != 0) dst[i] = b;
				}
				src += bp->width_org;
				dst += bp->pitch;
			} while (--height != 0);
		}
	} else if (bp->mode & 2) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			do {
				for (i = 0; i != width; i++)
					if (src[i] != 0) dst[i] = ctab[dst[i]];
				src += bp->width_org;
				dst += bp->pitch;
			} while (--height != 0);
		}
	} else {
		if (!(bp->info & 1)) {
			do {
				memcpy(dst, src, width);
				src += bp->width_org;
				dst += bp->pitch;
			} while (--height != 0);
		} else {
			do {
				int n = width;

				for (; n >= 4; n -= 4) {
					if (src[0] != 0) dst[0] = src[0];
					if (src[1] != 0) dst[1] = src[1];
					if (src[2] != 0) dst[2] = src[2];
					if (src[3] != 0) dst[3] = src[3];

					dst += 4;
					src += 4;
				}

				for (; n != 0; n--) {
					if (src[0] != 0) dst[0] = src[0];
					src++;
					dst++;
				}

				src += bp->width_org - width;
				dst += bp->pitch - width;
			} while (--height != 0);
		}
	}
}

static void GfxBlitTileZoomMedium(BlitterParams *bp)
{
	const byte* src_o = bp->sprite;
	const byte* src;
	int num, skip;
	byte done;
	Pixel *dst;
	const byte* ctab;

	if (bp->mode & 1) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					src++;
					if (--num == 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 1;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;
				num = (num + 1) >> 1;
				for (; num != 0; num--) {
						*dst = ctab[*src];
						dst++;
						src += 2;
				}
			} while (!(done & 0x80));
			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
		} while (--bp->height != 0);
	} else if (bp->mode & 2) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					if (--num == 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 1;
				} else {
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;
				num = (num + 1) >> 1;
				for (; num != 0; num--) {
						*dst = ctab[*dst];
						dst++;
				}
			} while (!(done & 0x80));
			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
		} while (--bp->height != 0);
	} else {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		do {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					src++;
					if (--num == 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 1;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				num = (num + 1) >> 1;

				for (; num != 0; num--) {
						*dst = *src;
						dst++;
						src += 2;
				}

			} while (!(done & 0x80));

			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
		} while (--bp->height != 0);
	}
}

static void GfxBlitZoomMediumUncomp(BlitterParams *bp)
{
	const byte *src = bp->sprite;
	Pixel *dst = bp->dst;
	int height = bp->height;
	int width = bp->width;
	int i;

	assert(height > 0);
	assert(width > 0);

	if (bp->mode & 1) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			for (height >>= 1; height != 0; height--) {
				for (i = 0; i != width >> 1; i++) {
					byte b = ctab[src[i * 2]];

					if (b != 0) dst[i] = b;
				}
				src += bp->width_org * 2;
				dst += bp->pitch;
			}
		}
	} else if (bp->mode & 2) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			for (height >>= 1; height != 0; height--) {
				for (i = 0; i != width >> 1; i++)
					if (src[i * 2] != 0) dst[i] = ctab[dst[i]];
				src += bp->width_org * 2;
				dst += bp->pitch;
			}
		}
	} else {
		if (bp->info & 1) {
			for (height >>= 1; height != 0; height--) {
				for (i = 0; i != width >> 1; i++)
					if (src[i * 2] != 0) dst[i] = src[i * 2];
				src += bp->width_org * 2;
				dst += bp->pitch;
			}
		}
	}
}

static void GfxBlitTileZoomOut(BlitterParams *bp)
{
	const byte* src_o = bp->sprite;
	const byte* src;
	int num, skip;
	byte done;
	Pixel *dst;
	const byte* ctab;

	if (bp->mode & 1) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		for(;;) {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					src++;
					if (--num == 0) continue;
				}

				if (skip & 2) {
					skip += 2;
					src += 2;
					num -= 2;
					if (num <= 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 2;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;
				num = (num + 3) >> 2;
				for (; num != 0; num--) {
						*dst = ctab[*src];
						dst++;
						src += 4;
				}
			} while (!(done & 0x80));
			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;
		}
	} else if (bp->mode & 2) {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		for(;;) {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					if (--num == 0) continue;
				}

				if (skip & 2) {
					skip += 2;
					num -= 2;
					if (num <= 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 2;
				} else {
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				ctab = _color_remap_ptr;
				num = (num + 3) >> 2;
				for (; num != 0; num--) {
						*dst = ctab[*dst];
						dst++;
				}

			} while (!(done & 0x80));
			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;
		}
	} else {
		src_o += READ_LE_UINT16(src_o + bp->start_y * 2);
		for(;;) {
			do {
				done = src_o[0];
				num = done & 0x7F;
				skip = src_o[1];
				src = src_o + 2;
				src_o += num + 2;

				dst = bp->dst;

				if (skip & 1) {
					skip++;
					src++;
					if (--num == 0) continue;
				}

				if (skip & 2) {
					skip += 2;
					src += 2;
					num -= 2;
					if (num <= 0) continue;
				}

				if ( (skip -= bp->start_x) > 0) {
					dst += skip >> 2;
				} else {
					src -= skip;
					num += skip;
					if (num <= 0) continue;
					skip = 0;
				}

				skip = skip + num - bp->width;
				if (skip > 0) {
					num -= skip;
					if (num <= 0) continue;
				}

				num = (num + 3) >> 2;

				for (; num != 0; num--) {
						*dst = *src;
						dst++;
						src += 4;
				}
			} while (!(done & 0x80));

			bp->dst += bp->pitch;
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;

			do {
				done = src_o[0];
				src_o += (done & 0x7F) + 2;
			} while (!(done & 0x80));
			if (--bp->height == 0) return;
		}
	}
}

static void GfxBlitZoomOutUncomp(BlitterParams *bp)
{
	const byte* src = bp->sprite;
	Pixel *dst = bp->dst;
	int height = bp->height;
	int width = bp->width;
	int i;

	assert(height > 0);
	assert(width > 0);

	if (bp->mode & 1) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			for (height >>= 2; height != 0; height--) {
				for (i = 0; i != width >> 2; i++) {
					byte b = ctab[src[i * 4]];

					if (b != 0) dst[i] = b;
				}
				src += bp->width_org * 4;
				dst += bp->pitch;
			}
		}
	} else if (bp->mode & 2) {
		if (bp->info & 1) {
			const byte *ctab = _color_remap_ptr;

			for (height >>= 2; height != 0; height--) {
				for (i = 0; i != width >> 2; i++)
					if (src[i * 4] != 0) dst[i] = ctab[dst[i]];
				src += bp->width_org * 4;
				dst += bp->pitch;
			}
		}
	} else {
		if (bp->info & 1) {
			for (height >>= 2; height != 0; height--) {
				for (i = 0; i != width >> 2; i++)
					if (src[i * 4] != 0) dst[i] = src[i * 4];
				src += bp->width_org * 4;
				dst += bp->pitch;
			}
		}
	}
}

typedef void (*BlitZoomFunc)(BlitterParams *bp);

static void GfxMainBlitter(const Sprite* sprite, int x, int y, int mode)
{
	const DrawPixelInfo* dpi = _cur_dpi;
	int start_x, start_y;
	byte info;
	BlitterParams bp;
	int zoom_mask = ~((1 << dpi->zoom) - 1);

	static const BlitZoomFunc zf_tile[3] =
	{
		GfxBlitTileZoomIn,
		GfxBlitTileZoomMedium,
		GfxBlitTileZoomOut
	};
	static const BlitZoomFunc zf_uncomp[3] =
	{
		GfxBlitZoomInUncomp,
		GfxBlitZoomMediumUncomp,
		GfxBlitZoomOutUncomp
	};

	/* decode sprite header */
	x += sprite->x_offs;
	y += sprite->y_offs;
	bp.width_org = bp.width = sprite->width;
	bp.height_org = bp.height = sprite->height;
	info = sprite->info;
	bp.info = info;
	bp.sprite_org = bp.sprite = sprite->data;
	bp.dst = dpi->dst_ptr;
	bp.mode = mode;
	bp.pitch = dpi->pitch;

	assert(bp.height > 0);
	assert(bp.width > 0);

	if (info & 8) {
		/* tile blit */
		start_y = 0;

		if (dpi->zoom > 0) {
			start_y += bp.height & ~zoom_mask;
			bp.height &= zoom_mask;
			if (bp.height == 0) return;
			y &= zoom_mask;
		}

		if ( (y -= dpi->top) < 0) {
			bp.height += y;
			if (bp.height <= 0) return;
			start_y -= y;
			y = 0;
		} else {
			bp.dst += bp.pitch * (y >> dpi->zoom);
		}
		bp.start_y = start_y;

		if ( (y = y + bp.height - dpi->height) > 0) {
			bp.height -= y;
			if (bp.height <= 0) return;
		}

		start_x = 0;
		x &= zoom_mask;
		if ( (x -= dpi->left) < 0) {
			bp.width += x;
			if (bp.width <= 0) return;
			start_x -= x;
			x = 0;
		}
		bp.start_x = start_x;
		bp.dst += x >> dpi->zoom;

		if ( (x = x + bp.width - dpi->width) > 0) {
			bp.width -= x;
			if (bp.width <= 0) return;
		}

		zf_tile[dpi->zoom](&bp);
	} else {
		bp.sprite += bp.width * (bp.height & ~zoom_mask);
		bp.height &= zoom_mask;
		if (bp.height == 0) return;

		y &= zoom_mask;

		if ( (y -= dpi->top) < 0) {
			bp.height += y;
			if (bp.height <= 0) return;
			bp.sprite -= bp.width * y;
			y = 0;
		} else {
			bp.dst += bp.pitch * (y >> dpi->zoom);
		}

		if (bp.height > dpi->height - y) {
			bp.height = dpi->height - y;
			if (bp.height <= 0) return;
		}

		x &= zoom_mask;

		if ( (x -= dpi->left) < 0) {
			bp.width += x;
			if (bp.width <= 0) return;
			bp.sprite -= x;
			x = 0;
		}
		bp.dst += x >> dpi->zoom;

		if (bp.width > dpi->width - x) {
			bp.width = dpi->width - x;
			if (bp.width <= 0) return;
		}

		zf_uncomp[dpi->zoom](&bp);
	}
}

void DoPaletteAnimations(void);

void GfxInitPalettes(void)
{
	memcpy(_cur_palette, _palettes[_use_dos_palette ? 1 : 0], sizeof(_cur_palette));

	_pal_first_dirty = 0;
	_pal_last_dirty = 255;
	DoPaletteAnimations();
}

#define EXTR(p, q) (((uint16)(_timer_counter * (p)) * (q)) >> 16)
#define EXTR2(p, q) (((uint16)(~_timer_counter * (p)) * (q)) >> 16)

void DoPaletteAnimations(void)
{
	const Colour* s;
	Colour* d;
	/* Amount of colors to be rotated.
	 * A few more for the DOS palette, because the water colors are
	 * 245-254 for DOS and 217-226 for Windows.  */
	const ExtraPaletteValues *ev = &_extra_palette_values;
	int c = _use_dos_palette ? 38 : 28;
	Colour old_val[38]; // max(38, 28)
	uint i;
	uint j;

	d = &_cur_palette[217];
	memcpy(old_val, d, c * sizeof(*old_val));

	// Dark blue water
	s = (_opt.landscape == LT_CANDY) ? ev->ac : ev->a;
	j = EXTR(320, 5);
	for (i = 0; i != 5; i++) {
		*d++ = s[j];
		j++;
		if (j == 5) j = 0;
	}

	// Glittery water
	s = (_opt.landscape == LT_CANDY) ? ev->bc : ev->b;
	j = EXTR(128, 15);
	for (i = 0; i != 5; i++) {
		*d++ = s[j];
		j += 3;
		if (j >= 15) j -= 15;
	}

	s = ev->e;
	j = EXTR2(512, 5);
	for (i = 0; i != 5; i++) {
		*d++ = s[j];
		j++;
		if (j == 5) j = 0;
	}

	// Oil refinery fire animation
	s = ev->oil_ref;
	j = EXTR2(512, 7);
	for (i = 0; i != 7; i++) {
		*d++ = s[j];
		j++;
		if (j == 7) j = 0;
	}

	// Radio tower blinking
	{
		byte i = (_timer_counter >> 1) & 0x7F;
		byte v;

		(v = 255, i < 0x3f) ||
		(v = 128, i < 0x4A || i >= 0x75) ||
		(v = 20);
		d->r = v;
		d->g = 0;
		d->b = 0;
		d++;

		i ^= 0x40;
		(v = 255, i < 0x3f) ||
		(v = 128, i < 0x4A || i >= 0x75) ||
		(v = 20);
		d->r = v;
		d->g = 0;
		d->b = 0;
		d++;
	}

	// Handle lighthouse and stadium animation
	s = ev->lighthouse;
	j = EXTR(256, 4);
	for (i = 0; i != 4; i++) {
		*d++ = s[j];
		j++;
		if (j == 4) j = 0;
	}

	// Animate water for old DOS graphics
	if (_use_dos_palette) {
		// Dark blue water DOS
		s = (_opt.landscape == LT_CANDY) ? ev->ac : ev->a;
		j = EXTR(320, 5);
		for (i = 0; i != 5; i++) {
			*d++ = s[j];
			j++;
			if (j == 5) j = 0;
		}

		// Glittery water DOS
		s = (_opt.landscape == LT_CANDY) ? ev->bc : ev->b;
		j = EXTR(128, 15);
		for (i = 0; i != 5; i++) {
			*d++ = s[j];
			j += 3;
			if (j >= 15) j -= 15;
		}
	}

	if (memcmp(old_val, &_cur_palette[217], c * sizeof(*old_val)) != 0) {
		if (_pal_first_dirty > 217) _pal_first_dirty = 217;
		if (_pal_last_dirty < 217 + c) _pal_last_dirty = 217 + c;
	}
}


void LoadStringWidthTable(void)
{
	byte *b = _stringwidth_table;
	uint i;

	// 2 equals space.
	for (i = 2; i != 226; i++) {
		*b++ = i != 97 && (i < 99 || i > 113) && i != 116 && i != 117 && (i < 123 || i > 129) && (i < 151 || i > 153) && i != 155 ? GetSprite(i)->width : 0;
	}

	for (i = 226; i != 450; i++) {
		*b++ = i != 321 && (i < 323 || i > 353) && i != 367 && (i < 375 || i > 377) && i != 379 ? GetSprite(i)->width + 1 : 0;
	}

	for (i = 450; i != 674; i++) {
		*b++ = (i < 545 || i > 577) && i != 585 && i != 587 && i != 588 && (i < 590 || i > 597) && (i < 599 || i > 601) && i != 603 && i != 633 && i != 665 ? GetSprite(i)->width + 1 : 0;
	}
}

void ScreenSizeChanged(void)
{
	// check the dirty rect
	if (_invalid_rect.right >= _screen.width) _invalid_rect.right = _screen.width;
	if (_invalid_rect.bottom >= _screen.height) _invalid_rect.bottom = _screen.height;

	// screen size changed and the old bitmap is invalid now, so we don't want to undraw it
	_cursor.visible = false;
}

void UndrawMouseCursor(void)
{
	if (_cursor.visible) {
		_cursor.visible = false;
		memcpy_pitch(
			_screen.dst_ptr + _cursor.draw_pos.x + _cursor.draw_pos.y * _screen.pitch,
			_cursor_backup,
			_cursor.draw_size.x, _cursor.draw_size.y, _cursor.draw_size.x, _screen.pitch);

		_video_driver->make_dirty(_cursor.draw_pos.x, _cursor.draw_pos.y, _cursor.draw_size.x, _cursor.draw_size.y);
	}
}

void DrawMouseCursor(void)
{
	int x;
	int y;
	int w;
	int h;

	// Don't draw the mouse cursor if it's already drawn
	if (_cursor.visible) {
		if (!_cursor.dirty) return;
		UndrawMouseCursor();
	}

	w = _cursor.size.x;
	x = _cursor.pos.x + _cursor.offs.x;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (w > _screen.width - x) w = _screen.width - x;
	if (w <= 0) return;
	_cursor.draw_pos.x = x;
	_cursor.draw_size.x = w;

	h = _cursor.size.y;
	y = _cursor.pos.y + _cursor.offs.y;
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (h > _screen.height - y) h = _screen.height - y;
	if (h <= 0) return;
	_cursor.draw_pos.y = y;
	_cursor.draw_size.y = h;

	assert(w * h < (int)sizeof(_cursor_backup));

	// Make backup of stuff below cursor
	memcpy_pitch(
		_cursor_backup,
		_screen.dst_ptr + _cursor.draw_pos.x + _cursor.draw_pos.y * _screen.pitch,
		_cursor.draw_size.x, _cursor.draw_size.y, _screen.pitch, _cursor.draw_size.x);

	// Draw cursor on screen
	_cur_dpi = &_screen;
	DrawSprite(_cursor.sprite, _cursor.pos.x, _cursor.pos.y);

	_video_driver->make_dirty(_cursor.draw_pos.x, _cursor.draw_pos.y, _cursor.draw_size.x, _cursor.draw_size.y);

	_cursor.visible = true;
	_cursor.dirty = false;
}

#if defined(_DEBUG)
static void DbgScreenRect(int left, int top, int right, int bottom)
{
	DrawPixelInfo dp;
	DrawPixelInfo* old;

	old = _cur_dpi;
	_cur_dpi = &dp;
	dp = _screen;
	GfxFillRect(left, top, right - 1, bottom - 1, rand() & 255);
	_cur_dpi = old;
}
#endif

void RedrawScreenRect(int left, int top, int right, int bottom)
{
	assert(right <= _screen.width && bottom <= _screen.height);
	if (_cursor.visible) {
		if (right > _cursor.draw_pos.x &&
				left < _cursor.draw_pos.x + _cursor.draw_size.x &&
				bottom > _cursor.draw_pos.y &&
				top < _cursor.draw_pos.y + _cursor.draw_size.y) {
			UndrawMouseCursor();
		}
	}
	UndrawTextMessage();

#if defined(_DEBUG)
	if (_dbg_screen_rect)
		DbgScreenRect(left, top, right, bottom);
	else
#endif
		DrawOverlappedWindowForAll(left, top, right, bottom);

	_video_driver->make_dirty(left, top, right - left, bottom - top);
}

void DrawDirtyBlocks(void)
{
	byte *b = _dirty_blocks;
	const int w = ALIGN(_screen.width, 64);
	const int h = ALIGN(_screen.height, 8);
	int x;
	int y;

	y = 0;
	do {
		x = 0;
		do {
			if (*b != 0) {
				int left;
				int top;
				int right = x + 64;
				int bottom = y;
				byte *p = b;
				int h2;

				// First try coalescing downwards
				do {
					*p = 0;
					p += DIRTY_BYTES_PER_LINE;
					bottom += 8;
				} while (bottom != h && *p != 0);

				// Try coalescing to the right too.
				h2 = (bottom - y) >> 3;
				assert(h2 > 0);
				p = b;

				while (right != w) {
					byte *p2 = ++p;
					int h = h2;
					// Check if a full line of dirty flags is set.
					do {
						if (!*p2) goto no_more_coalesc;
						p2 += DIRTY_BYTES_PER_LINE;
					} while (--h != 0);

					// Wohoo, can combine it one step to the right!
					// Do that, and clear the bits.
					right += 64;

					h = h2;
					p2 = p;
					do {
						*p2 = 0;
						p2 += DIRTY_BYTES_PER_LINE;
					} while (--h != 0);
				}
				no_more_coalesc:

				left = x;
				top = y;

				if (left   < _invalid_rect.left  ) left   = _invalid_rect.left;
				if (top    < _invalid_rect.top   ) top    = _invalid_rect.top;
				if (right  > _invalid_rect.right ) right  = _invalid_rect.right;
				if (bottom > _invalid_rect.bottom) bottom = _invalid_rect.bottom;

				if (left < right && top < bottom) {
					RedrawScreenRect(left, top, right, bottom);
				}

			}
		} while (b++, (x += 64) != w);
	} while (b += -(w >> 6) + DIRTY_BYTES_PER_LINE, (y += 8) != h);

	_invalid_rect.left = w;
	_invalid_rect.top = h;
	_invalid_rect.right = 0;
	_invalid_rect.bottom = 0;
}

/*
void SetDirtyBlocks(int left, int top, int right, int bottom)
{
	byte *b;
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

		do b[--i] = 0xFF; while (i);

		b += DIRTY_BYTES_PER_LINE;
	} while (--height != 0);
}

void MarkWholeScreenDirty(void)
{
	SetDirtyBlocks(0, 0, _screen.width, _screen.height);
}

bool FillDrawPixelInfo(DrawPixelInfo* n, const DrawPixelInfo* o, int left, int top, int width, int height)
{
	int t;

	if (o == NULL) o = _cur_dpi;

	n->zoom = 0;

	assert(width > 0);
	assert(height > 0);

	n->left = 0;
	if ((left -= o->left) < 0) {
		width += left;
		if (width < 0) return false;
		n->left = -left;
		left = 0;
	}

	if ((t=width + left - o->width) > 0) {
		width -= t;
		if (width < 0) return false;
	}
	n->width = width;

	n->top = 0;
	if ((top -= o->top) < 0) {
		height += top;
		if (height < 0) return false;
		n->top = -top;
		top = 0;
	}

	n->dst_ptr = o->dst_ptr + left + top * (n->pitch = o->pitch);

	if ((t=height + top - o->height) > 0) {
		height -= t;
		if (height < 0) return false;
	}
	n->height = height;

	return true;
}

static void SetCursorSprite(CursorID cursor)
{
	CursorVars *cv = &_cursor;
	const Sprite *p;

	if (cv->sprite == cursor) return;

	p = GetSprite(cursor & SPRITE_MASK);
	cv->sprite = cursor;
	cv->size.y = p->height;
	cv->size.x = p->width;
	cv->offs.x = p->x_offs;
	cv->offs.y = p->y_offs;

	cv->dirty = true;
}

static void SwitchAnimatedCursor(void)
{
	CursorVars *cv = &_cursor;
	const CursorID *cur = cv->animate_cur;
	CursorID sprite;

	// ANIM_CURSOR_END is 0xFFFF in table/animcursors.h
	if (cur == NULL || *cur == 0xFFFF) cur = cv->animate_list;

	sprite = cur[0];
	cv->animate_timeout = cur[1];
	cv->animate_cur = cur + 2;

	SetCursorSprite(sprite);
}

void CursorTick(void)
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

void SetAnimatedMouseCursor(const CursorID *table)
{
	_cursor.animate_list = table;
	_cursor.animate_cur = NULL;
	SwitchAnimatedCursor();
}

bool ChangeResInGame(int w, int h)
{
	return
		(_screen.width == w && _screen.height == h) ||
		_video_driver->change_resolution(w, h);
}

void ToggleFullScreen(bool fs) {_video_driver->toggle_fullscreen(fs);}

static int CDECL compare_res(const void *pa, const void *pb)
{
	int x = ((const uint16*)pa)[0] - ((const uint16*)pb)[0];
	if (x != 0) return x;
	return ((const uint16*)pa)[1] - ((const uint16*)pb)[1];
}

void SortResolutions(int count)
{
	qsort(_resolutions, count, sizeof(_resolutions[0]), compare_res);
}

uint16 GetDrawStringPlayerColor(PlayerID player)
{
	// Get the color for DrawString-subroutines which matches the color
	//  of the player
	if (player == OWNER_SPECTATOR || player == OWNER_SPECTATOR - 1) return 1;
	return (_color_list[_player_colors[player]].window_color_1b) | IS_PALETTE_COLOR;
}
*/