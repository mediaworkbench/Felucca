# DrumBrain's drum pattern model

`model.js` is the model of [DrumBrain](https://github.com/mganss/DrumBrain), the drum pattern generator Max for Live
device by Michael Ganss, as it is in that repository at commit `95a7cb479cb8e46ade51378247d1da91b73598b7`: a Lempel-Ziv
continuation dictionary per style (ALL, HOUSE, BREAKS) and instrument (AC, then BD SD LT MT HT RS CP CB CY OH CH), built
from about 250 808 / 909 patterns, which are not part of DrumBrain or of Felucca.

- Licence: MIT, `LICENSES/MIT-DrumBrain.txt` (Copyright (c) 2019 Michael Ganss)
- SHA-256: `85e9cf40e248f123e4cf102d03055a076c82a088494168c0f7114d19d3e84454`

`tools/gen_dbrain.py` turns it into the firmware's flash tables (`build/gen/felucca_dbrain.h`) for a DRUM track's BRAIN
pages (`firmware/src/dbrain.c`); nothing in the model is changed but the rounding of its probabilities.
