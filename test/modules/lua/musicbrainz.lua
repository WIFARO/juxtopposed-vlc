-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Execute the shipped provider with deterministic transport responses.
-- No public service is contacted by this test.
local release = "76df3287-6cda-33eb-8e9a-044b5e15ffdd"
local other = "99b09d02-9cc9-3fed-8431-f162165a9371"
local tests = 0
local function check(meta, expected, expected_requests, no_album_result)
    local requests = {}
    vlc = {
        item = {metas = function() return meta end},
        strings = {encode_uri_component = function(text) return text end},
        msg = {dbg = function() end},
        stream = function(url)
            requests[#requests + 1] = url
            local page
            if url:find("/release/?query=", 1, true) then
                page = no_album_result and "<release-list/>" or '<release id="' .. release .. '"></release>'
            elseif url:find("/recording/?query=", 1, true) then
                page = '<release id="' .. release .. '"></release>'
            elseif url == "http://mb.videolan.org/ws/2/release/" .. release
                or url == "http://mb.videolan.org/ws/2/release/" .. other then
                page = "<artwork>true</artwork><front>true</front>"
            else
                error("Unexpected provider request: " .. url)
            end
            return {read = function() return page end}
        end
    }
    dofile(provider_source)
    assert(descriptor().scope == "network")
    local actual = fetch_art()
    assert(actual == expected, "Unexpected artwork result for case " .. (tests + 1))
    assert(#requests == expected_requests, "Unexpected request count for case " .. (tests + 1))
    tests = tests + 1
end
local function cover(id) return "http://coverartarchive.org/release/" .. id .. "/front-500" end
check({MB_ALBUMID = release}, cover(release), 1)
check({MUSICBRAINZ_ALBUMID = release}, cover(release), 1)
check({MB_ALBUMID = other, MUSICBRAINZ_ALBUMID = release}, cover(other), 1)
check({MB_ALBUMID = "", MUSICBRAINZ_ALBUMID = release}, cover(release), 1)
check({MUSICBRAINZ_ALBUMID = ""}, nil, 0)
check({MB_ALBUMID = "", artist = "Artist", album = "Album"}, cover(release), 2)
check({artist = "Artist", album = "Album"}, cover(release), 2)
check({artist = "Artist", title = "Title"}, cover(release), 2)
check({artist = "Artist", album = "Album", title = "Title"}, cover(release), 3, true)
check({["Listing Type"] = "radio", MB_ALBUMID = release}, nil, 0)
check({["Listing Type"] = "tv", MB_ALBUMID = release}, nil, 0)
check({}, nil, 0)
print("MusicBrainz provider cases passed: " .. tests)
