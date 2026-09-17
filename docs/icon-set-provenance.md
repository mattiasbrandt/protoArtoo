# Icon set provenance

The operator surfaces carry no emoji (ADR 0066). Where a glyph earns its place
beside a label, it is an icon from **Material Design Icons 7.4.47**
(Pictogrammers), taken as unmodified SVG path data from the `@mdi/svg` package
and injected as an inline `<symbol>` sprite by `data/shell.js`.

The sprite is inline rather than a served `.svg` file for two reasons: an
external sprite is a second request in the opening burst the controller
deliberately sheds connections in, and the markup is smaller than the request
that would fetch it.

## Licence

Material Design Icons is released by the Pictogrammers group. The icons are
distributed under the **Apache License 2.0**
(<https://www.apache.org/licenses/LICENSE-2.0>). The package's own notice, read
from `@mdi/svg`'s `LICENSE` and reproduced verbatim:

> ## Pictogrammers Free License
>
> This icon collection is released as free, open source, and GPL friendly by
> the [Pictogrammers](http://pictogrammers.com/) icon group. You may use it
> for commercial projects, open source projects, or anything really.
>
> ### Icons: Apache 2.0 (https://www.apache.org/licenses/LICENSE-2.0)
> Some of the icons are redistributed under the Apache 2.0 license. All other
> icons are either redistributed under their respective licenses or are
> distributed under the Apache 2.0 license.
>
> ### Fonts: Apache 2.0 (https://www.apache.org/licenses/LICENSE-2.0)
> All web and desktop fonts are distributed under the Apache 2.0 license. Web
> and desktop fonts contain some icons that are redistributed under the Apache
> 2.0 license. All other icons are either redistributed under their respective
> licenses or are distributed under the Apache 2.0 license.
>
> ### Code: MIT (https://opensource.org/licenses/MIT)
> The MIT license applies to all non-font and non-icon files.

The paths are not covered by this repository's MIT grant; see LICENSE scope
item 5.

## The paths this firmware carries

Only the symbols the chrome and a swept surface actually draw are in the image:
a `<symbol>` nothing references is bytes in a 640 KiB filesystem for no reason.
`tools/check_surface_anatomy.py` fails the build on a reference that resolves to
no symbol, which is the failure this list would otherwise hide.

| MDI name | Where it is drawn |
|---|---|
| `view-dashboard-outline` | Dashboard, in the nav rail |
| `steering` | Foot Drive, in the nav rail |
| `rotate-360` | Dome, in the nav rail |
| `volume-high` | Sound, in the nav rail |
| `controller-classic-outline` | RC Control, in the nav rail |
| `timeline-outline` | Sequences, in the nav rail |
| `tune-variant` | Setup, in the nav rail |
| `puzzle-outline` | Parts, in the nav rail |
| `connection` | Wiring, in the nav rail |
| `robot-outline` | Servos, in the nav rail |
| `wifi` | WiFi, in the nav rail |
| `chip` | Firmware, in the nav rail |
| `stop-circle-outline` | the Latching Estop, in the topbar |
| `power-sleep` | the Dashboard's Sleep act |
| `restart` | the Dashboard's Restart act |
| `console-line` | reserved for the Controller Console |
| `chevron-right` | a disclosure's open/closed marker |

The #398 prototype (`prototypes/395-surface-anatomy/chrome.js`, merged) holds
the full twenty-two paths it took from the same package, including the five this
image does not carry - `wrench-outline`, `arrow-left`, `arrow-right`,
`play-outline` and `pause`. A slice that needs one copies its path in beside the
others rather than fetching a package to read it again.

`connection` is the first of those six a slice has needed. Wiring (#350) is the
destination the prototype's own nav row named `connection`, so landing that
surface was one path copied in and no package fetched.

`robot-outline` is the one choice the prototype did not make for us: it left
Servos out of the rail because `CONTEXT.md` **Activity Group** does not list it
and its fate is #364's, so the shipped nav needed an icon the reference had no
row for. It was picked from the twenty-two already taken rather than adding a
twenty-third from an unread source.
