/*
 *  Copyright (C) 2026  The Exult Team
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

#ifndef USECODE_TEXT_TRANSLATOR_H
#define USECODE_TEXT_TRANSLATOR_H

#include <string>
#include <unordered_map>

/*
 *  Runtime string replacement for usecode dialogue text.
 *
 *  Loads a translation dictionary from <PATCH>/usecode_translations.txt
 *  and performs O(1) lookups to replace English source strings with their
 *  Traditional Chinese (or other language) translations before rendering.
 *
 *  File format (tab-separated, UTF-8):
 *      English source text<TAB>Translated text
 *      Lines starting with '#' are comments.
 *      Empty lines are skipped.
 *
 *  The hook is installed via translate_usecode_text() in font_map.cc.
 */

class UsecodeTextTranslator {
public:
	UsecodeTextTranslator() = default;

	// Load the translation dictionary from a file. Returns true on success.
	// If the file does not exist, silently returns false (no translations).
	bool load(const std::string& filepath);

	// Look up a string and replace it with its translation if found.
	// Returns true if a translation was applied, false if not found.
	bool translate(std::string& text) const;

	// Returns the number of loaded translation entries.
	size_t size() const {
		return dict.size();
	}

private:
	std::unordered_map<std::string, std::string> dict;
};

// Global singleton access. Initialized once at startup.
UsecodeTextTranslator& get_usecode_text_translator();

#endif    // USECODE_TEXT_TRANSLATOR_H
