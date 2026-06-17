/*
 *  ttfont.cc - FreeType-based TrueType font rendering.
 *
 *  Copyright (C) 2024  The Exult Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "ttfont.h"

#include "U7obj.h"
#include "ibuf8.h"
#include "ignore_unused_variable_warning.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

/*
 *  Decode a single UTF-8 codepoint and advance the pointer.
 *  Returns the Unicode codepoint (or '?' for invalid sequences).
 */
static unsigned long utf8_decode(const char*& s, const char* end = nullptr) {
	unsigned char c = static_cast<unsigned char>(*s);
	if (c < 0x80) {                              // 1-byte sequence.
		s += 1;
		return c;
	} else if ((c & 0xE0) == 0xC0) {             // 2-byte sequence.
		if (end && s + 2 > end) { s += 1; return '?'; }
		unsigned long cp = (c & 0x1F) << 6;
		cp |= static_cast<unsigned char>(*(s + 1)) & 0x3F;
		s += 2;
		return cp;
	} else if ((c & 0xF0) == 0xE0) {             // 3-byte sequence (CJK etc.).
		if (end && s + 3 > end) { s += 1; return '?'; }
		unsigned long cp = (c & 0x0F) << 12;
		cp |= (static_cast<unsigned char>(*(s + 1)) & 0x3F) << 6;
		cp |= static_cast<unsigned char>(*(s + 2)) & 0x3F;
		s += 3;
		return cp;
	} else {                                     // Invalid start byte.
		s += 1;
		return '?';
	}
}
#ifdef HAVE_FREETYPE2
/*
 *  Blit a FreeType glyph bitmap onto an 8-bit image buffer at the given
 *  destination.  Every non-zero coverage pixel becomes 'color'.
 */
static void blit_glyph_bitmap(
		Image_buffer8* win,
		FT_Bitmap*     bitmap,
		int            dest_x,
		int            dest_y,
		unsigned char  color) {
	const unsigned char* src = bitmap->buffer;
	for (unsigned int row = 0; row < bitmap->rows; row++) {
		for (unsigned int col = 0; col < bitmap->width; col++) {
			if (src[col] > 0) {
				win->put_pixel8(color,
					dest_x + static_cast<int>(col),
					dest_y + static_cast<int>(row));
			}
		}
		src += bitmap->pitch;
	}
}
#endif   /* HAVE_FREETYPE2 */

/*
 *  Load from File_spec (delegates to path-based overload).
 */
int TtFont::load(const File_spec& fname0, int pixel_ht, int hlead, int vlead) {
	return load(fname0.name.c_str(), pixel_ht, hlead, vlead);
}

/*
 *  Load from file path.
 */
int TtFont::load(const char* font_path, int pixel_ht, int hlead, int vlead) {
#ifdef HAVE_FREETYPE2
	cleanup();
	int error = FT_Init_FreeType(&ft_library);
	if (error) {
		return -1;
	}
	error = FT_New_Face(ft_library, font_path, 0, &ft_face);
	if (error) {
		FT_Done_FreeType(ft_library);
		ft_library = nullptr;
		return -1;
	}
	error = FT_Set_Pixel_Sizes(ft_face, 0, pixel_ht);
	if (error) {
		FT_Done_Face(ft_face);
		ft_face = nullptr;
		FT_Done_FreeType(ft_library);
		ft_library = nullptr;
		return -1;
	}
	pixel_height = pixel_ht;
	hor_lead     = hlead;
	ver_lead     = vlead;
	font_file    = font_path;
	calc_highlow();
	return 0;
#else
	ignore_unused_variable_warning(font_path, pixel_ht, hlead, vlead);
	return -1;
#endif
}

/*
 *  Destructor.
 */
TtFont::~TtFont() noexcept {
	cleanup();
}

/*
 *  Release FreeType resources and reset state.
 */
void TtFont::cleanup() {
#ifdef HAVE_FREETYPE2
	if (ft_face) {
		FT_Done_Face(ft_face);
		ft_face = nullptr;
	}
	if (ft_library) {
		FT_Done_FreeType(ft_library);
		ft_library = nullptr;
	}
#endif
	pixel_height = 0;
	hor_lead     = 0;
	ver_lead     = 0;
	highest      = 0;
	lowest       = 0;
	font_file.clear();
}

/*
 *  Calculate the highest (above baseline) and lowest (below baseline)
 *  pixel extents from the FreeType face size metrics.
 */
void TtFont::calc_highlow() {
#ifdef HAVE_FREETYPE2
	if (ft_face && ft_face->size) {
		highest = ft_face->size->metrics.ascender >> 6;
		lowest  = (-ft_face->size->metrics.descender) >> 6;
	} else {
		highest = 0;
		lowest  = 0;
	}
#else
	highest = 0;
	lowest  = 0;
#endif
}

/*
 *  Draw null-terminated text at a given location.
 */
int TtFont::paint_text(
		Image_buffer8* win,
		const char*    text,
		int            xoff,
		int            yoff,
		unsigned char  fg_color,
		int            sh_color,
		unsigned char* trans) {
	return paint_text(win, text, -1, xoff, yoff, fg_color, sh_color, trans);
}

/*
 *  Draw text with an explicit byte length.
 */
int TtFont::paint_text(
		Image_buffer8* win,
		const char*    text,
		int            textlen,
		int            xoff,
		int            yoff,
		unsigned char  fg_color,
		int            sh_color,
		unsigned char* trans) {
#ifdef HAVE_FREETYPE2
	if (!ft_face || !text) {
		return 0;
	}
	int         x          = xoff;
	int         y          = yoff + get_text_baseline();
	const char* s          = text;
	const char* text_start = text;
	const char* text_end   = (textlen >= 0) ? text + textlen : nullptr;

	while (*s) {
		if (textlen >= 0 && s - text_start >= textlen) {
			break;
		}
		unsigned long ch          = utf8_decode(s, text_end);
		FT_UInt       glyph_index = FT_Get_Char_Index(ft_face, ch);
		if (glyph_index == 0) {
			glyph_index = FT_Get_Char_Index(ft_face, '?');
			if (glyph_index == 0) {
				x += hor_lead;
				continue;
			}
		}
		FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

		FT_Bitmap* bitmap    = &ft_face->glyph->bitmap;
		int        glyph_x   = x + ft_face->glyph->bitmap_left;
		int        glyph_y   = y - ft_face->glyph->bitmap_top;

		// Draw shadow first (offset by 1,1).
		if (sh_color >= 0) {
			unsigned char shadow_pixel = trans
				? trans[sh_color]
				: static_cast<unsigned char>(sh_color);
			blit_glyph_bitmap(win, bitmap, glyph_x + 1, glyph_y + 1, shadow_pixel);
		}

		// Draw foreground.
		unsigned char fg_pixel = trans ? trans[fg_color] : fg_color;
		blit_glyph_bitmap(win, bitmap, glyph_x, glyph_y, fg_pixel);

		x += (ft_face->glyph->advance.x >> 6) + hor_lead;
	}
	return x - xoff;
#else
	ignore_unused_variable_warning(win, text, textlen, xoff, yoff,
									fg_color, sh_color, trans);
	return 0;
#endif
}

/*
 *  Draw text within a rectangular area with word wrapping.
 *
 *  Output: If out of room, -offset of end of text painted.
 *          Else height of text painted.
 */
int TtFont::paint_text_box(
		Image_buffer8* win,                // Buffer to paint in.
		const char*    text,               // UTF-8 text.
		int            x, int y,           // Top-left corner of box.
		int            w, int h,           // Box dimensions.
		int            vert_lead,          // Extra spacing between lines.
		bool           pbreak,             // End at punctuation.
		bool           center,             // Center each line.
		Cursor_info*   cursor,             // Output cursor position.
		unsigned char  fg_color,
		int            sh_color,
		unsigned char* trans) {
	const char* start    = text;
	auto        clipsave = win->SaveClip();
	auto        newclip  = clipsave.Rect().intersect(TileRect(x, y, w, h));
	win->set_clip(newclip.x, newclip.y, newclip.w, newclip.h);

	const int   endx           = x + w;
	const int   line_height    = get_text_height() + vert_lead + ver_lead;
	const int   space_width    = get_text_width(" ", 1);
	const int   max_lines      = h / line_height;
	int         curx           = x;
	int         cur_line       = 0;
	int         cury           = y;
	const char* last_punct_end = nullptr;
	int         last_punct_line   = -1;
	int         last_punct_offset = -1;

	// Cursor tracking.
	int coff = -1;
	if (cursor) {
		coff      = cursor->offset;
		cursor->x = -1;
	}

	// Build lines with word wrapping.
	std::vector<std::string> lines;
	std::string current_line;

	while (*text) {
		if (cursor && text - start == coff) {
			cursor->set_found(curx, cury, cur_line);
		}

		// Handle newlines.
		if (*text == '\n') {
			lines.push_back(current_line);
			current_line.clear();
			cur_line++;
			curx = x;
			cury += line_height;
			text++;
			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Handle spaces/tabs.
		if (*text == ' ' || *text == '\t') {
			const char* sp = text;
			while (*text == ' ' || *text == '\t') {
				text++;
			}
			int nsp = static_cast<int>(text - sp);
			current_line.append(sp, nsp);
			curx += nsp * space_width;

			if (cursor && coff > static_cast<int>(sp - start)
					&& coff < static_cast<int>(text - start)) {
				int scx = curx
					- (static_cast<int>(text - start) - coff) * space_width;
				cursor->set_found(scx, cury, cur_line);
			}
			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Pass word.
		const char* ewrd = text;
		while (*text && *text != ' ' && *text != '\t' && *text != '\n') {
			text++;
		}
		if (text == ewrd) {
			continue;
		}
		int word_width = get_text_width(ewrd,
							static_cast<int>(text - ewrd));

		// Word-wrap?
		if (curx + word_width - hor_lead > endx && curx > x) {
			lines.push_back(current_line);
			current_line.clear();
			curx = x;
			cur_line++;
			cury += line_height;
			if (cur_line >= max_lines) {
				break;
			}
		}

		// Cursor in word?
		if (cursor && coff >= static_cast<int>(ewrd - start)
				&& coff < static_cast<int>(text - start)) {
			int woff = find_xcursor(ewrd,
							static_cast<int>(text - ewrd),
							coff - static_cast<int>(ewrd - start));
			if (woff < 0) {
				woff = static_cast<int>(text - ewrd);
			}
			int cx = curx + get_text_width(ewrd, woff);
			cursor->set_found(cx, cury, cur_line);
		}

		// Store word.
		current_line.append(ewrd, text - ewrd);
		curx += word_width;

		// Keep loc. of punct. endings.
		if (text[-1] == '.' || text[-1] == '?' || text[-1] == '!'
				|| text[-1] == ',' || text[-1] == '"') {
			last_punct_end    = text;
			last_punct_line   = cur_line;
			last_punct_offset = static_cast<int>(current_line.length());
		}
	}

	// End of loop.
	if (cur_line < max_lines) {
		lines.push_back(current_line);
	}

	// Text overflow handling.
	if (*text && pbreak && last_punct_end) {
		text = last_punct_end;
		while (*text == ' ' || *text == '\t' || *text == '\n') {
			text++;
		}
	} else if (*text) {
		last_punct_line = -1;
		if (cursor && text - start == coff && cur_line < max_lines) {
			cursor->set_found(curx, cury, cur_line);
		}
	}

	if (cursor) {
		cursor->nlines = cur_line + (cur_line < max_lines ? 1 : 0);
	}

	// Render lines.
	cury = y;
	for (int i = 0; i <= cur_line && i < static_cast<int>(lines.size()); i++) {
		const char* str = lines[i].c_str();
		int len = static_cast<int>(lines[i].length());
		if (i == last_punct_line && last_punct_offset >= 0) {
			len = last_punct_offset;
		}
		int line_x = x;
		if (center) {
			line_x = x + w / 2 - get_text_width(str) / 2;
		}
		paint_text(win, str, len, line_x, cury, fg_color, sh_color, trans);
		cury += line_height;
		if (i == last_punct_line) {
			break;
		}
	}

	if (*text) {
		return -static_cast<int>(text - start);
	}
	return cury - y;
}

/*
 *  Get the width in pixels of a null-terminated UTF-8 string.
 */
int TtFont::get_text_width(const char* text) {
	return get_text_width(text, -1);
}

/*
 *  Get the width in pixels of a UTF-8 string with explicit byte length.
 */
int TtFont::get_text_width(const char* text, int textlen) {
#ifdef HAVE_FREETYPE2
	if (!ft_face || !text) {
		return 0;
	}
	int         width       = 0;
	const char* s           = text;
	const char* text_start  = text;
	const char* text_end    = (textlen >= 0) ? text + textlen : nullptr;

	while (*s) {
		if (textlen >= 0 && s - text_start >= textlen) {
			break;
		}
		unsigned long ch          = utf8_decode(s, text_end);
		FT_UInt       glyph_index = FT_Get_Char_Index(ft_face, ch);
		if (glyph_index == 0) {
			glyph_index = FT_Get_Char_Index(ft_face, '?');
			if (glyph_index == 0) {
				width += hor_lead;
				continue;
			}
		}
		FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
		width += (ft_face->glyph->advance.x >> 6) + hor_lead;
	}
	return width;
#else
	ignore_unused_variable_warning(text, textlen);
	return 0;
#endif
}

/*
 *  Compute pixel dimensions of multi-line text.
 */
void TtFont::get_text_box_dims(
		const char* text,
		int&        width,
		int&        height,
		int         vert_lead) {
#ifdef HAVE_FREETYPE2
	width       = 0;
	height      = 0;
	if (!ft_face || !text) {
		return;
	}
	int         cur_width = 0;
	int         num_lines = 1;
	const char* s         = text;

	while (*s) {
		unsigned long ch = utf8_decode(s);
		if (ch == '\n') {
			num_lines++;
			width     = std::max(width, cur_width);
			cur_width = 0;
		} else {
			FT_UInt glyph_index = FT_Get_Char_Index(ft_face, ch);
			if (glyph_index == 0) {
				glyph_index = FT_Get_Char_Index(ft_face, '?');
			}
			if (glyph_index != 0) {
				FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
				cur_width += (ft_face->glyph->advance.x >> 6) + hor_lead;
			} else {
				cur_width += hor_lead;
			}
		}
	}
	width  = std::max(width, cur_width);
	height = num_lines * (get_text_height() + vert_lead + ver_lead);
#else
	ignore_unused_variable_warning(text, vert_lead);
	width  = 0;
	height = 0;
#endif
}

/*
 *  Find cursor location within text, given (x,y) screen coords.
 *  This mirrors paint_text_box() layout logic.
 *
 *  Output: Offset in text if found.
 *          If not found, -offset of end of text there was room to paint.
 */
int TtFont::find_cursor(
		const char* text,        // UTF-8 text.
		int x, int y,            // Top-left corner of box.
		int w, int h,            // Box dimensions.
		int cx, int cy,          // Mouse location to find.
		int vert_lead) {         // Extra spacing between lines.
#ifdef HAVE_FREETYPE2
	if (!ft_face || !text) {
		return -1;
	}
	const char* start       = text;
	const int   endx        = x + w;
	int         curx        = x;
	int         cury        = y;
	const int   line_height = get_text_height() + vert_lead + ver_lead;
	const int   space_width = get_text_width(" ", 1);
	const int   max_lines   = h / line_height;
	int         cur_line    = 0;

	while (*text) {
		// Handle newlines.
		if (*text == '\n') {
			if (cy >= cury && cy < cury + line_height
					&& cx >= curx && cx < endx) {
				return static_cast<int>(text - start);
			}
			text++;
			curx = x;
			cur_line++;
			cury += line_height;
			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Handle spaces/tabs.
		if (*text == ' ' || *text == '\t') {
			if (cy >= cury && cy < cury + line_height
					&& cx >= curx && cx < curx + space_width) {
				return static_cast<int>(text - start);
			}
			text++;
			curx += space_width;
			continue;
		}

		if (cur_line >= max_lines) {
			break;
		}

		// Pass word & get its width.
		const char* ewrd  = text;
		while (*text && *text != ' ' && *text != '\t' && *text != '\n') {
			text++;
		}
		int width = get_text_width(ewrd, static_cast<int>(text - ewrd));

		// Word-wrap?
		if (curx + width - hor_lead > endx && curx > x) {
			// Past end of this line?
			if (cy >= cury && cy < cury + line_height
					&& cx >= curx && cx < endx) {
				return static_cast<int>(ewrd - start);
			}
			curx = x;
			cur_line++;
			cury += line_height;
			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Cursor within this word?
		if (cy >= cury && cy < cury + line_height
				&& cx >= curx && cx < curx + width) {
			int woff = find_xcursor(ewrd,
							static_cast<int>(text - ewrd),
							cx - curx);
			if (woff >= 0) {
				return static_cast<int>(ewrd - start) + woff;
			}
		}

		curx += width;
	}

	// End of last line?
	if (cy >= cury && cy < cury + line_height
			&& cx >= curx && cx < endx) {
		return static_cast<int>(text - start);
	}
	return -static_cast<int>(text - start);
#else
	ignore_unused_variable_warning(text, x, y, w, h, cx, cy, vert_lead);
	return -1;
#endif
}

/*
 *  Find an x-coordinate within a piece of text.
 *
 *  Output: Offset if found, else -1.
 */
int TtFont::find_xcursor(
		const char* text,       // UTF-8 text.
		int         textlen,    // Byte length.
		int         cx) {       // X location to find.
#ifdef HAVE_FREETYPE2
	if (!ft_face || !text) {
		return -1;
	}
	const char* start   = text;
	const char* end     = (textlen >= 0) ? text + textlen : nullptr;
	int         curx    = 0;
	const char* s       = text;

	while (*s && (textlen < 0 || s - start < textlen)) {
		const char* before    = s;
		unsigned long ch      = utf8_decode(s, end);
		int           char_w  = hor_lead;

		FT_UInt glyph_index = FT_Get_Char_Index(ft_face, ch);
		if (glyph_index == 0) {
			glyph_index = FT_Get_Char_Index(ft_face, '?');
		}
		if (glyph_index != 0) {
			FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
			char_w += ft_face->glyph->advance.x >> 6;
		}
		if (cx >= curx && cx < curx + char_w) {
			return static_cast<int>(before - start);
		}
		curx += char_w;
	}
	return -1;
#else
	ignore_unused_variable_warning(text, textlen, cx);
	return -1;
#endif
}

/*
 *  Center text horizontally at the given location.
 */
int TtFont::center_text(
		Image_buffer8* win,
		int            x,
		int            y,
		const char*    s,
		unsigned char  fg_color,
		int            sh_color,
		unsigned char* trans) {
	int width = get_text_width(s);
	return paint_text(win, s, x - width / 2, y, fg_color, sh_color, trans);
}

/*
 *  Draw null-terminated text at a fixed character width.
 */
int TtFont::paint_text_fixedwidth(
		Image_buffer8* win,
		const char*    text,
		int            xoff,
		int            yoff,
		int            width,
		unsigned char* trans) {
	return paint_text_fixedwidth(win, text, -1, xoff, yoff, width, trans);
}

/*
 *  Draw text with explicit length at a fixed character width.
 */
int TtFont::paint_text_fixedwidth(
		Image_buffer8* win,
		const char*    text,
		int            textlen,
		int            xoff,
		int            yoff,
		int            width,
		unsigned char* trans) {
#ifdef HAVE_FREETYPE2
	if (!ft_face || !text) {
		return 0;
	}
	int         x          = xoff;
	int         y          = yoff + get_text_baseline();
	const char* s          = text;
	const char* text_start = text;
	const char* text_end   = (textlen >= 0) ? text + textlen : nullptr;

	while (*s) {
		if (textlen >= 0 && s - text_start >= textlen) {
			break;
		}
		unsigned long ch          = utf8_decode(s, text_end);
		FT_UInt       glyph_index = FT_Get_Char_Index(ft_face, ch);
		if (glyph_index == 0) {
			glyph_index = FT_Get_Char_Index(ft_face, '?');
			if (glyph_index == 0) {
				x += width;
				continue;
			}
		}
		FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

		FT_Bitmap* bitmap      = &ft_face->glyph->bitmap;
		int        adv_x       = ft_face->glyph->advance.x >> 6;
		int        centering   = (width - adv_x) / 2;
		int        glyph_x     = x + centering + ft_face->glyph->bitmap_left;
		int        glyph_y     = y - ft_face->glyph->bitmap_top;

		// Draw foreground (color set via TtFont::set_color).
		unsigned char fg_pixel = trans ? trans[fg_color] : fg_color;
		blit_glyph_bitmap(win, bitmap, glyph_x, glyph_y, fg_pixel);

		x += width;
	}
	return x - xoff;
#else
	ignore_unused_variable_warning(win, text, textlen, xoff, yoff, width, trans);
	return 0;
#endif
}

/*
 *  Draw text within a rectangular area at a fixed character width.
 *  Handles word wrapping, newlines, spaces, tabs, and punctuation breaks.
 */
int TtFont::paint_text_box_fixedwidth(
		Image_buffer8* win,
		const char*    text,
		int            x,
		int            y,
		int            w,
		int            h,
		int            char_width,
		int            vert_lead,
		int            pbreak,
		unsigned char* trans) {
	const char* start   = text;
	auto        clipsave = win->SaveClip();
	auto        newclip  = clipsave.Rect().intersect(TileRect(x, y, w, h));
	win->set_clip(newclip.x, newclip.y, newclip.w, newclip.h);

	const int   endx           = x + w;
	const int   line_height    = get_text_height() + vert_lead + ver_lead;
	const int   max_lines      = h / line_height;
	int         curx           = x;
	int         cur_line       = 0;
	int         cury           = y;
	const char* last_punct_end = nullptr;
	int         last_punct_line   = -1;
	int         last_punct_offset = -1;

	// Build lines with word wrapping using fixed character width.
	std::vector<std::string> lines;
	std::string current_line;

	while (*text) {
		// Handle newlines.
		if (*text == '\n') {
			lines.push_back(current_line);
			current_line.clear();
			cur_line++;
			curx = x;
			cury += line_height;
			text++;
			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Handle spaces/tabs.
		if (*text == ' ' || *text == '\t') {
			const char* sp = text;
			while (*text == ' ' || *text == '\t') {
				text++;
			}
			int nsp = static_cast<int>(text - sp);
			current_line.append(sp, nsp);
			curx += nsp * char_width;

			if (cur_line >= max_lines) {
				break;
			}
			continue;
		}

		// Pass word.
		const char* ewrd = text;
		while (*text && *text != ' ' && *text != '\t' && *text != '\n') {
			text++;
		}
		if (text == ewrd) {
			continue;
		}
		int word_width = static_cast<int>(text - ewrd) * char_width;

		// Word-wrap?
		if (curx + word_width - hor_lead > endx && curx > x) {
			lines.push_back(current_line);
			current_line.clear();
			curx = x;
			cur_line++;
			cury += line_height;
			if (cur_line >= max_lines) {
				break;
			}
		}

		// Store word.
		current_line.append(ewrd, text - ewrd);
		curx += word_width;

		// Keep loc. of punct. endings.
		if (text[-1] == '.' || text[-1] == '?' || text[-1] == '!'
				|| text[-1] == ',' || text[-1] == '"') {
			last_punct_end    = text;
			last_punct_line   = cur_line;
			last_punct_offset = static_cast<int>(current_line.length());
		}
	}

	// End of loop.
	if (cur_line < max_lines) {
		lines.push_back(current_line);
	}

	// Text overflow handling.
	if (*text && pbreak && last_punct_end) {
		text = last_punct_end;
		while (*text == ' ' || *text == '\t' || *text == '\n') {
			text++;
		}
	} else if (*text) {
		last_punct_line = -1;
	}

	// Render lines.
	cury = y;
	for (int i = 0; i <= cur_line && i < static_cast<int>(lines.size()); i++) {
		const char* str = lines[i].c_str();
		int len = static_cast<int>(lines[i].length());
		if (i == last_punct_line && last_punct_offset >= 0) {
			len = last_punct_offset;
		}
		paint_text_fixedwidth(win, str, len, x, cury, char_width, trans);
		cury += line_height;
		if (i == last_punct_line) {
			break;
		}
	}

	if (*text) {
		return -static_cast<int>(text - start);
	}
	return cury - y;
}
