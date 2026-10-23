# Series and podcast episode browsing

The redesigned player offers an Episodes control when the playing item has a
recognizable local episode identity or a known podcast feed. It opens at the
current season, highlights the playing episode, shows saved playback progress,
and expands episode details. A back button opens the season selector. Specials
use season zero; episodes without season metadata remain a separate group.
The control is also available in player toolbar customization. Existing saved
toolbar layouts retain their controls and can add Episodes from the palette.

Selecting Play or Resume appends and starts the selected file through VLC's
existing playlist machinery. Play from beginning explicitly disables resume for
that selection. Discovery and browsing do not alter the queue. Local files with
the same episode identity appear as selectable versions; a file containing an
episode range stays one playable entry. Podcasts can offer their alternate video
enclosure when the feed provides one.

## Local discovery

`modules/gui/qt/util/series_discovery.hpp` reuses the shared media identity parser.
It recognizes seasonal, date-based and supported absolute episode numbering.
It keeps absolute numbering separate from seasonal numbering and matches series
using normalized exact titles or scoped series IDs, rejecting conflicting known
years or IDs. Different seasons and episode numbers are allowed within a series.
This differs from the existing same-episode matching used for subtitle/artwork
ownership.

Discovery supports flat series folders, season subfolders, and an additional
folder around each episode. Season folders and episode wrappers contribute
missing identity information. A file such as `01.mkv` or `02 - Pilot.mkv` can
inherit the show and season from `Show/Season 123/`. File extensions restrict
local discovery to video media; subtitle, artwork and unrelated series files
are excluded.

On input changes the player supplies the current URL and already available tags
to `SeriesModel`. A worker searches the current directory first, steps out of
recognized episode/season wrappers, and searches beneath the inferred series
folder. It does not walk upward through unrelated ancestors or enumerate a
filesystem/drive root. Symbolic links are not followed. Published partial results
are throttled; playback never waits for the scan.

The initial limits are four levels below the series folder, 128 directories,
16,384 entries in total, 2,048 entries per directory and approximately two seconds
between filesystem operations. A blocked filesystem call itself cannot be
interrupted by that elapsed-time limit. Search further raises the depth to eight,
the directory count to 256, the total entry count to 32,768 and the elapsed-time
budget to five seconds. The per-directory limit remains unchanged. Partial
results remain usable and are labeled. Choose series folder can narrow or correct
the search scope.

The model retains its directory snapshot when playing an already discovered
episode. Watches on visited directories trigger debounced rescans. Switching
series, refreshing or destroying the model cancels older work; generation
checks prevent late results from replacing the current context. This is a
session cache rather than a new persistent library index.

## Metadata, artwork and playback state

The interface uses the redesign's existing artwork resolver and media-library
state reads. Artwork is requested for expanded local entries; a batch reads
existing progress and watched flags without opening every file or importing a
whole series. The playing item's progress also updates directly from the player.
Without a media library, local discovery and playback still work, with current
playback progress and artwork placeholders.

Episode titles and descriptions use available current-item or feed metadata.
Missing local titles fall back to the episode number, and filenames remain
available in the details. Discovery does not fetch online TV descriptions or
posters, read NFO files, or decode every sibling to obtain embedded metadata.
Additional metadata adapters can extend the model independently.

## RSS episodes

For a known feed, the existing `NetworkPlaylistModel` parses RSS and supplies the
same season projection. It is registered in `VLC.Network` independently of media
library availability. Feed identity and GUID identify an episode even when its
enclosure URL changes. Duplicate feed GUIDs are collapsed; distinct GUIDs with
the same season/episode numbers remain distinct items. Numbered episodes sort
numerically and unnumbered episodes use publication order. Only episodes still
present in the available feed can be discovered this way. A standalone stream
URL without feed metadata does not trigger a search for a podcast feed.

## Validation

`test_series_discovery.cpp` covers folder layouts, three-digit seasons, numeric
filenames, specials, remakes, combined episodes, versions, date/absolute numbering,
scan bounds, symbolic links, cancellation and drive-root avoidance.
`test_series_model.cpp` covers projection, GUID identity, stale results, progress,
snapshot reuse and native model consistency. `tst_RedesignEpisodes.qml` covers
season navigation, expansion, resume/restart, keyboard navigation and scaled
layouts. The media-library model suite checks that batch state reads return
saved progress without requesting artwork. Existing player layout, chrome and
customization suites cover the new control alongside saved layouts.
