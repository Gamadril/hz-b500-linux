# Target system fonts

Unmodified font files copied from the host's Debian font packages on 2026-10-21.
These are source assets for the `hz-ui-fonts` package; generated caches are not
included. Each font is distributed under SIL Open Font License 1.1. The complete
Debian copyright records, including upstream font notices and license texts, are
retained in `notices/` and installed with the fonts.

| File | Source | SHA-256 |
| --- | --- | --- |
| `NotoSans-Regular.ttf` | https://github.com/notofonts/noto-fonts (Debian `fonts-noto-core`; original source URL in notice) | `89c3c497f618fdaa0b2d1e98fef93582f28c71debd2c4a8cdf41f190ced2909d` |
| `fontawesome-webfont.ttf` | https://github.com/FortAwesome/Font-Awesome/tree/v4.7.0 (Debian `fonts-font-awesome`) | `aa58f33f239a0fb02f5c7a6c45c043d7a9ac9a093335806694ecd6d4edc0d6a8` |

FreeType accesses the installed files at runtime. The Noto Sans CJK font was
removed to save 19 MB; Asian characters in tags or device names show as boxes.
No font is modified, subsetted or converted into generated C glyph arrays.
Desktop builds continue using installed host fonts.
