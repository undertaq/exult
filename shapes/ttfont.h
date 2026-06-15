/*
 *  TtFont.h - FreeType-based TrueType font rendering.
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

#ifndef TT_FONT_H
#define TT_FONT_H

#include "font.h"

#include <memory>
#include <string>

struct File_spec;
class Image_buffer8;

#ifdef HAVE_FREETYPE2
#	include <ft2build.h>
#	ifdef __GNUC__
#		pragma GCC diagnostic push
#		pragma GCC diagnostic ignored "-Wold-style-cast"
#		pragma GCC diagnostic ignored "-Wcast-qual"
#	endif
#	include FT_FREETYPE_H
#	ifdef __GNUC__
#		pragma GCC diagnostic pop
#	endif
#endif

/*
 *  A TrueType font using FreeType for direct rendering.
 *  Bypasses the 256-frame limit of the traditional Font class.
 */
class TtFont {
private:
#ifdef HAVE_FREETYPE2
	FT_Library ft_library = nullptr;
	FT_Face    ft_face    = nullptr;
#endif
	int        pixel_height = 0;
	int        hor_lead     = 0;
	int        ver_lead     = 0;
	int        highest      = 0;    // Max extent above baseline
	int        lowest       = 0;    // Max extent below baseline
	std::string font_file;

	void calc_highlow();
	void cleanup();

public:
	TtFont() = default;
	/**
	 *  Loads a TrueType font from a File_spec.
	 *  @param fname0   File specification for the TTF file.
	 *  @param pixel_ht Desired height in pixels.
	 *  @param hlead    Horizontal lead (extra spacing between chars).
	 *  @param vlead    Vertical lead (extra spacing between lines).
	 *  @return 0 on success, -1 on failure.
	 */
	int load(const File_spec& fname0, int pixel_ht, int hlead = 0, int vlead = 1);

	/**
	 *  Loads a TrueType font from a file path.
	 *  @param font_path Path to the TTF file.
	 *  @param pixel_ht  Desired height in pixels.
	 *  @param hlead     Horizontal lead (extra spacing between chars).
	 *  @param vlead     Vertical lead (extra spacing between lines).
	 *  @return 0 on success, -1 on failure.
	 */
	int load(const char* font_path, int pixel_ht, int hlead = 0, int vlead = 1);

	~TtFont() noexcept;

	// Non-copyable, movable
	TtFont(const TtFont&)            = delete;
	TtFont& operator=(const TtFont&) = delete;
	TtFont(TtFont&& other) noexcept
			:
#ifdef HAVE_FREETYPE2
			ft_library(other.ft_library),
			ft_face(other.ft_face),
#endif
			pixel_height(other.pixel_height),
			hor_lead(other.hor_lead),
			ver_lead(other.ver_lead),
			highest(other.highest),
			lowest(other.lowest),
			font_file(std::move(other.font_file)) {
#ifdef HAVE_FREETYPE2
		other.ft_library = nullptr;
		other.ft_face    = nullptr;
#endif
		other.pixel_height = 0;
		other.hor_lead     = 0;
		other.ver_lead     = 0;
		other.highest      = 0;
		other.lowest       = 0;
	}
	TtFont& operator=(TtFont&& other) noexcept {
		if (this != &other) {
			cleanup();
#ifdef HAVE_FREETYPE2
			ft_library       = other.ft_library;
			ft_face          = other.ft_face;
			other.ft_library = nullptr;
			other.ft_face    = nullptr;
#endif
			pixel_height      = other.pixel_height;
			hor_lead          = other.hor_lead;
			ver_lead          = other.ver_lead;
			highest           = other.highest;
			lowest            = other.lowest;
			font_file         = std::move(other.font_file);
			other.pixel_height = 0;
			other.hor_lead     = 0;
			other.ver_lead     = 0;
			other.highest      = 0;
			other.lowest       = 0;
		}
		return *this;
	}

	// Text rendering:
	/**
	 *  Draw text at a given location.
	 *  @param win      Buffer to paint in.
	 *  @param text     UTF-8 encoded text to draw.
	 *  @param xoff     X offset (left).
	 *  @param yoff     Y offset (top).
	 *  @param fg_color Foreground color index (default 255).
	 *  @param sh_color Shadow color index (default -1, no shadow).
	 *  @param trans    Optional translation table.
	 *  @return Width in pixels of what was drawn.
	 */
	int paint_text(Image_buffer8* win, const char* text, int xoff, int yoff,
				   unsigned char fg_color = 255, int sh_color = -1,
				   unsigned char* trans = nullptr);

	/**
	 *  Draw text with explicit length.
	 *  @param win      Buffer to paint in.
	 *  @param text     UTF-8 encoded text to draw.
	 *  @param textlen  Length of text in bytes.
	 *  @param xoff     X offset (left).
	 *  @param yoff     Y offset (top).
	 *  @param fg_color Foreground color index (default 255).
	 *  @param sh_color Shadow color index (default -1, no shadow).
	 *  @param trans    Optional translation table.
	 *  @return Width in pixels of what was drawn.
	 */
	int paint_text(Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
				   unsigned char fg_color = 255, int sh_color = -1,
				   unsigned char* trans = nullptr);

	/**
	 *  Draw text within a rectangular area with word wrapping.
	 *  @param win       Buffer to paint in.
	 *  @param text      UTF-8 encoded text to draw.
	 *  @param x         Left of box.
	 *  @param y         Top of box.
	 *  @param w         Width of box.
	 *  @param h         Height of box.
	 *  @param vert_lead Extra vertical spacing between lines.
	 *  @param pbreak    Break at punctuation if out of room.
	 *  @param center    Center each line.
	 *  @param cursor    Optional cursor info output.
	 *  @param fg_color  Foreground color index (default 255).
	 *  @param sh_color  Shadow color index (default -1, no shadow).
	 *  @param trans     Optional translation table.
	 *  @return Height of text painted, or negative offset if out of room.
	 */
	int paint_text_box(Image_buffer8* win, const char* text, int x, int y, int w, int h,
					   int vert_lead = 0, bool pbreak = false, bool center = false,
					   struct Cursor_info* cursor = nullptr,
					   unsigned char fg_color = 255, int sh_color = -1,
					   unsigned char* trans = nullptr);

	/**
	 *  Draw text at a fixed character width.
	 *  @param win   Buffer to paint in.
	 *  @param text  UTF-8 encoded text to draw.
	 *  @param xoff  X offset (left).
	 *  @param yoff  Y offset (top).
	 *  @param width Fixed width for each character.
	 *  @param trans Optional translation table.
	 *  @return Width in pixels of what was drawn.
	 */
	int paint_text_fixedwidth(Image_buffer8* win, const char* text, int xoff, int yoff,
							  int width, unsigned char* trans = nullptr);

	/**
	 *  Draw text with explicit length at a fixed character width.
	 *  @param win     Buffer to paint in.
	 *  @param text    UTF-8 encoded text to draw.
	 *  @param textlen Length of text in bytes.
	 *  @param xoff    X offset (left).
	 *  @param yoff    Y offset (top).
	 *  @param width   Fixed width for each character.
	 *  @param trans   Optional translation table.
	 *  @return Width in pixels of what was drawn.
	 */
	int paint_text_fixedwidth(Image_buffer8* win, const char* text, int textlen, int xoff, int yoff,
							  int width, unsigned char* trans = nullptr);

	/**
	 *  Draw text within a rectangular area at a fixed character width.
	 *  @param win        Buffer to paint in.
	 *  @param text       UTF-8 encoded text to draw.
	 *  @param x          Left of box.
	 *  @param y          Top of box.
	 *  @param w          Width of box.
	 *  @param h          Height of box.
	 *  @param char_width Fixed width for each character.
	 *  @param vert_lead  Extra vertical spacing between lines.
	 *  @param pbreak     Break at punctuation if out of room.
	 *  @param trans      Optional translation table.
	 *  @return Height of text painted, or negative offset if out of room.
	 */
	int paint_text_box_fixedwidth(Image_buffer8* win, const char* text, int x, int y, int w, int h,
								  int char_width, int vert_lead = 0, int pbreak = 0,
								  unsigned char* trans = nullptr);

	// Get text width.
	/**
	 *  Get the width in pixels of a UTF-8 string.
	 *  @param text UTF-8 encoded text.
	 *  @return Width in pixels.
	 */
	int get_text_width(const char* text);

	/**
	 *  Get the width in pixels of a UTF-8 string with explicit length.
	 *  @param text     UTF-8 encoded text.
	 *  @param textlen  Length of text in bytes.
	 *  @return Width in pixels.
	 */
	int get_text_width(const char* text, int textlen);

	// Get dimensions of text box for multiline string
	void get_text_box_dims(const char* text, int& width, int& height, int vert_lead = 0);

	// Get text height, baseline, and vertical lead.
	int get_text_height() const {
		return highest + lowest + 1;
	}
	int get_text_baseline() const {
		return highest;
	}
	int get_ver_lead() const {
		return ver_lead;
	}

	// Find cursor location within text, given (x,y) screen coords.
	int find_cursor(const char* text, int x, int y, int w, int h, int cx, int cy, int vert_lead);
	int find_xcursor(const char* text, int textlen, int cx);

	// Convenience methods matching Font API
	int draw_text(Image_buffer8* win, int x, int y, const char* s,
				  unsigned char fg_color = 255, int sh_color = -1,
				  unsigned char* trans = nullptr) {
		return paint_text(win, s, x, y, fg_color, sh_color, trans);
	}

	int draw_text_box(Image_buffer8* win, int x, int y, int w, int h, const char* s,
					  unsigned char fg_color = 255, int sh_color = -1,
					  unsigned char* trans = nullptr) {
		return paint_text_box(win, s, x, y, w, h, 0, false, false, nullptr, fg_color, sh_color, trans);
	}

	int center_text(Image_buffer8* win, int x, int y, const char* s,
					unsigned char fg_color = 255, int sh_color = -1,
					unsigned char* trans = nullptr);

	bool is_loaded() const {
#ifdef HAVE_FREETYPE2
		return ft_face != nullptr;
#else
		return false;
#endif
	}
};

#endif /* TT_FONT_H */
