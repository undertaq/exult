/*
 *  Copyright (C) 2000-2022  The Exult Team
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

#include "font.h"

#include "ttfont.h"
#include "font_map.h"

#include "U7file.h"
#include "databuf.h"
#include "exceptions.h"
#include "ibuf8.h"
#include "ignore_unused_variable_warning.h"
#include "vgafile.h"

using std::size_t;
using std::string;
using std::strncmp;
using std::toupper;

FontManager fontManager;

/*
 *  CJK / font-byte helpers used by CJKRoutingFont.
 */

// Returns true if the string contains any byte >= 0x80 (potential CJK font byte).
static bool has_high_byte(const char* text, int textlen = -1) {
	if (!text || textlen == 0) {
		return false;
	}
	if (textlen < 0) {
		for (const char* p = text; *p; ++p) {
			if (static_cast<unsigned char>(*p) >= 0x80) {
				return true;
			}
		}
	} else {
		for (int i = 0; i < textlen; ++i) {
			if (static_cast<unsigned char>(text[i]) >= 0x80) {
				return true;
			}
		}
	}
	return false;
}

// Reverse-translate font bytes back to UTF-8 using the font_map reverse lookup.
// Each font byte (0x00-0xFF) is mapped to its UTF-8 character. Non-CJK bytes
// that have no mapping are passed through unchanged.
static std::string font_bytes_to_utf8(const char* text, int textlen = -1) {
	std::string result;
	int         len = (textlen < 0) ? static_cast<int>(std::strlen(text)) : textlen;
	result.reserve(static_cast<size_t>(len) * 2);    // Rough upper bound.
	char buf[FONT_MAP_MAX_UTF8_BYTES + 1];
	for (int i = 0; i < len; ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		// Raw UTF-8 3-byte CJK sequence (0xE0-0xEF + 2 continuation bytes).
		if (c >= 0xE0 && i + 2 < len &&
			(static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
			(static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80) {
			result.append(text + i, 3);  // Pass through as-is.
			i += 2;
			continue;
		}
		const size_t n = translate_font_hex_to_utf8(c, buf);
		result.append(buf, n);
	}
	return result;
}

//	Want a more restrictive test for space.
inline bool Is_space(char c) {
	return c == ' ' || c == '\n' || c == '\t';
}

/*
 *  Pass space.
 */

static const char* Pass_whitespace(const char* text) {
	while (Is_space(*text)) {
		text++;
	}
	return text;
}

// Just spaces and tabs:
static const char* Pass_space(const char* text) {
	while (*text == ' ' || *text == '\t') {
		text++;
	}
	return text;
}

/*
 *  Pass a word.
 */

static const char* Pass_word(const char* text) {
	while (*text && (*text != '^') && (!Is_space(*text) || (*text == '\f') || (*text == '\v'))) {
		text++;
	}
	return text;
}

/*
 *  Draw text within a rectangular area.
 *  Special characters handled are:
 *      \n  New line.
 *      space   Word break.
 *      tab Treated like a space for now.
 *      *   Page break.
 *      ^   Uppercase next letter.
 *
 *  Output: If out of room, -offset of end of text painted.
 *      Else height of text painted.
 */

int Font::paint_text_box(
		Image_buffer8* win,                // Buffer to paint in.
		const char* text, int x, int y,    // Top-left corner of box.
		int w, int h,                      // Dimensions.
		int            vert_lead,          // Extra spacing between lines.
		bool           pbreak,             // End at punctuation.
		bool           center,             // Center each line.
		Cursor_info*   cursor,             // We set x, y if not nullptr.
		unsigned char* trans) {
	const char* start    = text;    // Remember the start.
	auto        clipsave = win->SaveClip();
	auto        newclip  = clipsave.Rect().intersect(TileRect(x, y, w, h));
	win->set_clip(newclip.x, newclip.y, newclip.w, newclip.h);

	const int   endx           = x + w;    // Figure where to stop.
	int         curx           = x;
	int         cury           = y;
	const int   height         = get_text_height() + vert_lead + ver_lead;
	const int   space_width    = get_text_width(" ", 1);
	const int   max_lines      = h / height;    // # lines that can be shown.
	auto*       lines          = new string[max_lines + 1];
	int         cur_line       = 0;
	const char* last_punct_end = nullptr;    // ->last period, qmark, etc.
	// Last punct in 'lines':
	int last_punct_line   = -1;
	int last_punct_offset = -1;
	int coff              = -1;

	if (cursor) {
		coff      = cursor->offset;
		cursor->x = -1;
	}
	while (*text) {
		if (cursor && text - start == coff) {
			cursor->set_found(curx, cury, cur_line);
		}
		switch (*text) {    // Special cases.
		case '\n':          // Next line.
			curx = x;
			text++;
			cur_line++;
			cury += height;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
			continue;
		case '\r':    //??
			text++;
			continue;
		case ' ':    // Space.
		case '\t': {
			// Pass space.
			const char* wrd = Pass_space(text);
			if (wrd != text) {
				int w = get_text_width(text, static_cast<uint32>(wrd - text));
				if (w <= 0) {
					w = space_width;
				}
				const int nsp = w / space_width;
				lines[cur_line].append(nsp, ' ');
				if (cursor && coff > text - start && coff < wrd - start) {
					cursor->set_found(curx + static_cast<uint32>(coff - (text - start)) * space_width, cury, cur_line);
				}
				curx += nsp * space_width;
			}
			text = wrd;
			break;
		}
		}
		if (cur_line >= max_lines) {
			break;
		}

		if (*text == '*') {
			text++;
			if (cur_line) {
				break;
			}
		}
		const bool ucase_next = *text == '^';
		if (ucase_next) {    // Skip it.
			text++;
		}
		// Pass word & get its width.
		const char* ewrd = Pass_word(text);
		int         width;
		if (ucase_next) {
			const char c = static_cast<char>(toupper(static_cast<unsigned char>(*text)));
			width        = get_text_width(&c, 1u) + get_text_width(text + 1, static_cast<uint32>(ewrd - text - 1));
		} else {
			width = get_text_width(text, static_cast<uint32>(ewrd - text));
		}
		if (curx + width - hor_lead > endx) {
			// Word-wrap.
			if (ucase_next) {
				text--;    // Put the '^' back.
			}
			curx = x;
			cur_line++;
			cury += height;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
		}
		if (cursor && coff >= text - start && coff < ewrd - start) {
			cursor->set_found(curx + get_text_width(text, static_cast<uint32>(coff - (text - start))), cury, cur_line);
		}
		// Store word.
		if (ucase_next) {
			lines[cur_line].push_back(static_cast<char>(toupper(static_cast<unsigned char>(*text))));
			++text;
		}
		lines[cur_line].append(text, ewrd - text);
		curx += width;
		text = ewrd;    // Continue past the word.
		// Keep loc. of punct. endings.
		if (text[-1] == '.' || text[-1] == '?' || text[-1] == '!' || text[-1] == ',' || text[-1] == '"') {
			last_punct_end    = text;
			last_punct_line   = cur_line;
			last_punct_offset = static_cast<int>(lines[cur_line].length());
		}
	}
	if (*text &&    // Out of room?
					// Break off at end of punct.
		pbreak && last_punct_end) {
		text = Pass_whitespace(last_punct_end);
	} else {
		last_punct_line = -1;
		if (cursor && text - start == coff &&    // Cursor at very end?
			cur_line < max_lines) {
			cursor->set_found(curx, cury, cur_line);
		}
	}
	if (cursor) {
		cursor->nlines = cur_line + (cur_line < max_lines);
	}
	cury = y;    // Render text.
	for (int i = 0; i <= cur_line; i++) {
		const char* str = lines[i].c_str();
		int         len = static_cast<int>(lines[i].length());
		if (i == last_punct_line) {
			len = last_punct_offset;
		}
		if (center) {
			center_text(win, x + w / 2, cury, str, trans);
		} else {
			paint_text(win, str, len, x, cury, trans);
		}
		cury += height;
		if (i == last_punct_line) {
			break;
		}
	}
	delete[] lines;
	if (*text) {                                   // Out of room?
		return -static_cast<int>(text - start);    // Return -offset of end.
	} else {                                       // Else return height.
		return cury - y;
	}
}

/*
 *  Draw text at a given location (which is the upper-left corner of the
 *  place to draw.
 *
 *  Output: Width in pixels of what was drawn.
 */

int Font::paint_text(
		Image_buffer8* win,     // Buffer to paint in.
		const char*    text,    // What to draw, 0-delimited.
		int xoff, int yoff,     // Upper-left corner of where to start.
		unsigned char* trans) {
	ignore_unused_variable_warning(win);
	int x = xoff;
	yoff += get_text_baseline();
	if (font_shapes) {
		int chr;
		while ((chr = *text++) != 0) {
			Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr));
			if (!shape || !shape->is_rle()) {
				auto oldflags = std::cerr.flags();

				std::cerr << " unable to find rle frame for character '" << char(chr) << "' 0x" << std::hex << chr << " in font"
						  << std::endl;
				std::cerr.flags(oldflags);

				continue;
			}
			if (trans) {
				shape->paint_rle_remapped(x, yoff, trans);
			} else {
				shape->paint_rle(x, yoff);
			}
			x += shape->get_width() + hor_lead;
		}
	}
	return x - xoff;
}

/*
 *  Paint text using font from "fonts.vga".
 *
 *  Output: Width in pixels of what was painted.
 */

int Font::paint_text(
		Image_buffer8* win,        // Buffer to paint in.
		const char*    text,       // What to draw.
		int            textlen,    // Length of text.
		int xoff, int yoff,        // Upper-left corner of where to start.
		unsigned char* trans) {
	ignore_unused_variable_warning(win);
	int x = xoff;
	yoff += get_text_baseline();
	if (font_shapes) {
		while (textlen--) {
			int          chr;
			Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr = *text++));
			if (!shape || !shape->is_rle()) {
				auto oldflags = std::cerr.flags();

				std::cerr << " unable to find rle frame for character '" << char(chr) << "' 0x" << std::hex << chr << " in font"
						  << std::endl;
				std::cerr.flags(oldflags);

				continue;
			}
			if (trans) {
				shape->paint_rle_remapped(x, yoff, trans);
			} else {
				shape->paint_rle(x, yoff);
			}
			x += shape->get_width() + hor_lead;
		}
	}
	return x - xoff;
}

/*
 *
 *  FIXED WIDTH RENDERING
 *
 */

/*
 *  Draw text within a rectangular area.
 *  Special characters handled are:
 *      \n  New line.
 *      space   Word break.
 *      tab Treated like a space for now.
 *      *   Page break.
 *      ^   Uppercase next letter.
 *
 *  Output: If out of room, -offset of end of text painted.
 *      Else height of text painted.
 */

int Font::paint_text_box_fixedwidth(
		Image_buffer8* win,                // Buffer to paint in.
		const char* text, int x, int y,    // Top-left corner of box.
		int w, int h,                      // Dimensions.
		int            char_width,         // Width of each character
		int            vert_lead,          // Extra spacing between lines.
		int            pbreak,             // End at punctuation.
		unsigned char* trans) {
	const char* start = text;    // Remember the start.
	win->set_clip(x, y, w, h);
	const int   endx           = x + w;    // Figure where to stop.
	int         curx           = x;
	int         cury           = y;
	const int   height         = get_text_height() + vert_lead + ver_lead;
	const int   max_lines      = h / height;    // # lines that can be shown.
	auto*       lines          = new string[max_lines + 1];
	int         cur_line       = 0;
	const char* last_punct_end = nullptr;    // ->last period, qmark, etc.
	// Last punct in 'lines':
	int last_punct_line   = -1;
	int last_punct_offset = -1;

	while (*text) {
		switch (*text) {    // Special cases.
		case '\n':          // Next line.
			curx = x;
			text++;
			cur_line++;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
			continue;
		case ' ':    // Space.
		case '\t': {
			// Pass space.
			const char* wrd = Pass_space(text);
			if (wrd != text) {
				const int w   = static_cast<int>(wrd - text) * char_width;
				const int nsp = w / char_width;
				lines[cur_line].append(nsp, ' ');
				curx += nsp * char_width;
			}
			text = wrd;
			break;
		}
		}

		if (cur_line >= max_lines) {
			break;
		}

		if (*text == '*') {
			text++;
			if (cur_line) {
				break;
			}
		}
		const bool ucase_next = *text == '^';
		if (ucase_next) {    // Skip it.
			text++;
		}
		// Pass word & get its width.
		const char* ewrd  = Pass_word(text);
		const int   width = static_cast<int>(ewrd - text) * char_width;
		if (curx + width - hor_lead > endx) {
			// Word-wrap.
			if (ucase_next) {
				text--;    // Put the '^' back.
			}
			curx = x;
			cur_line++;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
		}

		// Store word.
		if (ucase_next) {
			lines[cur_line].push_back(static_cast<char>(toupper(static_cast<unsigned char>(*text))));
			++text;
		}
		lines[cur_line].append(text, ewrd - text);
		curx += width;
		text = ewrd;    // Continue past the word.
		// Keep loc. of punct. endings.
		if (text[-1] == '.' || text[-1] == '?' || text[-1] == '!' || text[-1] == ',' || text[-1] == '"') {
			last_punct_end    = text;
			last_punct_line   = cur_line;
			last_punct_offset = static_cast<int>(lines[cur_line].length());
		}
	}
	if (*text &&    // Out of room?
					// Break off at end of punct.
		pbreak && last_punct_end) {
		text = Pass_whitespace(last_punct_end);
	} else {
		last_punct_line = -1;
	}
	// Render text.
	for (int i = 0; i <= cur_line; i++) {
		const char* str = lines[i].data();
		int         len = static_cast<int>(lines[i].length());
		if (i == last_punct_line) {
			len = last_punct_offset;
		}
		paint_text_fixedwidth(win, str, len, x, cury, char_width, trans);
		cury += height;
		if (i == last_punct_line) {
			break;
		}
	}
	win->clear_clip();
	delete[] lines;
	if (*text) {                                   // Out of room?
		return -static_cast<int>(text - start);    // Return -offset of end.
	} else {                                       // Else return height.
		return cury - y;
	}
}

/*
 *  Draw text at a given location (which is the upper-left corner of the
 *  place to draw. Text will be drawn with the fixed width specified.
 *
 *  Output: Width in pixels of what was drawn.
 */

int Font::paint_text_fixedwidth(
		Image_buffer8* win,      // Buffer to paint in.
		const char*    text,     // What to draw, 0-delimited.
		int xoff, int yoff,      // Upper-left corner of where to start.
		int            width,    // Width of each character
		unsigned char* trans) {
	ignore_unused_variable_warning(win);
	int x = xoff;
	int w;
	int chr;
	yoff += get_text_baseline();
	while ((chr = *text++) != 0) {
		Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr));
		if (!shape || !shape->is_rle()) {
			auto oldflags = std::cerr.flags();

			std::cerr << " unable to find rle frame for character '" << char(chr) << "' 0x" << std::hex << chr << " in font"
					  << std::endl;
			std::cerr.flags(oldflags);

			continue;
		}
		x += w = (width - shape->get_width()) / 2;
		if (trans) {
			shape->paint_rle_remapped(x, yoff, trans);
		} else {
			shape->paint_rle(x, yoff);
		}
		x += width - w;
	}
	return x - xoff;
}

/*
 *  Draw text at a given location (which is the upper-left corner of the
 *  place to draw. Text will be drawn with the fixed width specified.
 *
 *  Output: Width in pixels of what was drawn.
 */

int Font::paint_text_fixedwidth(
		Image_buffer8* win,        // Buffer to paint in.
		const char*    text,       // What to draw.
		int            textlen,    // Length of text.
		int xoff, int yoff,        // Upper-left corner of where to start.
		int            width,      // Width of each character
		unsigned char* trans) {
	ignore_unused_variable_warning(win);
	int w;
	int x = xoff;
	yoff += get_text_baseline();
	while (textlen--) {
		int          chr;
		Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr = *text++));
		if (!shape || !shape->is_rle()) {
			auto oldflags = std::cerr.flags();
			std::cerr << " unable to find rle frame for character '" << char(chr) << "' 0x" << std::hex << chr << " in font"
					  << std::endl;

			std::cerr.flags(oldflags);
			continue;
		}
		x += w = (width - shape->get_width()) / 2;
		if (trans) {
			shape->paint_rle_remapped(x, yoff, trans);
		} else {
			shape->paint_rle(x, yoff);
		}
		x += width - w;
	}
	return x - xoff;
}

/*
 *  Get the width in pixels of a 0-delimited string.
 */

int Font::get_text_width(const char* text) {
	int width = 0;
	if (font_shapes) {
		short chr;
		while ((chr = *text++) != 0) {
			Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr));
			if (shape && shape->is_rle()) {
				width += shape->get_width() + hor_lead;
			}
		}
	}
	return width;
}

/*
 *  Get the width in pixels of a string given by length.
 */

int Font::get_text_width(
		const char* text,
		int         textlen    // Length of text.
) {
	int width = 0;
	if (font_shapes) {
		while (textlen--) {
			Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(*text++));
			if (shape && shape->is_rle()) {
				width += shape->get_width() + hor_lead;
			}
		}
	}
	return width;
}

void Font::get_text_box_dims(const char* text, int& width, int& height, int vert_lead) {
	width         = 0;
	height        = 0;
	int cur_width = 0;

	int num_lines = 1;
	if (font_shapes) {
		short chr;
		while ((chr = *text++) != 0) {
			if (chr == '\n') {
				num_lines++;
				width     = std::max(width, cur_width);
				cur_width = 0;
			}
			Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(chr));
			if (shape && shape->is_rle()) {
				cur_width += shape->get_width() + hor_lead;
			}
		}
		width  = std::max(width, cur_width);
		height = num_lines * (get_text_height() + vert_lead + ver_lead);
	}
}

/*
 *  Get font line-height.
 */

int Font::get_text_height() {
	// Note, I wont assume the fonts exist
	// Shape_frame *A = font_shapes->get_frame('A');
	// Shape_frame *y = font_shapes->get_frame('y');
	return highest + lowest + 1;
}

/*
 *  Get font baseline as the distance from the top.
 */

int Font::get_text_baseline() {
	// Shape_frame *A = font_shapes->get_frame('A');
	return highest;
}

/*
 *  Find cursor location within text, given (x,y) screen coords.
 *  Note:  This has to match paint_text_box().
 *
 *  Output: Offset in text if found.
 *      If not found, -offset of end of text there was room to paint.
 */

int Font::find_cursor(
		const char* text, int x, int y,    // Top-left corner of box.
		int w, int h,                      // Dimensions.
		int cx, int cy,                    // Mouse loc. to find.
		int vert_lead                      // Extra spacing between lines.
) {
	const char* start       = text;     // Remember the start.
	const int   endx        = x + w;    // Figure where to stop.
	int         curx        = x;
	int         cury        = y;
	const int   height      = get_text_height() + vert_lead + ver_lead;
	const int   space_width = get_text_width(" ", 1);
	const int   max_lines   = h / height;    // # lines that can be shown.
	int         cur_line    = 0;
	int         chr;

	while ((chr = *text) != 0) {
		switch (chr) {    // Special cases.
		case '\n':        // Next line.
			if (cy >= cury && cy < cury + height && cx >= curx && cx < x + w) {
				return static_cast<int>(text - start);
			}
			++text;
			curx = x;
			cur_line++;
			cury += height;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
			continue;
		case '\r':    //??
			++text;
			continue;
		case ' ':    // Space.
		case '\t':
			if (cy >= cury && cy < cury + height && cx >= curx && cx < curx + space_width) {
				return static_cast<int>(text - start);
			}
			++text;
			curx += space_width;
			continue;
		}
		if (cur_line >= max_lines) {
			break;
		}

		if (*text == '*') {
			text++;
			if (cur_line) {
				break;
			}
		}
		const bool ucase_next = *text == '^';
		if (ucase_next) {    // Skip it.
			text++;
		}
		// Pass word & get its width.
		const char* ewrd = Pass_word(text);
		int         width;
		if (ucase_next) {
			const char c = static_cast<char>(toupper(static_cast<unsigned char>(*text)));
			width        = get_text_width(&c, 1u) + get_text_width(text + 1, static_cast<uint32>(ewrd - text - 1));
		} else {
			width = get_text_width(text, static_cast<uint32>(ewrd - text));
		}
		if (curx + width - hor_lead > endx) {
			// Word-wrap.
			// Past end of this line?
			if (cy >= cury && cy < cury + height && cx >= curx && cx < x + w) {
				return static_cast<int>(text - start - 1);
			}
			curx = x;
			cur_line++;
			cury += height;
			if (cur_line >= max_lines) {
				break;    // No more room.
			}
		}
		if (cy >= cury && cy < cury + height && cx >= curx && cx < curx + width) {
			const int woff = find_xcursor(text, static_cast<int>(ewrd - text), cx - curx);
			if (woff >= 0) {
				return static_cast<int>(text - start) + woff;
			}
		}
		curx += width;
		text = ewrd;    // Continue past the word.
	}
	if (cy >= cury && cy < cury + height &&    // End of last line?
		cx >= curx && cx < x + w) {
		return static_cast<int>(text - start);
	}
	return -static_cast<int>(text - start);    // Failed, so indicate where we are.
}

/*
 *  Find an x-coord. within a piece of text.
 *
 *  Output: Offset if found, else -1.
 */

int Font::find_xcursor(
		const char* text,
		int         textlen,    // Length of text.
		int         cx          // Loc. to find.
) {
	const char* start = text;
	int         curx  = 0;
	while (textlen--) {
		Shape_frame* shape = font_shapes->get_frame(static_cast<unsigned char>(*text++));
		if (shape && shape->is_rle()) {
			const int w = shape->get_width() + hor_lead;
			if (cx >= curx && cx < curx + w) {
				return static_cast<int>(text - 1 - start);
			}
			curx += w;
		}
	}
	return -1;
}

/*
 *  Get the default pixel value used in this font for text rendering.
 *  Scans the RLE-encoded glyph for character 'X' and returns the first
 *  non-255 pixel value (255 is transparent in Exult's RLE encoding).
 *  Returns 255 if the font has no glyph data.
 */
unsigned char Font::get_text_pixel() const {
	if (!font_shapes) {
		return 255;
	}
	// Use 'X' as reference; most fonts have it.
	const int ref_char = 'X';
	if (ref_char >= font_shapes->get_num_frames()) {
		return 255;
	}
	Shape_frame* shape = font_shapes->get_frame(ref_char);
	if (!shape || !shape->is_rle()) {
		return 255;
	}
	unsigned char* data = shape->get_data();
	if (!data) {
		return 255;
	}
	const unsigned char* in = data;
	int                  scanlen;
	while ((scanlen = little_endian::Read2(in)) != 0) {
		const int encoded = scanlen & 1;
		scanlen >>= 1;
		in += 4;    // Skip x, y offsets.
		if (encoded) {
			int remaining = scanlen;
			while (remaining > 0) {
				unsigned char bcnt = *in++;
				const int     repeat = bcnt & 1;
				bcnt >>= 1;
				remaining -= bcnt;
				if (repeat) {
					const unsigned char pix = *in++;
					if (pix != 255) {
						return pix;
					}
				} else {
					for (int i = 0; i < bcnt; i++) {
						const unsigned char pix = *in++;
						if (pix != 255) {
							return pix;
						}
					}
				}
			}
		} else {
			for (int i = 0; i < scanlen; i++) {
				const unsigned char pix = *in++;
				if (pix != 255) {
					return pix;
				}
			}
		}
	}
	return 255;
}

Font::Font() = default;

Font::Font(const File_spec& fname0, int index, int hlead, int vlead) {
	load(fname0, index, hlead, vlead);
}

Font::Font(const File_spec& fname0, const File_spec& fname1, int index, int hlead, int vlead) {
	load(fname0, fname1, index, hlead, vlead);
}

void Font::clean_up() {
	font_shapes.reset();
}

/**
 *  Loads a font from a multiobject.
 *  @param font_obj Where we are loading from.
 *  @param hleah    Horizontal lead of the font.
 *  @param vleah    Vertical lead of the font.
 */
int Font::load_internal(IDataSource& data, int hlead, int vlead) {
	if (!data.good()) {
		font_shapes.reset();
		hor_lead = 0;
		ver_lead = 0;
		return -1;
	} else {
		// Is it an IFF archive?
		char hdr[5] = {0};
		data.read(hdr, 4);
		data.seek(0);
		if (!strncmp(hdr, "font", 4)) {
			data.skip(8);    // Yes, skip first 8 bytes.
		}
		font_shapes = std::make_unique<Shape_file>(&data);
		hor_lead    = hlead;
		ver_lead    = vlead;
		calc_highlow();
	}
	return 0;
}

int Font::load(const File_spec& fname0, int index, int hlead, int vlead) {
	clean_up();
	IExultDataSource data(fname0, index);
	return load_internal(data, hlead, vlead);
}

int Font::load(const File_spec& fname0, const File_spec& fname1, int index, int hlead, int vlead) {
	clean_up();
	IExultDataSource data(fname0, fname1, index);
	return load_internal(data, hlead, vlead);
}

int Font::center_text(Image_buffer8* win, int x, int y, const char* s, unsigned char* trans) {
	return draw_text(win, x - get_text_width(s) / 2, y, s, trans);
}

void Font::calc_highlow() {
	bool unset = true;

	for (int i = 0; i < font_shapes->get_num_frames(); i++) {
		Shape_frame* f = font_shapes->get_frame(i);

		if (!f || !f->is_rle()) {
			continue;
		}

		if (unset) {
			unset   = false;
			highest = f->get_yabove();
			lowest  = f->get_ybelow();
			continue;
		}

		if (f->get_yabove() > highest) {
			highest = f->get_yabove();
		}
		if (f->get_ybelow() > lowest) {
			lowest = f->get_ybelow();
		}
	}
}

FontManager::~FontManager() {
	reset();
}

/**
 *  Loads a font from a File_spec.
 *  @param name Name to give to this font.
 *  @param fname0   First file spec.
 *  @param index    Number of font to load.
 *  @param hleah    Horizontal lead of the font.
 *  @param vleah    Vertical lead of the font.
 */
void FontManager::add_font(const char* name, const File_spec& fname0, int index, int hlead, int vlead) {
	remove_font(name);

	auto font = std::make_shared<Font>(fname0, index, hlead, vlead);

	fonts[name] = font;
}

/**
 *  Loads a font from a File_spec.
 *  @param name Name to give to this font.
 *  @param fname0   First file spec.
 *  @param fname1   Second file spec.
 *  @param index    Number of font to load.
 *  @param hleah    Horizontal lead of the font.
 *  @param vleah    Vertical lead of the font.
 */
void FontManager::add_font(const char* name, const File_spec& fname0, const File_spec& fname1, int index, int hlead, int vlead) {
	remove_font(name);

	auto font = std::make_shared<Font>(fname0, fname1, index, hlead, vlead);

	fonts[name] = font;
}

void FontManager::remove_font(const char* name) {
	fonts.erase(name);
}


/*
 *  Wrapper that routes text rendering to the TC (Traditional Chinese)
 *  TTF font when the text contains CJK font bytes (>= 0x80) and the TC
 *  font is loaded (registered as "ttf/tc" in FontManager).
 *
 *  Pure-ASCII strings bypass all routing and are delegated to the base
 *  (bitmap) font with zero runtime overhead beyond a single map lookup
 *  and a fast scan for high bytes.
 *
 *  When CJK bytes are detected, the wrapper reverse-translates the font
 *  bytes back to UTF-8 via translate_font_hex_to_utf8() and passes the
 *  reconstructed UTF-8 string to the TC font.  This ensures the TTF
 *  rendering path (which expects UTF-8) receives valid input.
 *
 *  If the TC font is not loaded, or the text is pure-ASCII, the wrapper
 *  is transparent — it delegates directly to the base font.
 */
class CJKRoutingFont : public Font {
	std::shared_ptr<Font> base_font;
	int                   base_ver_lead;

public:
	CJKRoutingFont(std::shared_ptr<Font> base)
			: Font(), base_font(std::move(base)), base_ver_lead(base_font->get_ver_lead()) {
	}

	int get_ver_lead() const {
		return base_ver_lead;
	}

	int paint_text(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->paint_text(win, utf8.c_str(), xoff, yoff, trans);
		}
		return base_font->paint_text(win, text, xoff, yoff, trans);
	}

	int paint_text(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text, textlen)) {
			auto utf8 = font_bytes_to_utf8(text, textlen);
			return tc->paint_text(
					win, utf8.c_str(), static_cast<int>(utf8.size()), xoff, yoff, trans);
		}
		return base_font->paint_text(win, text, textlen, xoff, yoff, trans);
	}

	int paint_text_box(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int vert_lead, bool pbreak, bool center, Cursor_info* cursor,
			unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->paint_text_box(
					win, utf8.c_str(), x, y, w, h, vert_lead, pbreak, center, cursor, trans);
		}
		return base_font->paint_text_box(
				win, text, x, y, w, h, vert_lead, pbreak, center, cursor, trans);
	}

	int get_text_width(const char* text) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->get_text_width(utf8.c_str());
		}
		return base_font->get_text_width(text);
	}

	int get_text_width(const char* text, int textlen) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text, textlen)) {
			auto utf8 = font_bytes_to_utf8(text, textlen);
			return tc->get_text_width(utf8.c_str(), static_cast<int>(utf8.size()));
		}
		return base_font->get_text_width(text, textlen);
	}

	int get_text_height() override {
		return base_font->get_text_height();
	}

	int get_text_baseline() override {
		return base_font->get_text_baseline();
	}

	void get_text_box_dims(
			const char* text, int& width, int& height, int vert_lead) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			tc->get_text_box_dims(utf8.c_str(), width, height, vert_lead);
			return;
		}
		base_font->get_text_box_dims(text, width, height, vert_lead);
	}

	int find_cursor(
			const char* text, int x, int y, int w, int h, int cx, int cy,
			int vert_lead) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->find_cursor(utf8.c_str(), x, y, w, h, cx, cy, vert_lead);
		}
		return base_font->find_cursor(text, x, y, w, h, cx, cy, vert_lead);
	}

	int find_xcursor(const char* text, int textlen, int cx) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text, textlen)) {
			auto utf8 = font_bytes_to_utf8(text, textlen);
			return tc->find_xcursor(
					utf8.c_str(), static_cast<int>(utf8.size()), cx);
		}
		return base_font->find_xcursor(text, textlen, cx);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			int width, unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->paint_text_fixedwidth(win, utf8.c_str(), xoff, yoff, width, trans);
		}
		return base_font->paint_text_fixedwidth(win, text, xoff, yoff, width, trans);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			int width, unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text, textlen)) {
			auto utf8 = font_bytes_to_utf8(text, textlen);
			return tc->paint_text_fixedwidth(
					win, utf8.c_str(), static_cast<int>(utf8.size()), xoff, yoff, width, trans);
		}
		return base_font->paint_text_fixedwidth(win, text, textlen, xoff, yoff, width, trans);
	}

	int paint_text_box_fixedwidth(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int char_width, int vert_lead, int pbreak,
			unsigned char* trans) override {
		auto tc = fontManager.get_font("ttf/tc");
		if (tc && has_high_byte(text)) {
			auto utf8 = font_bytes_to_utf8(text);
			return tc->paint_text_box_fixedwidth(
					win, utf8.c_str(), x, y, w, h, char_width, vert_lead, pbreak, trans);
		}
		return base_font->paint_text_box_fixedwidth(
				win, text, x, y, w, h, char_width, vert_lead, pbreak, trans);
	}
};


/**
 *  Wraps a TTF font to render ALL text (not just CJK) via the TTF path,
 *  converting font bytes back to UTF-8 before each call. Every character,
 *  including ASCII, is routed through the TrueType renderer.
 */
class TtfFullFont : public Font {
	std::shared_ptr<Font> ttfont;
	int                   tt_ver_lead;

public:
	TtfFullFont(std::shared_ptr<Font> tc)
			: Font(), ttfont(std::move(tc)), tt_ver_lead(ttfont->get_ver_lead()) {
	}

	int get_ver_lead() const {
		return tt_ver_lead;
	}

	int paint_text(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->paint_text(win, utf8.c_str(), xoff, yoff, trans);
	}

	int paint_text(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text, textlen);
		return ttfont->paint_text(
				win, utf8.c_str(), static_cast<int>(utf8.size()), xoff, yoff, trans);
	}

	int paint_text_box(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int vert_lead, bool pbreak, bool center, Cursor_info* cursor,
			unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->paint_text_box(
				win, utf8.c_str(), x, y, w, h, vert_lead, pbreak, center, cursor, trans);
	}

	int get_text_width(const char* text) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->get_text_width(utf8.c_str());
	}

	int get_text_width(const char* text, int textlen) override {
		auto utf8 = font_bytes_to_utf8(text, textlen);
		return ttfont->get_text_width(utf8.c_str(), static_cast<int>(utf8.size()));
	}

	int get_text_height() override {
		return ttfont->get_text_height();
	}

	int get_text_baseline() override {
		return ttfont->get_text_baseline();
	}

	void get_text_box_dims(
			const char* text, int& width, int& height, int vert_lead) override {
		auto utf8 = font_bytes_to_utf8(text);
		ttfont->get_text_box_dims(utf8.c_str(), width, height, vert_lead);
	}

	int find_cursor(
			const char* text, int x, int y, int w, int h, int cx, int cy,
			int vert_lead) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->find_cursor(utf8.c_str(), x, y, w, h, cx, cy, vert_lead);
	}

	int find_xcursor(const char* text, int textlen, int cx) override {
		auto utf8 = font_bytes_to_utf8(text, textlen);
		return ttfont->find_xcursor(
				utf8.c_str(), static_cast<int>(utf8.size()), cx);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			int width, unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->paint_text_fixedwidth(win, utf8.c_str(), xoff, yoff, width, trans);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			int width, unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text, textlen);
		return ttfont->paint_text_fixedwidth(
				win, utf8.c_str(), static_cast<int>(utf8.size()), xoff, yoff, width, trans);
	}

	int paint_text_box_fixedwidth(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int char_width, int vert_lead, int pbreak,
			unsigned char* trans) override {
		auto utf8 = font_bytes_to_utf8(text);
		return ttfont->paint_text_box_fixedwidth(
				win, utf8.c_str(), x, y, w, h, char_width, vert_lead, pbreak, trans);
	}
};


std::shared_ptr<Font> FontManager::get_font(const char* name) {
	auto it = fonts.find(name);
	if (it == fonts.end() || !it->second) {
		return nullptr;
	}
	// Do NOT wrap the TC font itself (would cause recursion).
	if (std::strcmp(name, "ttf/tc") == 0) {
		return it->second;
	}
	// If the TC font is loaded, wrap every named-font lookup with a
	// CJKRoutingFont so that strings containing CJK font bytes (>= 0x80)
	// are automatically routed through the TrueType rendering path.
	// Pure-ASCII text passes through to the bitmap font untouched.
	if (fonts.find("ttf/tc") != fonts.end()) {
		return std::make_shared<CJKRoutingFont>(it->second);
	}
	return it->second;
}

void FontManager::reset() {
	fonts.clear();
}
/*
 *  Wrapper that adapts TtFont to the Font interface.
 *  Used by FontManager to store TTF fonts alongside bitmap fonts.
 */
class TtFontWrapper : public Font {
private:
	TtFont               ttfont;
	unsigned char        fg_color = 15;    // Default: palette white.
	int                  sh_color = 0;     // Black outline.

public:
	TtFontWrapper() = default;

	void set_text_color(unsigned char fg, int sh) override {
		fg_color = fg;
		sh_color = sh;
		ttfont.set_color(fg, sh);
	}

	unsigned char get_text_pixel() const override {
		return fg_color;
	}

	int load(const char* font_path, int pixel_size, int hlead = 0, int vlead = 1) {
		return ttfont.load(font_path, pixel_size, hlead, vlead);
	}

	int paint_text(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			unsigned char* trans) override {
		return ttfont.paint_text(win, text, xoff, yoff, fg_color, sh_color, trans);
	}

	int paint_text(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			unsigned char* trans) override {
		return ttfont.paint_text(win, text, textlen, xoff, yoff, fg_color, sh_color, trans);
	}

	int paint_text_box(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int vert_lead, bool pbreak, bool center,
			Cursor_info* cursor, unsigned char* trans) override {
		return ttfont.paint_text_box(
				win, text, x, y, w, h, vert_lead, pbreak, center,
				cursor, fg_color, sh_color, trans);
	}

	int get_text_width(const char* text) override {
		return ttfont.get_text_width(text);
	}

	int get_text_width(const char* text, int textlen) override {
		return ttfont.get_text_width(text, textlen);
	}

	int get_text_height() override {
		return ttfont.get_text_height();
	}

	int get_text_baseline() override {
		return ttfont.get_text_baseline();
	}

	void get_text_box_dims(const char* text, int& width, int& height, int vert_lead) override {
		ttfont.get_text_box_dims(text, width, height, vert_lead);
	}

	int find_cursor(
			const char* text, int x, int y, int w, int h,
			int cx, int cy, int vert_lead) override {
		return ttfont.find_cursor(text, x, y, w, h, cx, cy, vert_lead);
	}

	int find_xcursor(const char* text, int textlen, int cx) override {
		return ttfont.find_xcursor(text, textlen, cx);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int xoff, int yoff,
			int width, unsigned char* trans) override {
		ttfont.set_color(fg_color, sh_color);
		return ttfont.paint_text_fixedwidth(win, text, xoff, yoff, width, trans);
	}

	int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
			int width, unsigned char* trans) override {
		ttfont.set_color(fg_color, sh_color);
		return ttfont.paint_text_fixedwidth(win, text, textlen, xoff, yoff, width, trans);
	}

	int paint_text_box_fixedwidth(
			Image_buffer8* win, const char* text, int x, int y, int w, int h,
			int char_width, int vert_lead, int pbreak,
			unsigned char* trans) override {
		ttfont.set_color(fg_color, sh_color);
		return ttfont.paint_text_box_fixedwidth(
				win, text, x, y, w, h, char_width, vert_lead, pbreak, trans);
	}

	bool is_loaded() const {
		return ttfont.is_loaded();
	}
};

/**
 *  Loads a TrueType font and registers it by name in the font manager.
 *  The font is stored via a TtFontWrapper that adapts TtFont to the Font
 *  interface, allowing it to coexist with bitmap fonts in the same map.
 *  @param name       Name to give to this font.
 *  @param ttf_path   Path to the TTF file.
 *  @param pixel_size Desired pixel height.
 *  @param hlead      Horizontal lead (extra spacing between chars).
 *  @param vlead      Vertical lead (extra spacing between lines).
 *  @return Shared pointer to the registered Font, or nullptr on failure.
 */
	std::shared_ptr<Font> FontManager::add_ttf_font(
		const char* name, const char* ttf_path, int pixel_size, int hlead, int vlead) {
	remove_font(name);

	auto wrapper = std::make_shared<TtFontWrapper>();
	if (wrapper->load(ttf_path, pixel_size, hlead, vlead) != 0) {
		return nullptr;
	}

	fonts[name] = wrapper;
	return wrapper;
}

/**
 *  Loads a TrueType font and registers it under @p name with full-text
 *  routing via TtfFullFont. Every paint/get_text_width call converts font
 *  bytes to UTF-8 before delegating to the TTF renderer, so ALL text
 *  (ASCII + CJK) appears in the TrueType face.
 *
 *  Unlike add_ttf_font(), which stores a raw TtFontWrapper that expects
 *  UTF-8 input, this method stores a TtfFullFont wrapper that first
 *  converts Exult font bytes to UTF-8 via font_bytes_to_utf8().
 *
 *  @param name       Name to give to this font (e.g. "NORMAL_FONT").
 *  @param ttf_path   Path to the TTF file.
 *  @param pixel_size Desired pixel height.
 *  @param hlead      Horizontal lead (extra spacing between chars).
 *  @param vlead      Vertical lead (extra spacing between lines).
 *  @return Shared pointer to the registered Font, or nullptr on failure.
 */
	std::shared_ptr<Font> FontManager::add_ttf_full_font(
		const char* name, const char* ttf_path, int pixel_size, int hlead, int vlead) {
	// First register a plain TTF wrapper so we have a Font object to wrap.
	auto tc = add_ttf_font("ttf/_full", ttf_path, pixel_size, hlead, vlead);
	if (!tc) {
		return nullptr;
	}

	// Replace it with a TtfFullFont wrapper that converts font bytes → UTF-8.
	auto full = std::make_shared<TtfFullFont>(std::move(tc));
	remove_font("ttf/_full");
	remove_font(name);
	fonts[name] = full;
	return full;
}

std::shared_ptr<Font> wrap_font_for_cjk(std::shared_ptr<Font> base) {
	if (!base) {
		return nullptr;
	}
	// Idempotent: don't wrap an already-wrapped font — doing so would
	// cause the inner wrapper to treat UTF-8 output as font bytes and
	// double-translate them, producing garbage.
	if (std::dynamic_pointer_cast<CJKRoutingFont>(base)) {
		return base;
	}
	return std::make_shared<CJKRoutingFont>(std::move(base));
}
