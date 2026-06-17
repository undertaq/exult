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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "usecode_text_translator.h"

#include "fnames.h"
#include "utils.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using std::cerr;
using std::endl;
using std::string;

/*
 *  Load the translation dictionary from a UTF-8 text file.
 *
 *  Format (tab-separated):
 *      English source text<TAB>Translated text
 *
 *  Lines starting with '#' are comments. Empty lines are skipped.
 *  If the file does not exist, we silently return false (no translations).
 *
 *  TODO: Handle %s, %d, %i, %x, %c, %% format specifiers — the
 *  translation should preserve them in the correct order.
 */

bool UsecodeTextTranslator::load(const string& filepath) {
	// Map Exult path (<PATCH>/...) to system path.
	string syspath;
	try {
		syspath = get_system_path(filepath);
	} catch (const std::exception& e) {
		cerr << "Translator: Failed to resolve path '" << filepath << "': " << e.what() << endl;
		return false;
	}

	std::ifstream file(syspath, std::ios::binary);
	if (!file.good()) {
		// File doesn't exist — no translations, that's OK.
		return false;
	}

	dict.clear();

	string line;
	string source;
	string translation;
	int    lineNum    = 0;
	int    loadCount  = 0;
	int    errorCount = 0;

	while (std::getline(file, line)) {
		lineNum++;

		// Strip trailing CR (Windows line endings).
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		// Skip empty lines and comments.
		if (line.empty() || line[0] == '#') {
			continue;
		}

		// Find the tab separator.
		auto tabPos = line.find('\t');
		if (tabPos == string::npos) {
			// No tab — skip malformed line.
			errorCount++;
			if (errorCount <= 5) {
				cerr << "Translator: Line " << lineNum << " missing tab separator, skipping." << endl;
			}
			continue;
		}

		source      = line.substr(0, tabPos);
		translation = line.substr(tabPos + 1);

		if (source.empty()) {
			errorCount++;
			if (errorCount <= 5) {
				cerr << "Translator: Line " << lineNum << " has empty source text, skipping." << endl;
			}
			continue;
		}

		// Insert or update.
		dict[source] = translation;
		loadCount++;
	}

	if (errorCount > 5) {
		cerr << "Translator: ... and " << (errorCount - 5) << " more errors." << endl;
	}

	cerr << "Translator: Loaded " << loadCount << " translation(s) from '" << syspath << "'"
		 << (errorCount ? " (" + std::to_string(errorCount) + " errors)" : "") << endl;

	return loadCount > 0;
}

/*
 *  Look up a string and replace it with its translation if found.
 *
 *  This performs an exact match of the entire string against the dictionary.
 *  Returns true if a replacement was made, false if not found.
 */

bool UsecodeTextTranslator::translate(string& text) const {
	if (dict.empty() || text.empty()) {
		return false;
	}

	auto it = dict.find(text);
	if (it != dict.end()) {
		text = it->second;
		return true;
	}

	return false;
}

/*
 *  Global singleton access.
 */

UsecodeTextTranslator& get_usecode_text_translator() {
	static UsecodeTextTranslator instance;
	return instance;
}
