# Factory patches

These 45 files are the factory patches from **SwarmSynth** by *Anarchy Sound
Software* (2003, released as freeware in 2012).

They were extracted from the plugin's own resources by
`analysis/extract_presets.sh`, which walks its Presets menu, loads each entry
and dumps the resulting state chunk. Each file is a verbatim 1908-byte state
chunk exactly as the original plugin produced it, so the parameter values in
them are the original author's work rather than this project's.

They are here so the recreation can be measured against real patches instead of
synthetic test cases, and so anyone reading along can hear what the synth is
meant to sound like.

The format is documented in
[`analysis/FINDINGS.md`](../../analysis/FINDINGS.md). Both engines load them:
the original through `vsthost32 --chunk`, the recreation through
`swarmrender --preset` or the Presets button.

If you are the rights holder and would rather these were not here, open an issue
and they will be removed.
