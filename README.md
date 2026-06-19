# cs2bhop-metamod

Metamod plugin for CS2 bhop servers, based on `cs2surf-metamod`.

## Movement Modes

- `128tick` is the built-in default bhop mode.
- `CSS66tick` is packaged as an optional mode plugin under `addons/cs2bhop/modes`.

## Fake Zoning

Fake zoning is resolved from map entities. On maps without the official Mapping API, the plugin inspects spawned `trigger_multiple` entities and treats common bhop/timer names as timer zones:

- Start: `map_start`, `s1_start`, `stage1_start`, `timer_startzone`, `trigger_startzone`, `zone_start`
- End: `map_end`, `timer_endzone`, `trigger_endzone`, `zone_end`
- Bonus: `b1_start`, `bonus1_start`, `timer_bonus1_startzone`, and matching end names
- Checkpoints/stages: `map_cp1`, `map_checkpoint1`, `s2_start`, `stage2_start`

Per-map trigger aliases can be shared in `addons/cs2bhop/zones/<map>.json`:

```json
{
  "MapStartTrigger": "custom_start_trigger_name",
  "MapEndTrigger": "custom_end_trigger_name",
  "MapStartTriggers": ["alternate_start_name"],
  "MapEndTriggers": ["alternate_end_name"]
}
```

The detected trigger bounds are passed through the same timer start/end path as official Mapping API zones, so later global/API compatibility can converge on the same course and mode descriptors.

## Builds

Builds are intended to run through GitHub Actions, following the workflow structure inherited from `cs2surf-metamod`. Release artifacts contain Linux and Windows packages.
