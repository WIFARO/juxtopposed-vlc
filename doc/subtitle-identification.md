# Media identification and remote subtitles

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
