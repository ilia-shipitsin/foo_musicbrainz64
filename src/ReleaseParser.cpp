#include "stdafx.hpp"
#include "ReleaseParser.hpp"

struct AsciiReplacement
{
	std::string what, with;
};

static const std::vector<AsciiReplacement> ascii_replacements =
{
	{ u8"…", "..." },
	{ u8"‘", "'" },
	{ u8"’", "'" },
	{ u8"‚", "'" },
	{ u8"“", "\"" },
	{ u8"”", "\"" },
	{ u8"„", "\"" },
	{ u8"′", "'" },
	{ u8"″", "\"" },
	{ u8"‹", "<" },
	{ u8"›", ">" },
	{ u8"«", "\"" },
	{ u8"»", "\"" },
	{ u8"‐", "-" },
	{ u8"‒", "-" },
	{ u8"–", "-" },
	{ u8"−", "-" },
	{ u8"—", "-" },
	{ u8"―", "-" },
};

ReleaseParser::ReleaseParser(json obj, size_t handle_count, json discid) : m_obj(obj), m_handle_count(handle_count)
{
	m_release.discid = to_str(discid);
}

Release ReleaseParser::parse()
{
	auto& medias = m_obj["media"];
	if (!medias.is_array()) return m_release;

	const size_t release_totaltracks = std::accumulate(medias.begin(), medias.end(), size_t{ 0 }, [this](size_t t, json j) { return t + j["tracks"].size(); });
	const bool complete = release_totaltracks == m_handle_count;
	m_release.totaldiscs = medias.size();

	Strings dummy;
	get_artist_info(m_obj, m_release.album_artist, m_release.album_artist_sort, dummy, m_release.albumartistids);

	for (auto&& media : medias)
	{
		auto& tracks = media["tracks"];
		if (tracks.is_array() && (complete || tracks.size() == m_handle_count))
		{
			if (m_release.discid.length())
			{
				auto& discs = media["discs"];
				if (!discs.is_array()) continue;
				const auto it = std::ranges::find_if(discs, [=](const auto& disc) { return m_release.discid == to_str(disc["id"]); });
				if (it == discs.end()) continue;
			}

			if (!complete) m_release.partial_lookup_matches++;

			const std::string format = to_str(media["format"]);
			const std::string subtitle = to_str(media["title"]);
			const size_t discnumber = media["position"].get<size_t>();
			const size_t totaltracks = tracks.size();

			for (auto&& track : tracks)
			{
				Track t;
				get_artist_info(track, t.artist, t.artist_sort, t.artists, t.artistids);
				t.discnumber = discnumber;
				t.media = format;
				t.subtitle = subtitle;
				t.title = to_str(track["title"]);
				t.releasetrackid = to_str(track["id"]);
				t.tracknumber = track["position"].get<size_t>();
				t.totaltracks = totaltracks;

				auto& recording = track["recording"];
				if (recording.is_object())
				{
					t.trackid = to_str(recording["id"]);

					auto& isrcs = recording["isrcs"];
					if (isrcs.is_array())
					{
						auto view = isrcs | std::views::transform([](auto&& isrc) { return to_str(isrc); });
						t.isrcs = std::ranges::to<Strings>(view);
					}
				}

				if (m_release.album_artist != t.artist) m_release.is_various = true;
				m_release.tracks.emplace_back(t);
			}
		}
	}

	m_release.albumid = to_str(m_obj["id"]);
	m_release.barcode = to_str(m_obj["barcode"]);
	m_release.date = to_str(m_obj["date"]);
	m_release.status = to_str(m_obj["status"]);
	m_release.title = to_str(m_obj["title"]);
	m_release.country = to_str(m_obj["country"]);

#if 0
	if (m_release.country != "GB")
	{
		auto& release_events = m_obj["release-events"];
		if (release_events.is_array())
		{
			for (auto&& release_event : release_events)
			{
				auto& area = release_event["area"];
				if (area.is_object())
				{
					auto& codes = area["iso-3166-1-codes"];
					if (codes.is_array())
					{
						const auto it = std::ranges::find_if(codes, [](const auto& code) { return to_str(code) == "GB"; });
						if (it != codes.end())
						{
							m_release.country = "GB";
							break;
						}
					}
				}
			}
		}
	}
#endif

	auto& label_info = m_obj["label-info"];
	if (label_info.is_array() && label_info.size())
	{
		m_release.catalog = to_str(label_info[0]["catalog-number"]);
		auto& label = label_info[0]["label"];
		if (label.is_object())
		{
			m_release.label = to_str(label["name"]);
		}
	}

	auto& rg = m_obj["release-group"];
	if (rg.is_object())
	{
		m_release.original_release_date = to_str(rg["first-release-date"]);
		m_release.releasegroupid = to_str(rg["id"]);
		m_release.primary_type = to_str(rg["primary-type"]);

		auto& secondary_types = rg["secondary-types"];
		if (secondary_types.is_array())
		{
			auto view = secondary_types | std::views::transform([](auto&& secondary_type) { return to_str(secondary_type); });
			m_release.secondary_types = fmt::format("{}", fmt::join(view, ", "));
		}
	}

	if (prefs::bools::short_date)
	{
		if (m_release.date.length() > 4)
		{
			m_release.date.resize(4);
		}

		if (m_release.original_release_date.length() > 4)
		{
			m_release.original_release_date.resize(4);
		}
	}

	auto& relations = m_obj["relations"];
	if (relations.is_array())
	{
		std::map<std::string, Strings> performer_map;

		for (auto&& relation : relations)
		{
			auto& artist_obj = relation["artist"];
			if (artist_obj.is_object())
			{
				const std::string artist = to_str(artist_obj["name"]);
				const std::string type = to_str(relation["type"]);

				if (type == "composer")
				{
					m_release.composers.emplace_back(artist);
				}
				else if (type == "instrument" || type == "vocal")
				{
					auto& attributes = relation["attributes"];
					if (attributes.is_array() && attributes.size() > 0)
					{
						Strings what;
						for (auto&& attribute : attributes)
						{
							what.emplace_back(to_str(attribute));
						}

						const auto it = performer_map.find(artist);
						if (it == performer_map.end())
						{
							performer_map[artist] = what;
						}
						else
						{
							std::ranges::copy(what, std::back_inserter(it->second));
						}
					}
				}
			}
		}

		for (const auto& [performer, what] : performer_map)
		{
			const std::string str = fmt::format("{} ({})", performer, fmt::join(what, ", "));
			m_release.performers.emplace_back(str);
		}
	}

	return m_release;
}

std::string ReleaseParser::to_str(json j)
{
	if (j.is_null()) return "";
	if (!j.is_string()) return j.dump();

	pfc::string8 str = j.get<std::string>();
	if (prefs::bools::ascii_punctuation)
	{
		for (const auto& [what, with] : ascii_replacements)
		{
			str = str.replace(what.c_str(), with.c_str());
		}
	}
	return str.get_ptr();
}

void ReleaseParser::filter_releases(json releases, size_t count, Strings& out)
{
	if (!releases.is_array()) return;

	for (auto&& release : releases)
	{
		const std::string id = to_str(release["id"]);
		auto& release_track_count = release["track-count"];

		if (release_track_count.is_number_unsigned() && release_track_count.get<size_t>() == count)
		{
			out.emplace_back(id);
		}
		else
		{
			auto& medias = release["media"];
			if (medias.is_array())
			{
				for (auto&& media : medias)
				{
					auto& media_track_count = media["track-count"];
					if (media_track_count.is_number_unsigned() && media_track_count.get<size_t>() == count)
					{
						out.emplace_back(id);
						break;
					}
				}
			}
		}
	}
}

void ReleaseParser::get_artist_info(json j, std::string& artist, std::string& artist_sort, Strings& artists, Strings& ids)
{
	auto& artist_credits = j["artist-credit"];
	if (!artist_credits.is_array()) return;

	for (auto&& artist_credit : artist_credits)
	{
		auto& artist_obj = artist_credit["artist"];

		const std::string name = to_str(artist_credit["name"]);
		const std::string joinphrase = to_str(artist_credit["joinphrase"]);
		const std::string id = to_str(artist_obj["id"]);
			
		std::string sort_name = to_str(artist_obj["sort-name"]);
		if (sort_name.empty()) sort_name = name;

		artist += name + joinphrase;
		artist_sort += sort_name + joinphrase;
		artists.emplace_back(name);
		ids.emplace_back(id);
	}
}
