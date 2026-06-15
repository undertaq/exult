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

#ifndef FONT_H
#define FONT_H

#include "hash_utils.h"

#include <memory>

class Image_buffer8;
class Shape_file;
class IDataSource;
struct File_spec;
class U7multiobject;

/*
 *  Cursor info. filled in by paint_text_box.
 */
struct Cursor_info {
	int offset;    // Loc. within text.
	int x, y;      // Loc. of top-left of cursor in win.
	int line;      // Line # of cursor.
	int nlines;    // Total # lines printed.

	void set_found(int cx, int cy, int l) {
		x    = cx;
		y    = cy;
		line = l;
	}
};

/*
 *  A single font:
 */
class Font {
private:
	int                         hor_lead = 0;
	int                         ver_lead = 0;
	std::unique_ptr<Shape_file> font_shapes;
	int                         highest = 0, lowest = 0;

	void calc_highlow();
	void clean_up();
	int  load_internal(IDataSource& data, int hlead, int vlead);

public:
	Font();
	Font(const File_spec& fname0, int index, int hlead = 0, int vlead = 1);
	Font(const File_spec& fname0, const File_spec& fname1, int index, int hlead = 0, int vlead = 1);
	Font(Font&&) noexcept            = default;
	Font& operator=(Font&&) noexcept = default;
	virtual ~Font() noexcept         = default;
	/**
	 *  Loads a font from a File_spec.
	 *  @param fname0   First file spec.
	 *  @param index    Number of font to load.
	 *  @param hleah    Horizontal lead of the font.
	 *  @param vleah    Vertical lead of the font.
	 *  @return 0 on success
	 */
	int load(const File_spec& fname0, int index, int hlead = 0, int vlead = 1);
	/**
	 *  Loads a font from a File_spec.
	 *  @param fname0   First file spec.
	 *  @param fname1   Second file spec.
	 *  @param index    Number of font to load.
	 *  @param hleah    Horizontal lead of the font.
	 *  @param vleah    Vertical lead of the font.
	 *  @return 0 on success
	 */
	int load(const File_spec& fname0, const File_spec& fname1, int index, int hlead = 0, int vlead = 1);
	// Text rendering:
	virtual int paint_text_box(
			Image_buffer8* win, const char* text, int x, int y, int w, int h, int vert_lead = 0, bool pbreak = false,
			bool center = false, Cursor_info* cursor = nullptr, unsigned char* trans = nullptr);
	virtual int paint_text(Image_buffer8* win, const char* text, int xoff, int yoff, unsigned char* trans = nullptr);

	int paint_text_right_aligned(Image_buffer8* win, const char* text, int xoff, int yoff, unsigned char* trans = nullptr) {
		return paint_text(win, text, xoff - get_text_width(text), yoff, trans);
	}

	virtual int paint_text(Image_buffer8* win, const char* text, int textlen, int xoff, int yoff, unsigned char* trans = nullptr);
	virtual int paint_text_box_fixedwidth(
			Image_buffer8* win, const char* text, int x, int y, int w, int h, int char_width, int vert_lead = 0, int pbreak = 0,
			unsigned char* trans = nullptr);
	virtual int paint_text_fixedwidth(Image_buffer8* win, const char* text, int xoff, int yoff, int width, unsigned char* trans = nullptr);
	virtual int paint_text_fixedwidth(
			Image_buffer8* win, const char* text, int textlen, int xoff, int yoff, int width, unsigned char* trans = nullptr);
	// Get text width.
	virtual int get_text_width(const char* text);
	virtual int get_text_width(const char* text, int textlen);
	// Get dimensions of text box for multiline string
	virtual void get_text_box_dims(const char* text, int& width, int& height, int vert_lead = 0);
	// Get text height, baseline, and vertical lead.
	virtual int get_text_height();
	virtual int get_text_baseline();

	virtual int get_ver_lead() const {
		return ver_lead;
	}

	virtual int find_cursor(const char* text, int x, int y, int w, int h, int cx, int cy, int vert_lead);
	virtual int find_xcursor(const char* text, int textlen, int cx);

	int draw_text(Image_buffer8* win, int x, int y, const char* s, unsigned char* trans = nullptr) {
		return paint_text(win, s, x, y, trans);
	}

	int draw_text_box(Image_buffer8* win, int x, int y, int w, int h, const char* s, unsigned char* trans = nullptr) {
		return paint_text_box(win, s, x, y, w, h, 0, false, false, nullptr, trans);
	}

	int center_text(Image_buffer8* iwin, int x, int y, const char* s, unsigned char* trans = nullptr);
};

/*
 *  Manage a list of fonts by name.
 */
class FontManager {
private:
	std::unordered_map<const char*, std::shared_ptr<Font>, hashstr, eqstr> fonts;

public:
	~FontManager();
	void add_font(const char* name, const File_spec& fname0, int index, int hlead = 0, int vlead = 1);
	void add_font(const char* name, const File_spec& fname0, const File_spec& fname1, int index, int hlead = 0, int vlead = 1);
	void remove_font(const char* name);
	std::shared_ptr<Font> get_font(const char* name);

	/**
	 *  Loads a TrueType font and registers it by name.
	 *  @param name      Name to give to this font.
	 *  @param ttf_path  Path to the TTF file.
	 *  @param pixel_size Desired pixel height.
	 *  @param hlead     Horizontal lead (extra spacing between chars).
	 *  @param vlead     Vertical lead (extra spacing between lines).
	 *  @return Shared pointer to the registered Font, or nullptr on failure.
	 */
	std::shared_ptr<Font> add_ttf_font(const char* name, const char* ttf_path, int pixel_size, int hlead = 0, int vlead = 1);

	/**
	 *  Loads a TrueType font with full-text routing under @p name.
	 *  Every paint/get_text_width call converts font bytes to UTF-8 first,
	 *  so ALL text (ASCII + CJK) is rendered via the TrueType face.
	 *  @param name       Name to give to this font (e.g. "NORMAL_FONT").
	 *  @param ttf_path   Path to the TTF file.
	 *  @param pixel_size Desired pixel height.
	 *  @param hlead      Horizontal lead (extra spacing between chars).
	 *  @param vlead      Vertical lead (extra spacing between lines).
	 *  @return Shared pointer to the registered Font, or nullptr on failure.
	 */
	std::shared_ptr<Font> add_ttf_full_font(const char* name, const char* ttf_path, int pixel_size, int hlead = 0, int vlead = 1);

	void reset();
};

extern FontManager fontManager;

class CJKRoutingFont;    // Forward decl for wrap_font_for_cjk idempotency check.

/**
 *  Wrap a Font with a CJK routing layer.
 *  When the TC font (registered as "ttf/tc") is loaded and the text
 *  contains font bytes in the CJK range (>= 0x80), the wrapper routes
 *  rendering and measurement calls to the TC font after reverse-translating
 *  font bytes back to UTF-8. Pure-ASCII text bypasses the TC font for
 *  zero performance overhead.
 *  This function is idempotent: if @p base is already a CJKRoutingFont,
 *  it is returned unchanged.
 *  @param base  The font to wrap (e.g. a bitmap Font).
 *  @return A shared_ptr to a CJKRoutingFont wrapper.
 */
std::shared_ptr<Font> wrap_font_for_cjk(std::shared_ptr<Font> base);

#endif
