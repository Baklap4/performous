#include "songparser.hh"
#include "unicode.hh"
#include "util.hh"

#include <boost/algorithm/string.hpp>

#include <cmath>
#include <fstream>
#include <regex>


namespace SongParserUtil {
	void assign(int& var, std::string const& str) {
		try {
			var = std::stoi(str);
		}
		catch (...) {
			throw std::runtime_error("\"" + str + "\" is not valid integer value");
		}
	}
	void assign(unsigned& var, std::string const& str) {
		try {
			var = stou(str);
		}
		catch (...) {
			throw std::runtime_error("\"" + str + "\" is not valid unsigned integer value");
		}
	}
	void assign(float& var, std::string str) {
		std::replace(str.begin(), str.end(), ',', '.');  // Fix decimal separators
		try {
			var = std::stof(str);
		}
		catch (...) {
			throw std::runtime_error("\"" + str + "\" is not valid floating point value");
		}
	}
	void assign(double& var, std::string str) {
		std::replace(str.begin(), str.end(), ',', '.');  // Fix decimal separators
		try {
			var = std::stod(str);
		}
		catch (...) {
			throw std::runtime_error("\"" + str + "\" is not valid floating point value");
		}
	}
	void assign(bool& var, std::string const& str) {
		auto lowerStr = UnicodeUtil::toLower(str);
		auto is_yes = lowerStr == "yes" || str == "1";
		auto is_no = lowerStr == "no" || str == "0";
		if (!is_yes && !is_no) { throw std::runtime_error("Invalid boolean value: " + str); }
		var = is_yes;
	}
	void eraseLast(std::string& s, char ch) {
		if (!s.empty() && (*s.rbegin() == ch)) { s.erase(s.size() - 1); }
	}
}

SongParser::SongParser(Song& s) : m_song(s) {
	try {
		// Read the file, determine the type and do some initial validation checks
		std::ifstream f(s.filename.string(), std::ios::binary);
		if (!f.is_open()) {
			throw SongParserException(s, "Could not open song file", 0);
		}
		m_ss << f.rdbuf();
		size_t size = m_ss.str().length();
		if ((size < 10) || (size > 100000)) {
			throw SongParserException(s, "Does not look like a song file (wrong size)", 1, true);
		}
		std::string ss = UnicodeUtil::convertToUTF8(m_ss.str(), s.filename.string());
		if (!isText(ss)) {
			throw SongParserException(s, "Does not look like a song file (binary)", 1, true);
		}
		// Convert m_ss; filename supplied for possible warning messages
		if (xmlCheck(m_ss.str())) {
			s.type = Song::Type::XML; // XMLPP should deal with encoding so we don't have to.
			ss = m_ss.str();
		}
		else {
			// For determining song type, SM has to come first as it's very similar in structure to the TXT format and thus it's possible for SM songs to be erroneously categorized as TXT songs.
			if (smCheck(ss)) {
				s.type = Song::Type::SM;
			} else if (txtCheck(ss)) {
				s.type = Song::Type::TXT;
			} else if (iniCheck(ss)) {
				s.type = Song::Type::INI;
			} else {
				throw SongParserException(s, "Does not look like a song file (wrong header)", 1, true);
			}
			m_ss.str(ss);
		}
		// Header already parsed?
		if (s.loadStatus == Song::LoadStatus::HEADER) {
			if (!s.m_bpms.empty()) {
				float bpm = static_cast<float>(15.0 / s.m_bpms.front().step);
				s.m_bpms.clear();
				addBPM(0, bpm);
			}
			if (s.type == Song::Type::TXT) txtParse();
			else if (s.type == Song::Type::INI) midParse();  // INI doesn't contain notes, parse those from MIDI
			else if (s.type == Song::Type::XML) xmlParse();
			else if (s.type == Song::Type::SM) smParse();
			finalize();  // Do some adjusting to the notes
			s.loadStatus = Song::LoadStatus::FULL;
			return;
		}
		// Parse only header to speed up loading and conserve memory
		if (s.type == Song::Type::TXT) txtParseHeader();
		else if (s.type == Song::Type::INI) iniParseHeader();
		else if (s.type == Song::Type::XML) xmlParseHeader();
		else if (s.type == Song::Type::SM) {
			smParseHeader(); s.dropNotes();  // Hack: drop notes here (load again when playing the song)
		}

		guessFiles();
		if (!m_song.midifilename.empty()) { 
			midParseHeader(); 
		}
		if (s.loadStatus != Song::LoadStatus::ERROR) {
			s.loadStatus = Song::LoadStatus::HEADER;
		}
	}
	catch (SongParserException&) {
		throw;
	}
	catch (std::runtime_error& e) {
		throw SongParserException(m_song, e.what(), m_linenum);
	}
	catch (std::exception& e) {
		throw SongParserException(m_song, "Internal error: " + std::string(e.what()), m_linenum);
	}
}

void SongParser::guessFiles() {
	// List of fields containing filenames, and auto-matching regexps, in order of priority
	const std::vector<std::pair<fs::path*, char const*> > fields = {
		{ &m_song.cover, R"((cover|album|label|banner|bn|\[co\])\.(png|jpeg|jpg|svg)$)" },
		{ &m_song.background, R"((background|bg|\[bg\])\.(png|jpeg|jpg|svg)$)" },
		{ &m_song.cover, R"(\.(png|jpeg|jpg|svg)$)" },
		{ &m_song.background, R"(\.(png|jpeg|jpg|svg)$)" },
		{ &m_song.video, R"(\.(avi|mpg|mpeg|flv|mov|mp4|mkv|m4v|webm)$)" },
		{ &m_song.midifilename, R"(^notes\.mid$)" },
		{ &m_song.midifilename, R"(\.mid$)" },
		{ &m_song.music[TrackName::PREVIEW], R"(^preview\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::GUITAR], R"(^guitar\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::BASS], R"(^(bass|rhythm)\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::DRUMS], R"(^drums(_1)?\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::DRUMS_SNARE], R"(^drums_2\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::DRUMS_CYMBALS], R"(^drums_3\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::DRUMS_TOMS], R"(^drums_4\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::KEYBOARD], R"(^key(board|s)\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::GUITAR_COOP], R"(^guitar_coop\.(mp3|m4a|ogg|opus|aac)$)"},
		{ &m_song.music[TrackName::GUITAR_RHYTHM], R"(^guitar_rhythm\.(mp3|m4a|ogg|opus|aac)$)"},
		{ &m_song.music[TrackName::VOCAL_LEAD], R"(^vocals_1\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::VOCAL_LEAD], R"(^vocals\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::VOCAL_BACKING], R"(^vocals_2\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::BGMUSIC], R"(^song(s)?\.(mp3|m4a|ogg|opus|aac)$)" },
		{ &m_song.music[TrackName::BGMUSIC], R"(\.(mp3|m4a|ogg|opus|aac)$)" },
	};

	std::string logMissing, logFound;

	// Run checks, remove bogus values and construct regexps
	bool missing = false;
	for (auto const& p : fields) {
		const fs::path& file = *p.first;
		if (file.empty()) {
			missing = true; 
		} else if (!is_regular_file(file)) {
			logMissing += "  " + file.filename().string();
			missing = true; 
		}
	}

	if (!missing) {
		return;	// All OK!
	}

	std::clog << "songparse/notice: Missing files for " << m_song.title << std::endl;

	// Try matching all files in song folder with any field
	std::set<fs::path> files(fs::directory_iterator{ m_song.path }, fs::directory_iterator{});
	for (unsigned i = 0; i < fields.size(); ++i) {
		fs::path& field = *fields[i].first;
		if (field.empty()) {
			auto const regexp = std::regex(fields[i].second, std::regex_constants::icase);
			for (fs::path const& f : files) {
				std::string name = f.filename().string();  // File basename
				if (!regex_search(name, regexp)) {
					continue;  // No match for current file
				}
				field = f;
				logFound += "  " + name;
			}
		}
		files.erase(field);  // Remove from available options
	}

	m_song.music[TrackName::PREVIEW].clear();  // We don't currently support preview tracks (TODO: proper handling in audio.cc).

	if (logFound.empty() && logMissing.empty()) {
		return;
	}
	std::clog << "songparser/" << (logMissing.empty() ? "debug" : "notice") << ": " + m_song.filename.string() + ":\n";
	if (!logMissing.empty()) {
		std::clog << "  Not found:    " + logMissing + "\n";
	}
	if (!logFound.empty()) {
		std::clog << "  Autodetected: " + logFound + "\n";
	}
	std::clog << std::flush;
}

void SongParser::vocalsTogether() {
	auto togetherIt = m_song.vocalTracks.find("Together");
	if (togetherIt == m_song.vocalTracks.end()) { return; }
	Notes& together = togetherIt->second.notes;
	if (!together.empty()) { return; }
	Notes notes;
	// Collect usable vocal tracks
	struct TrackInfo {
		typedef Notes::const_iterator It;
		It it, end;
		TrackInfo(It begin, It end) :
			it(begin), end(end) {}
	};
	std::vector<TrackInfo> tracks;
	for (auto& nt : m_song.vocalTracks) {
		togetherIt->second.noteMin = std::min(togetherIt->second.noteMin, nt.second.noteMin);
		togetherIt->second.noteMax = std::max(togetherIt->second.noteMax, nt.second.noteMax);

		Notes& n = nt.second.notes;
		if (!n.empty()) { tracks.push_back(TrackInfo(n.begin(), n.end())); }
	}
	if (tracks.empty()) { return; }
	// Combine notes
	// FIXME: This should do combining on sentence level rather than note-by-note
	TrackInfo* trk = &tracks.front();
	while (trk) {
		Note const& n = *trk->it;
		// std::cerr << " " << n.syllable << ": " << n.begin << "-" << n.end << std::endl;
		notes.push_back(n);
		++trk->it;
		trk = nullptr;
		// Iterate all tracks past the processed note and find the new earliest track
		for (TrackInfo& trk2 : tracks) {
			// Skip until a sentence that begins after the note ended
			while (trk2.it != trk2.end && trk2.it->begin < n.end) { ++trk2.it; }
			if (trk2.it == trk2.end) { continue; }
			if (!trk || (trk2.it->begin < trk->it->begin)) { trk = &trk2; }
		}
	}
	together.swap(notes);
}

void SongParser::finalize() {
	vocalsTogether();
	for (auto& nt : m_song.vocalTracks) {
		VocalTrack& vocal = nt.second;
		// Remove empty sentences
		{
			Note::Type lastType = Note::Type::NORMAL;
			std::clog << "songparser/debug: In " << m_song.artist << " - " << m_song.title << std::endl;
			for (auto itn = vocal.notes.begin(); itn != vocal.notes.end();) {
				if (itn->type == Note::Type::SLEEP) { itn->end = itn->begin; ++itn; continue; }
				auto next = (itn + 1);

				// Try to fix overlapping syllables.
				if (next != vocal.notes.end() && Note::overlapping(*itn, *next)) {
					double beatDur = getBPM(m_song, itn->begin).step;
					double newEnd = (next->begin - beatDur);
					std::clog << "songparser/info: Trying to correct duration of overlapping notes (" << itn->syllable << " & " << next->syllable << ")..." << std::endl;
					std::clog << "songparser/info: Changing ending to: " << newEnd << ", will give a length of: " << (newEnd - (itn->begin)) << std::endl;
					if ((newEnd - itn->begin) >= beatDur) {
						itn->end = newEnd;
					}
					else if (next->type != Note::Type::SLEEP) {
						std::clog << "songparser/info: Resulting note would be too short, will combine them instead." << std::endl;
						itn->syllable += std::string("-") += next->syllable;
						itn->end = next->end;
						vocal.notes.erase(next);
					}
					else {
						next->begin = next->end = itn->end;
					}
				}
				Note::Type type = itn->type;
				if (type == Note::Type::SLEEP && lastType == Note::Type::SLEEP) {
					std::clog << "songparser/info: " + m_song.filename.string() + ": Discarding empty sentence" << std::endl;
					itn = vocal.notes.erase(itn);
				}
				else {
					++itn;
				}
				lastType = type;
			}
		}
		// Adjust negative notes
		if (vocal.noteMin <= 0) {
			float shift = (1 - std::floor(vocal.noteMin / 12)) * 12;
			vocal.noteMin += shift;
			vocal.noteMax += shift;
			for (auto& elem : vocal.notes) {
				elem.note += shift;
				elem.notePrev += shift;
			}
		}
		// Set begin/end times
		if (!vocal.notes.empty()) { vocal.beginTime = vocal.notes.front().begin, vocal.endTime = vocal.notes.back().end; }
		else { vocal.beginTime = vocal.endTime = 0.0; }
		// Compute maximum score
		double max_score = 0.0;
		for (auto& note : vocal.notes) { max_score += note.maxScore(); }
		vocal.m_scoreFactor = 1.0 / max_score;
	}
	if (m_tsPerBeat) {
		// Add song beat markers
		for (unsigned ts = 0; ts < m_tsEnd; ts += m_tsPerBeat) { m_song.beats.push_back(tsTime(static_cast<double>(ts))); }
	}
}

Song::BPM SongParser::getBPM(Song const& s, double ts) const {
	for (auto& itb : reverse(s.m_bpms)) {
		if (itb.begin <= ts) 
			return itb;
	}
	throw std::runtime_error("No BPM definition prior to this note...");
}

void SongParser::addBPM(double ts, float bpm) {
	Song& s = m_song;
	if (!((bpm >= 1.0f) && (bpm < 1e12))) { throw std::runtime_error("Invalid BPM value"); }
	if (!s.m_bpms.empty() && (s.m_bpms.back().ts >= ts)) {
		if (s.m_bpms.back().ts < ts) { throw std::runtime_error("Invalid BPM timestamp"); }
		s.m_bpms.pop_back();	// Some ITG songs contain repeated BPM definitions...
	}
	s.m_bpms.push_back(Song::BPM(tsTime(ts), ts, bpm));
}

double SongParser::tsTime(double ts) const {
	Song& s = m_song;
	if (s.m_bpms.empty()) {
		if (ts != 0) { throw std::runtime_error("BPM data missing"); }
		return m_gap;
	}
	for (std::vector<Song::BPM>::const_reverse_iterator it = s.m_bpms.rbegin(); it != s.m_bpms.rend(); ++it) {
		if (it->ts <= ts) { return it->begin + (ts - it->ts) * it->step; }
	}
	throw std::logic_error("INTERNAL ERROR: BPM data invalid");
}
