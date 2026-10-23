# Media identification, artwork, and remote subtitles

The Qt interface uses a provider-independent identity model to understand media
names before searching OpenSubtitles. The same model is also used when assigning
local subtitle and audio files. It does not require a player, a QML engine, or
network access.

## Boundaries

- `modules/gui/qt/util/media_identity.hpp` contains QtCore-only parsing, evidence
  merging, serialization, title similarity, and identity comparison. Filename,
  folder, embedded metadata, provider data, and user corrections have provenance.
  Conflicting values remain recorded. Conflicting guesses are not added as title
  aliases; only actual aliases supplied by a caller belong in that list.
- `media_identity_vlc.hpp` reads already available input-item tags. Player and
  library adapters supply existing duration, dimensions, languages, and frame
  rate. Technical properties support compatibility; they do not identify a work.
- `media_fingerprint.hpp` implements the OpenSubtitles checksum for regular local
  files. Its algorithm is named explicitly: it is not a cryptographic identifier.
- `subtitle_matching.hpp` applies subtitle-specific compatibility and preference
  rules to shared identities. Other metadata consumers should use the shared
  comparison rather than importing subtitle policy.
- `SubtitleSourceService` handles provider requests, language selection,
  cancellation, pagination, and downloads. The interface handles drafts and user
  corrections. Downloads still happen on Save.

## Identity rules

Titles retain their original spelling and useful punctuation. Separate normalized
forms are used for comparison. Release year, season/episode lists, date-based
numbering, absolute anime numbering, release group, source, edition, resolution,
codecs, and frame rate are extracted where supported. Numeric film titles and
`Part 1` title suffixes are preserved.

A year on a series identity is a series start year. An embedded episode air date
must not become that year. Absolute numbering is never converted to seasonal
numbering without an external mapping. An episode range is a list, rather than
silently becoming its first episode.

Provider IDs retain their scope. In particular, a series TMDB ID is different
from a movie or episode TMDB ID. A series ID alone cannot confirm an episode.
IMDb tags in episodic release names are treated as series IDs; explicit provider
metadata can supply the episode's IMDb ID separately.

Title similarity is supporting evidence. A fuzzy title alone never authorizes
an automatic assignment. Known conflicting IDs, types, years, or episode details
block recommendations. Missing information is not treated as contradictory
information, but can prevent an identity from being sufficiently strong.

## Search and selection

Automatic lookup attempts the local-file fingerprint first, in a worker thread.
Only the first and last 64 KiB are read. The input stream used for playback is
never sought. Files shorter than 128 KiB, unsupported URLs, read failures,
changes during hashing, and cancellation do not produce a fingerprint.

If there is no confirmed hash result, requests use known external IDs or a clean
title and structured year/season/episode fields. A guessed filename/folder year
may be omitted on a further attempt when no compatible result was found. A user
or metadata year is not relaxed, and episode constraints and IDs are retained.
Manual queries bypass the original file's fingerprint and IDs, so correcting a
bad identification does not keep searching for the old work.

A batch searches each distinct media URL independently. It accepts up to 64
files, reads at most three result pages per attempt, and stops collecting further
files after approximately 4,000 distinct results. Limits and partial failures
are surfaced as messages. All outstanding work is invalidated when a newer
search replaces it, its source is changed, or the dialog closes.

Hash confirmations are attached to the particular media URL searched. Reusing a
cached subtitle file does not change that association or discard provider
identity evidence. Repeated results are deduplicated by the provider file ID.

Recommendations are computed per language, with identity evidence ahead of
release compatibility. Source, release group, edition and frame rate can affect
ranking. Forced/SDH settings are preferences, not identity evidence. Forced-only
subtitles are not automatically selected by default. Provider trust and rating
can influence ordering but cannot create the selection margin by themselves.

Automatic selection requires a strong identity and a lead of at least ten
weight units over the next candidate. The weights are not probabilities. Ties,
conflicting editions/frame rates, and subtitles with multiple equally plausible
media targets remain manual choices. Existing manual selections are preserved.
An identity match with no release evidence is labeled as release-uncertain; it
does not promise synchronization.

## Validation and extension

`test_media_identity.cpp` is a fixture corpus for parsing, identity conflicts,
release ranking, ambiguity, Unicode, explicit IDs, and fingerprint vectors.
`test_subtitle_sources.cpp` uses a local server for structured requests, fallback,
hash scoping, pagination, cancellation, and the existing download/cache behavior.
`test_ml_model.cpp` covers native metadata and asynchronous provider-aware
matching. `tst_RedesignTrackAssignments.qml` covers corrections, draft preservation,
match explanations, and the details editor at small window sizes and scaling.

This is a bounded native parser, not a claim to recognize every scene filename.
No Python/JavaScript runtime or new metadata provider is required. Additional
parser backends, including a specialized anime parser, can return the same
identity model. Evaluate additions against the fixture corpus and measure wrong
automatic assignments separately from missed suggestions. TMDB resolution and
absolute-to-season episode mapping remain possible future consumers/extensions.

## Artwork discovery and library previews

The redesigned library uses the shared identity model for artwork as well as
track assignment. Identities now include music tracks, albums, artists,
podcasts, podcast episodes and audiobooks. Artist, album artist, album,
track/disc numbers, podcast series, feed URL, episode GUID and supported
MusicBrainz identifiers retain their own fields. Audio filenames do not use
video release cleanup. Ambiguous audio identities are not inferred from a title
alone. Podcast GUIDs require supporting feed or series evidence. Existing
library metadata and already available native tags feed the same helpers;
identification does not start an online metadata search.

`artwork_discovery.hpp` handles filesystem conventions and image ownership.
It distinguishes primary covers/posters, landscape thumbnails, backdrops,
banners, logos, disc images and clear art. Image suffixes are removed before
identity comparison. Exact sibling ownership is preferred over a broader,
confirmed identity match. Conflicting remakes/episodes and equally plausible
owners are rejected. Release group, resolution, frame rate and subtitle hashes
are not artwork identity requirements.

Individual media use this order:

1. A persistent user-selected image.
2. Recognised local/sibling artwork.
3. Embedded artwork or an existing media thumbnail.
4. A video frame sampled at 15 percent, when applicable.
5. The audio/video icon.

Folders use a user-selected image, then their recognised local artwork, then
up to four distinct child artworks, then the folder icon. Conventional primary
names include `poster`, `folder`, `cover`, `default`, `movie`, `show`, `album`,
`artist`, and `jacket`. `thumb` and `landscape` are primary fallbacks. Named
siblings such as `Movie (2021)-poster.jpg` and `Show.S01E03-thumb.png` belong to
the matching media. Backdrops and logos are never substituted for primary art.
Generic folder art is not assigned to every unrelated video; audio album
covers may also be inherited from above an explicitly named Disc/CD folder.

Folder discovery is breadth first, ordered by name and independent of the view's
sort order. It examines at most 512 entries per directory, 2,048 entries overall,
32 directories, two levels below the chosen folder, and 32 preview candidates.
Symbolic links are not traversed. A child folder with valid selected/local art
represents that subtree. Missing or unreadable images are skipped, matching
movie/episode identities collapse alternate encodes, and decoded image
fingerprints prevent repeated covers filling a collage. Layouts use one image,
two columns, a full-height image beside two smaller ones, or four quadrants.

“Choose artwork…” copies a validated image into application storage, leaving
media folders untouched. “Use automatic artwork” removes that override.
`artwork_store.hpp` saves overrides independently of cached generated frames,
so newly discovered local artwork can supersede an old frame. Selected source
files can subsequently be moved or deleted. Discovery, image decoding and
composition run asynchronously; refreshes reject stale results. Watched
artwork and directory changes invalidate previews, including folder cards,
recent items, playlist media and saved sidebar shortcuts. The resolver also
emits source and matching explanations for consumers. Existing remote
thumbnails are retained; artwork discovery does not introduce remote searches.

Regression coverage lives in `test_artwork_discovery.cpp`, the shared identity
corpus, native library/resolver tests, folder-model tests and the library QML
suite.
