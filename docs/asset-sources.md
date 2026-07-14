# Asset provenance and licensing

This file records the origin and redistribution terms of every non-code image
shipped with Joustix. The runtime files in `assets/` are the complete canonical
release image set. Generation sources, prompts, reference conversions,
normalization scripts, and preview montages are intentionally kept outside the
public game tree under `~/research/joustix`.

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

## Runtime inventory policy

Only the eight production PPMs above belong in the release repository. No
generator scripts, prompt logs, unused variants, preview PNGs, converted
references, or other image-pipeline artifacts are required to build or run the
game.
