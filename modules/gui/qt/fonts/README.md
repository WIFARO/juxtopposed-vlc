# Inter interface font

These unmodified static OpenType fonts come from the official Inter 3.19 release:
https://github.com/rsms/inter/releases/tag/v3.19

The sampled Figma SVG text outlines match this version, including the medium-weight
settings headings. Inter 4 has different outlines and spacing.

Inter is designed by Rasmus Andersson and contributors. The included
LICENSE.txt contains the SIL Open Font License 1.1 and copyright notice.
VLC embeds regular, medium, semibold and bold styles, with their italic styles,
and loads them before creating its interface. Static styles allow Qt font
backends without named variable-font instances to select the intended weight.
An operating-system font installation is not required. Other system fonts
remain available for characters that Inter does not contain.
