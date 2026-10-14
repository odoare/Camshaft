# Licensing

Camshaft follows FxmeTools' terms (see FxmeTools' `LICENSE.md`
for the reasoning): two halves under two licences, stated in each file's header
as an SPDX identifier.

```
Source/Engine/   LGPL-3.0-or-later
everything else  AGPL-3.0-or-later, or commercial terms from the author
```

## Source/Engine: LGPL-3.0-or-later

The sound engine does not include a JUCE header, link a JUCE library or name a
JUCE symbol; it builds against FxmeTools' JUCE-free core alone (the
`CamshaftEngineTest` target proves it). It is not a derivative of JUCE and its
licence is independent of the framework's.

    SPDX-License-Identifier: LGPL-3.0-or-later

Full text: FxmeTools' `core/LICENSE` (with the GPLv3 text it
builds on, `core/LICENSE.GPL`).

## The plugin: AGPL-3.0-or-later, or commercial

The plugin shell, its editor and the effect hosting compile against JUCE and
are bound by its terms. Like JUCE 8 and FxmeTools' JUCE module, they are
dual-licensed:

    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial

- Under the AGPLv3 (full text: FxmeTools' `FxmeTools/LICENSE`):
  distribute freely with the complete source, under the same licence.
- Under commercial terms from the author, for those holding a commercial JUCE
  licence. Contact via [github.com/odoare](https://github.com/odoare) or
  www.fx-mechanics.com. A commercial grant covers this code only, not JUCE.

The FxmeFX effects compiled into the plugin carry their own terms (see
FxmeFX's `LICENSE.md`); WDL is under the zlib licence (Cockos).

---

Copyright (c) 2026 Olivier Doaré.
