# Asset provenance and licensing

This file records the origin and redistribution terms of the non-code assets
shipped with Joustix. The runtime files in `assets/` are the complete canonical
release set. Generation sources, prompts, raw responses, reference conversions,
normalization scripts, and preview material are intentionally kept outside the
public game tree.

The images were created specifically for Joustix with OpenAI image generation
and are distributed under the repository's MIT License to the extent copyright
or related rights exist. OpenAI is a tool provider, not an author, sponsor, or
endorser of Joustix.

## Production images

| Runtime file | Content | Release processing |
|---|---|---|
| `assets/player.ppm` | Eight-pose gold knight and cream ostrich atlas | Selected, point-resized, chroma-key normalized |
| `assets/bounder.ppm` | Eight-pose rust raider and olive buzzard atlas | Selected, point-resized, chroma-key normalized |
| `assets/hunter.ppm` | Eight-pose pale-steel huntress and slate hawk atlas | Selected, point-resized, chroma-key normalized |
| `assets/shadow.ppm` | Eight-pose black-violet lord and dark vulture atlas | Selected, point-resized, chroma-key normalized |
| `assets/props.ppm` | Intact/cracked eggs and two lava-troll poses | Selected, point-resized, chroma-key normalized |
| `assets/platform.ppm` | Modular basalt-and-bronze ledge | Selected, cropped, padded, point-resized |
| `assets/stage.ppm` | Moonlit volcanic cavern arena | Selected, gameplay-aligned crop, point-resized |
| `assets/gameover.ppm` | Fallen helmet and broken lance defeat scene | Selected, point-resized |

The generation briefs required original retro pixel art and prohibited copied
game sprites, screenshots, logos, recognizable copyrighted characters,
third-party artwork, text, and watermarks. Every selected image was visually
reviewed and materially prepared through layout selection, cropping, atlas
normalization, chroma-key handling, and binary PPM conversion.

Each rider atlas is a 2-column by 4-row sheet. Frames 1-4 form a generated
four-step ground run, followed by glide, wing-up, wing-down, and dive poses.

## Runtime audio assets

The 40 WAV files under `assets/sfx/` form ten cue-specific variation banks:
menu motion, giant-bird wing flaps and stone footsteps, armored landings and
lance collisions, bird damage, egg settling and hatching, a real-brass wave
fanfare, and a molten-lava fall. They were generated specifically for Joustix
with ElevenLabs Text to Sound Effects v2 (`eleven_text_to_sound_v2`) during the
account owner's paid Starter subscription.

The production field contained 60 candidates. A pinned LAION CLAP model
provided semantic triage, and candidate score arrays were identical across two
independent runs. Cue-shape checks rejected implausible onset, repeated-event,
stationary-noise, clipping, and tail behavior. Selected sources were decoded,
onset-aligned, downmixed with equal power, trimmed or padded to exact runtime
length, DC/high-pass cleaned, edge-faded, and reconstructed-peak gain-staged.
No procedural layer, library sample, or third-party recording was mixed into
the masters. The C synthesizer remains only as a missing-asset fallback.

Runtime files are mono 44.1 kHz signed 16-bit PCM WAV. The game loads every
`_vNN` file into a bank and avoids immediate repeats. Automated QA passed all
40 files for format, duration, headroom, silence/DC, fades, and duplicates.

ElevenLabs states that qualifying paid-plan output may be used commercially
and indefinitely. Its service-specific restrictions still apply, including a
prohibition on standalone commercial distribution or licensing of Sound
Effects output. The WAVs are therefore excluded from this repository's MIT
grant and included only as bundled Joustix content, not as a sample pack or
sound library. Terms were checked on 2026-07-14: [paid-plan commercial
use](https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform),
[Terms of Service](https://elevenlabs.io/terms-of-use), [Sound Effects
Terms](https://elevenlabs.io/sound-effects-terms), and [Prohibited Use
Policy](https://elevenlabs.io/use-policy). Exact prompts, source/final hashes,
selection metrics, and mastering settings are in
[`audio-provenance.json`](audio-provenance.json).

## Runtime inventory policy

Only the eight production PPMs and 40 production WAVs above belong in the
release repository. No generator scripts, prompt logs, raw responses, unused
variants, preview media, converted references, or other pipeline artifacts are
required to build or run the game.
