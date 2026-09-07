# Third-party notices

CantoDeck application code: Copyright (c) 2026 CantoDeck contributors. Licensed under GNU Affero General Public License version 3 only. Full text in LICENSE. No warranty.

JUCE 8.0.15, commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`, Copyright Raw Material Software Limited, is used under its AGPLv3 option. Its exact source and license notices are included in the portable package's `source/JUCE-8.0.15.zip`; source URL https://github.com/juce-framework/JUCE/tree/91ad83ae34a81e0833b1a2b0866f54846370ae53. JUCE LICENSE.md was inspected at implementation time; this project does not claim MIT licensing for JUCE.

Enabled JUCE core/graphics dependencies include zlib (zlib license), libpng (PNG license), Independent JPEG Group code (IJG license), HarfBuzz (Old MIT license) and SheenBidi (Apache 2.0). This software is based in part on the work of the Independent JPEG Group. Exact copyright and license texts are copied into the package's `licenses/` directory and remain in corresponding source. The bundled source archive may contain additional JUCE optional components; their notices remain with their source. FLAC, Ogg Vorbis, MP3, ASIO, AAX, AU, VST plugin hosting and LV2 hosting are not application capabilities. No extra codec or proprietary SDK is required for the WAV path.

Windows SDK and Microsoft C++ runtime are platform/build dependencies. The binary uses the compiler's dynamic runtime; machines without the Visual C++ 2015–2022 x64 runtime may require Microsoft's official redistributable. The application does not silently install it or drivers. Build scripts require Git, CMake and MSVC; these tools are not bundled.

The package includes CantoDeck corresponding source and scripts alongside the exact JUCE source. If redistributing a modified binary, include matching corresponding source, preserve these notices and comply with AGPLv3. No binary signing is configured.
