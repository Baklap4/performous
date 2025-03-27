#include "songparser.hh"
#include <sstream>
#include <regex>
#include <fstream>
#include <nlohmann/json.hpp>
#include "fs.hh"

using namespace SongParserUtil;

bool SongParser::assCheck(std::string const& data) const {
    // Basic check to confirm it's an Aegisub .ass file by looking for the necessary headers.
    return data.find("[Script Info]") != std::string::npos &&
        data.find("[Events]") != std::string::npos;
}

struct Syllable {
    std::string duration;
    std::string syllable;
};

void SongParser::buildFileIndex(const fs::path& directory) {
    std::unordered_map<std::string, fs::path> fileIndex;

    for (const auto& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            size_t firstDot = filename.find('.');
            if (firstDot == std::string::npos || firstDot + 9 > filename.size()) continue;
            std::string guidPrefix = std::string(filename.substr(firstDot + 1, 8));
            fileIndex[guidPrefix] = entry.path();
        }
    }

    assTagsFileIndex = fileIndex;
}

void SongParser::assParseMetadata() {
    // Determine starting path (lyrics folder which contains the ass files.)
    auto filePath = m_song.filename;
    // Song Guid we're working with.
    auto karaId = filePath.stem().string();

    // Setup paths for easy access.
    auto parentPath = filePath.parent_path().parent_path();
    auto lyricsPath = parentPath / "lyrics" / (karaId + ".ass");
    auto metadataPath = parentPath / "karaokes" / (karaId + ".kara.json");

    auto mediaPath = parentPath / "medias";
    // Extra metadata paths
    auto systemTagsPath = parentPath / "system-tags";
    auto tagsPath = parentPath / "tags";
    auto languageTagsPath = parentPath / "language-tags";

    if (assTagsFileIndex.empty())
    {
        buildFileIndex(tagsPath);
    }

    bool isFullSize = false;
    bool isInstrumental = false;
    bool isAlternative = false;
    bool isCreditless = false;

    // Read metadata file.
    nlohmann::json jsonData;
    std::ifstream file(metadataPath);
    if (!file.is_open()) throw std::runtime_error("Can't open file.");
    file >> jsonData;
    file.close();

    // Determine the media file.
    if (jsonData.contains("medias") && jsonData["medias"].is_array() && !jsonData["medias"].empty()) {
        const auto& firstItem = jsonData["medias"].front();
        if (firstItem.contains("filename") && firstItem["filename"].is_string()) {
            mediaPath /= getJsonEntry<std::string>(firstItem, "filename").value_or("");
        }
    }

    std::string title;
    const auto& data = jsonData.find("data");

    if (data != jsonData.end() && data->is_object()) {
        const auto& titles = data->find("titles");
        const auto& langKey = data->find("titles_default_language");

        if (titles != data->end() && titles->is_object() && langKey != data->end() && langKey->is_string()) {
            const auto& titleEntry = titles->find(langKey->get<std::string>());
            if (titleEntry != titles->end() && titleEntry->is_string()) {
                title = titleEntry->get<std::string>();
            }
        }
        else if (titles != data->end() && titles->is_object() && jsonData["data"]["titles"].contains("eng")) {
			title = jsonData["data"]["titles"]["eng"].get<std::string>();
        }
        else {
            title = jsonData["data"]["titles"].front().get<std::string>();
        }


        for (const auto& singerId : jsonData["data"]["tags"]["versions"])
        {
            auto singerIdstr = singerId.get<std::string>();
            auto singerIdStrFirstChunk = singerIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(singerIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {

                if (tagData.contains("tag") && tagData["tag"].is_object() && tagData["tag"].contains("name"))
                {
                    auto value = tagData["tag"]["name"].get<std::string>();
                    if (value == "Full")
                    {
						isFullSize = true;
                    }
                    else if (value == "Off Vocal")
                    {
                        isInstrumental = true;
                    }
                    else if (value == "Alternative")
                    {
                        isAlternative = true;
                    }
                    else if (value == "Creditless")
                    {
                        isCreditless = true;
                    }
                }
            }
        }
    }

    std::string artist;
    if (jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("singergroups") && jsonData["data"]["tags"]["singergroups"].is_array())
    {
        for (const auto& singerGroupId : jsonData["data"]["tags"]["singergroups"])
        {
            auto singerGroupIdstr = singerGroupId.get<std::string>();
            auto singerGroupIdStrFirstChunk = singerGroupIdstr.substr(0, 8);

            auto it = assTagsFileIndex.find(singerGroupIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {
                const auto& langKey = data->find("titles_default_language");// ->get<std::string>();

                if (tagData.contains("tag") && tagData["tag"].is_object() &&
                    tagData["tag"].contains("i18n") && tagData["tag"].is_object())
                {
                    if (langKey != data->end() && langKey->is_string())
                    {
						auto langKeyStr = langKey->get<std::string>();
                        if (tagData["tag"]["i18n"].contains(langKeyStr)) {
                            artist = tagData["tag"]["i18n"][langKeyStr].get<std::string>();
                        }
                    }
                    if (artist.empty() && !tagData["tag"]["name"].empty()) {
                        artist = tagData["tag"]["name"].get<std::string>();
                    }
                    if (artist.empty() && !tagData["tag"]["i18n"].empty())
                    {
                        artist = tagData["tag"]["i18n"].front().get<std::string>();
                    }
                }
            }
        }
    }
    if (artist.empty() && jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("singers") && jsonData["data"]["tags"]["singers"].is_array())
    {
        for (const auto& singerId : jsonData["data"]["tags"]["singers"])
        {
			auto singerIdstr = singerId.get<std::string>();
            auto singerIdStrFirstChunk = singerIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(singerIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {
                const auto& langKey = data->find("titles_default_language");

                if (tagData.contains("tag") && tagData["tag"].is_object() &&
                    tagData["tag"].contains("i18n") && tagData["tag"].is_object())
                {
                    if (langKey != data->end() && langKey->is_string())
                    {
						auto langKeyStr = langKey->get<std::string>();
                        if (tagData["tag"]["i18n"].contains(langKeyStr)) {
                            artist = tagData["tag"]["i18n"][langKeyStr].get<std::string>();
                        }
                    }
                    if (artist.empty() && !tagData["tag"]["name"].empty()) {
                        artist = tagData["tag"]["name"].get<std::string>();
                    }
                    if (artist.empty() && !tagData["tag"]["i18n"].empty())
                    {
                        artist = tagData["tag"]["i18n"].front().get<std::string>();
                    }
                }
            }
        }
    }
    if (artist.empty() && jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("franchises") && jsonData["data"]["tags"]["franchises"].is_array())
    {
        for (const auto& franchiseId : jsonData["data"]["tags"]["franchises"])
        {
            auto franchiseIdstr = franchiseId.get<std::string>();
            auto franchiseIdStrFirstChunk = franchiseIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(franchiseIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {
                const auto& langKey = data->find("titles_default_language");

                if (tagData.contains("tag") && tagData["tag"].is_object() &&
                    tagData["tag"].contains("i18n") && tagData["tag"].is_object())
                {
                    if (langKey != data->end() && langKey->is_string())
                    {
                        auto langKeyStr = langKey->get<std::string>();
                        if (tagData["tag"]["i18n"].contains(langKeyStr)) {
                            artist = tagData["tag"]["i18n"][langKeyStr].get<std::string>();
                        }
                    }
                    if (artist.empty() && !tagData["tag"]["name"].empty()) {
                        artist = tagData["tag"]["name"].get<std::string>();
                    }
                    if (artist.empty() && !tagData["tag"]["i18n"].empty())
                    {
                        artist = tagData["tag"]["i18n"].front().get<std::string>();
                    }
                }
            }
        }
    }
    if (artist.empty() && jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("series") && jsonData["data"]["tags"]["series"].is_array())
        {
            for (const auto& serieId : jsonData["data"]["tags"]["series"])
            {
                auto serieIdstr = serieId.get<std::string>();
                auto serieIdStrFirstChunk = serieIdstr.substr(0, 8);
                auto it = assTagsFileIndex.find(serieIdStrFirstChunk);
                if (it == assTagsFileIndex.end()) {
                    continue;
                }
                fs::path tagFilePath = it->second;

                nlohmann::json tagData;
                std::ifstream tagFile(tagFilePath);
                if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
                tagFile >> tagData;
                tagFile.close();

                if (data != jsonData.end() && data->is_object()) {
                    const auto& langKey = data->find("titles_default_language");

                    if (tagData.contains("tag") && tagData["tag"].is_object() &&
                        tagData["tag"].contains("i18n") && tagData["tag"].is_object())
                    {
                        if (langKey != data->end() && langKey->is_string())
                        {
                            auto langKeyStr = langKey->get<std::string>();
                            if (tagData["tag"]["i18n"].contains(langKeyStr)) {
                                artist = tagData["tag"]["i18n"][langKeyStr].get<std::string>();
                            }
                        }
                        if (artist.empty() && !tagData["tag"]["name"].empty()) {
                            artist = tagData["tag"]["name"].get<std::string>();
                        }
                        if (artist.empty() && !tagData["tag"]["i18n"].empty())
                        {
                            artist = tagData["tag"]["i18n"].front().get<std::string>();
                        }
                    }
                }
            }
        }
    if (artist.empty()) {
        artist = "N/A";
        std::clog << "songparse/notice: Artist not found for " << m_song.title << " Ass: " << m_song.filename << std::endl;
    }

    std::string language;
    if (jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("langs") && jsonData["data"]["tags"]["langs"].is_array())
    {
        for (const auto& singerId : jsonData["data"]["tags"]["langs"])
        {
            auto singerIdstr = singerId.get<std::string>();
            auto singerIdStrFirstChunk = singerIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(singerIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {

                if (tagData.contains("tag") && tagData["tag"].is_object() &&
                    tagData["tag"].contains("i18n") && tagData["tag"].is_object())
                {
                    if (tagData["tag"]["i18n"].contains("eng")) {
                        language = tagData["tag"]["i18n"]["eng"].get<std::string>();
                    }
                }
            }
        }
    }

    std::string creator;
    if (jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("authors") && jsonData["data"]["tags"]["authors"].is_array())
    {
        for (const auto& singerId : jsonData["data"]["tags"]["authors"])
        {
            auto singerIdstr = singerId.get<std::string>();
            auto singerIdStrFirstChunk = singerIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(singerIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {

                if (tagData.contains("tag") && tagData["tag"].is_object() && tagData["tag"].contains("name"))
                {
					creator = tagData["tag"]["name"].get<std::string>();
                }
            }
        }
    }

    int year = 0;
    if (jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("year") && jsonData["data"]["year"].is_number_integer())
    {
        year = jsonData["data"]["year"].get<int>();
    }

    std::string tags;
    if (jsonData.contains("data") && jsonData["data"].is_object() &&
        jsonData["data"].contains("tags") && jsonData["data"]["tags"].is_object() &&
        jsonData["data"]["tags"].contains("series") && jsonData["data"]["tags"]["series"].is_array())
    {
        for (const auto& singerId : jsonData["data"]["tags"]["series"])
        {
            auto singerIdstr = singerId.get<std::string>();
            auto singerIdStrFirstChunk = singerIdstr.substr(0, 8);
            auto it = assTagsFileIndex.find(singerIdStrFirstChunk);
            if (it == assTagsFileIndex.end()) {
                continue;
            }
            fs::path tagFilePath = it->second;

            nlohmann::json tagData;
            std::ifstream tagFile(tagFilePath);
            if (!tagFile.is_open()) throw std::runtime_error("Can't open file.");
            tagFile >> tagData;
            tagFile.close();

            if (data != jsonData.end() && data->is_object()) {

                if (tagData.contains("tag") && tagData["tag"].is_object() && tagData["tag"].contains("name"))
                {
                    tags = tagData["tag"]["name"].get<std::string>();
                }
            }
        }
    }

    // Dummy note to indicate there is a track;
	// This is actually wrong since the song can be a duet... we don't know untill we process the dialogues.
    m_song.insertVocalTrack(TrackName::VOCAL_LEAD, VocalTrack(TrackName::VOCAL_LEAD));
    m_song.artist = artist;
    m_song.title = title;
    if (isFullSize)
    {
		m_song.title += " (Full Size)";
    }
	if (isInstrumental)
	{
		m_song.title += " (Instrumental)";
	}
    m_song.music[TrackName::BGMUSIC] = mediaPath;
    m_song.video = mediaPath;
    m_song.language = language;
	m_song.creator = creator;
	m_song.tags = tags;
	if (isAlternative)
	{
		m_song.tags += ", Alternative";
	}
    if (isCreditless)
    {
        m_song.tags += ", Creditless";
    }
    m_song.providedBy = "Kara.Moe";
    if (year != 0)
    {
        m_song.year = year;
    }

    if (m_song.title.empty()) {
        throw std::runtime_error("Required header title missing");
    }
    if (m_song.artist.empty())
    {
        throw std::runtime_error("Required header artist fields missing");
    }
    if (m_song.music[TrackName::BGMUSIC].empty() || !fs::exists(m_song.music[TrackName::BGMUSIC]))
    {
        m_song.loadStatus = Song::LoadStatus::ERROR;
        std::clog << "songparser/error: Required MP3 '" << m_song.music[TrackName::BGMUSIC].string() << "' file isn't available." << std::endl;
    }

    if (m_bpm != 0.0f) addBPM(0, m_bpm);
}
void SongParser::assParse() {
    // Read the file content
    std::ifstream file(m_song.filename);
    std::string line;

    if (!file.is_open()) {
        throw std::runtime_error("Could not open Aegisub .ass file.");
    }

    if (!m_song.vocalTracks.empty()) { m_song.vocalTracks.clear(); }
    std::unordered_map<std::string, std::string> styleMap;
    m_song.insertVocalTrack(TrackName::VOCAL_LEAD, VocalTrack(TrackName::VOCAL_LEAD)); 

    auto basePath = m_song.path;
    addBPM(0, 1500);
    bool inEvents = false;
    std::vector<std::string> detectedStyles;
    while (std::getline(file, line)) {
        // Start processing after the '[Events]' section
        if (line.find("[Events]") != std::string::npos) {
            inEvents = true;
            continue;
        }

        if (inEvents) {
            if (line.empty() || line[0] == ';') continue;  // Ignore empty lines or comments

            // Parsing the dialogue line in [Events]
            std::regex dialoguePattern(R"(^Dialogue:.*,([0-9:.]+),([0-9:.]+),([^,]+),,([^,]+),([^,]+),([^,]+),([^,]+),(.+)$)");
            std::smatch match;

            if (std::regex_match(line, match, dialoguePattern)) {
                std::string startTimeStr = match[1].str();
                std::string endTimeStr = match[2].str();
                std::string style = match[3].str();
                std::string text = match[8].str();

                if (styleMap.find(style) == styleMap.end() && detectedStyles.size() < 2) {
                    std::string trackName = detectedStyles.empty() ? TrackName::VOCAL_LEAD : SongParserUtil::DUET_P2;
                    m_song.insertVocalTrack(trackName, VocalTrack(style));
                    styleMap[style] = trackName;
                    detectedStyles.push_back(style);
                }
                bool addedToLead = false;
                bool addedToBacking = false;
                if (styleMap.find(style) == styleMap.end()) {
                    // Try inserting the style into VOCAL_LEAD if no overlap
                    if (!addedToLead) {
                        auto& leadTrack = m_song.getVocalTrack(TrackName::VOCAL_LEAD);
                        if (!checkOverlap(leadTrack, timeToSeconds(startTimeStr)*100, timeToSeconds(endTimeStr)*100)) {
                            styleMap[style] = TrackName::VOCAL_LEAD;
                            addedToLead = true;
                        }
                    }

                    // Try inserting the style into VOCAL_BACKING if no overlap
                    else if (!addedToBacking) {
                        auto& backingTrack = m_song.getVocalTrack(SongParserUtil::DUET_P2);
                        if (!checkOverlap(backingTrack, timeToSeconds(startTimeStr)*100, timeToSeconds(endTimeStr)*100)) {
                            styleMap[style] = SongParserUtil::DUET_P2;
                            addedToBacking = true;
                        }
                    }

                    // If it doesn't fit in either, log the issue
                    else if (!addedToLead && !addedToBacking) {
                        std::cerr << "Warning: Style " << style << " overlaps both tracks. Skipping." << std::endl;
                        continue;
                    }
                }

                std::string trackName = styleMap[style];
                auto& track = m_song.getVocalTrack(trackName);
                if (addedToBacking) 
                {
					track = m_song.getVocalTrack(SongParserUtil::DUET_P2);
                }
                else if (addedToLead) 
                {
                    track = m_song.getVocalTrack(TrackName::VOCAL_LEAD);
				}

                std::vector<Syllable> syllables;
                size_t pos = 0;

                // Regex to capture valid \k karaoke timing tags inside {} blocks
                std::regex kTagRegex(R"(\{[^}]*\\k[fko]?(\d+)[^}]*\})");
                std::smatch kMatch;

                while (std::regex_search(text, kMatch, kTagRegex)) {
                    //std::string kValueKey = kMatch[0].str(); // use this to add missing k tags. Contains: {\fad(149,100)\k90\k24}

                    std::string kValue = kMatch[1].str();  // Extract numeric value after \k
                    int duration = std::stoi(kValue);  // Convert centiseconds to beats

                    // Find the actual syllable text **after** the closing `}`
                    size_t syllableStart = kMatch.position() + kMatch.length();
                    size_t nextTag = text.find_first_of("\\{", syllableStart);
                    if (nextTag == std::string::npos) nextTag = text.length();

                    std::string syllable = text.substr(syllableStart, nextTag - syllableStart);

                    syllables.push_back({ kValue, syllable });

                    // Move text past this match for the next search
                    text = text.substr(syllableStart);
                }

                auto beatsPerSecond = 100;
				auto currentBeat = timeToSeconds(startTimeStr) * beatsPerSecond;

				track.beginTime = currentBeat / 100;
                track.noteMin = 30;
                track.noteMax = 30;
                track.m_scoreFactor = 1.0;

                // Process syllables and generate notes
                for (const auto& s : syllables) {
                    int convertedDuration = (std::stoi(s.duration) / 100.0 * beatsPerSecond); // Convert centiseconds to beats
                    int tweakedDuration = convertedDuration > 1 ? convertedDuration - 1 : convertedDuration;

                    if (s.syllable.empty()) {
                        currentBeat += tweakedDuration;
                        continue;
                    }

                    Note note;
                    note.type = Note::Type::NORMAL;
                    note.begin = currentBeat / 100;
                    note.end = (currentBeat + tweakedDuration) / 100;
                    note.syllable = s.syllable;
                    note.note = 30;
                    note.notePrev = note.note;

                    track.notes.push_back(note);
                    currentBeat += tweakedDuration;
                }
                track.endTime = currentBeat / 100;

                // Add a "sleep" note at the end of the line
                if (!track.notes.empty() && track.notes.back().type != Note::Type::SLEEP) {
                    Note newLineNote;
                    newLineNote.type = Note::Type::SLEEP;
                    newLineNote.begin = currentBeat / 100;
					track.notes.push_back(newLineNote);
                }
            }
        }
    }
}


double SongParser::timeToSeconds(const std::string& timeStr) {
    int h = 0, m = 0, s = 0, cs = 0;
    int count = sscanf(timeStr.c_str(), "%d:%d:%d.%d", &h, &m, &s, &cs);
    if (count < 4) cs = 0; // Ensure cs is always valid. 10 cs = 100 ms.
    auto result = (h * 3600) + (m * 60) + s + (cs * 10 / 1000.0);
    return result;
}

int SongParser::timeToBeat(double seconds) {
    double beatsPerSecond = 100;
    return static_cast<int>(seconds * beatsPerSecond);
}
// Helper function to check for overlap between two time intervals
bool SongParser::checkOverlap(const VocalTrack& track, const double& startTimeStr, const double& endTimeStr) {
    // Check if the given time interval overlaps any existing entry in the track
    for (const auto& note : track.notes) {
        if ((startTimeStr < note.end) && (endTimeStr > note.begin)) {
            return true; // Overlap detected
        }
    }
    return false;
}
