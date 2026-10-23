// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <vlc_input_item.h>
#include <vlc_url.h>
#include <medialibrary/IMedia.h>
#include <medialibrary/IMetadata.h>
#include <map>
#include <sstream>
#include <string>

namespace vlc::podcast {
using Fields = std::map<std::string, std::string>;
inline Fields read(const ::medialibrary::IMedia& media)
{
    const auto& value = media.metadata(::medialibrary::IMedia::MetadataType::ApplicationSpecific);
    if (!value.isSet()) return {};
    const auto& text = value.asStr();
    if (text.compare(0, 15, "vlc-podcast-v1\n") != 0 || text.size() > 2 * 1024 * 1024) return {};
    Fields fields;
    std::istringstream stream(text.substr(15));
    std::string line;
    while (std::getline(stream, line)) {
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        auto decoded = vlc_uri_decode_duplicate(line.c_str() + split + 1);
        if (decoded) { fields[line.substr(0, split)] = decoded; free(decoded); }
    }
    return fields;
}
inline bool save(::medialibrary::IMedia& media, const Fields& fields)
{
    std::string text = "vlc-podcast-v1\n";
    for (const auto& [key, value] : fields) {
        auto encoded = vlc_uri_encode(value.c_str());
        if (!encoded) return false;
        text += key + "=" + encoded + "\n";
        free(encoded);
    }
    if (text.size() > 2 * 1024 * 1024) return false;
    return media.setMetadata(::medialibrary::IMedia::MetadataType::ApplicationSpecific, text);
}
inline const char* const extras[] = {"podcast:feed", "podcast:audio", "podcast:mime", "content:encoded", "podcast:showGuid", "podcast:guid", "podcast:episodeType", "podcast:explicit", "podcast:video", "podcast:transcript", "podcast:chapters"};
inline const vlc_meta_type_t metadata[] = {vlc_meta_Title, vlc_meta_ShowName, vlc_meta_Artist, vlc_meta_Description, vlc_meta_Date, vlc_meta_Season, vlc_meta_Episode, vlc_meta_ArtworkURL};
inline void collect(Fields& fields, input_item_t* item)
{
    auto audio = input_item_GetMetaExtra(item, "podcast:audio");
    fields["uri"] = audio && *audio ? audio : item->psz_uri;
    free(audio);
    fields["duration"] = std::to_string(MS_FROM_VLC_TICK(input_item_GetDuration(item)));
    for (const auto key : extras) {
        auto value = input_item_GetMetaExtra(item, key);
        fields[key] = value ? value : ""; free(value);
    }
    for (const auto key : metadata) {
        auto value = input_item_GetMeta(item, key);
        fields[std::to_string(key)] = value ? value : ""; free(value);
    }
    if (fields[std::to_string(vlc_meta_Title)].empty()) fields[std::to_string(vlc_meta_Title)] = item->psz_name;
}
inline void apply(const Fields& fields, input_item_t* item)
{
    const auto uri = fields.find("uri");
    if (uri == fields.end() || !item) return;
    input_item_SetURI(item, uri->second.c_str());
    for (const auto key : extras) {
        const auto value = fields.find(key);
        if (value != fields.end()) input_item_SetMetaExtra(item, key, value->second.c_str());
    }
    for (const auto key : metadata) {
        const auto value = fields.find(std::to_string(key));
        if (value != fields.end() && !value->second.empty()) input_item_SetMeta(item, key, value->second.c_str());
    }
    const auto duration = fields.find("duration");
    if (duration != fields.end()) input_item_SetDuration(item, VLC_TICK_FROM_MS(atoll(duration->second.c_str())));
    auto title = input_item_GetTitle(item);
    if (title) { input_item_SetName(item, title); free(title); }
}
}
