# Aicd's acid line model

`lm.js` is the model of [Aicd](https://github.com/mganss/Aicd), the acid pattern generator Max for Live device by
Michael Ganss, as it is in that repository at commit `2afe043c7c2dcb489af85003bea21d1cd93a959a` (2023-01-25):
a maximum-likelihood character n-gram (order 18: two steps) trained on ABL3 patterns, which are not part of Aicd
or of this package.

- Licence: MIT, `../LICENSES/MIT-Aicd.txt` (Copyright (c) 2018 Michael Ganss)
- SHA-256: `4968d388e14c41f69e4d536564698c979ea9cbb338006ee8b86d6514720f0480`

`tools/gen_aicd.py` turns it into the flash tables `src/acidgen_model.h` for acidgen's MODE AICD
(`src/acidgen.c`); nothing in the model is changed but the rounding of its probabilities.
