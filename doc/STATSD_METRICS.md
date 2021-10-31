# dumphfdl StatsD metrics

All metrics described below reside in a common namespace:

- if receiver site name is not set (ie. `--station-id <station_name>` option is not used), then the namespace is `dumphfdl`.

- if receiver site name is set, then the namespace is `dumphfdl.<station_name>`.

Abbreviations used throughout the following sections:

- MPDU (MAC Protocol Data Unit) - a portion of data occupying a HFDL timeslot (single or double). It consists of a prekey sequence, preamble, training sequences and data fields. It is a container for one or more LPDUs.

- SPDU (Squitter Protocol Data Unit) - a portion of data occupying a single HFDL timeslot. Only sent by ground stations. An SPDU contains a Squitter message that describes the current state of the network (currently used frequencies).

- PDU (Protocol Data Unit) - MPDU or SPDU.

- LPDU (Link Protocol Data Unit) - a portion of data carried inside the MPDU. It encapsulates a HFDL control message (Logon request, Logon confirm, etc) or user data. A single uplink MPDU may contain 0 to 64 LPDUs, while a downlink MPDU may contain 0 to 15 LPDUs.

## Per-channel metrics

In the following list `<freq>` is the channel frequency (in Hertz) - for example `11184000`.

- `<freq>.demod.preamble.A2_found` (counter) - number of A2 sequences found. A2 is a pseudo-random bit sequence in the preamble of a HFDL frame. It occurs twice in every preamble. Finding a second occurrence of this sequence is a good indication that a HFDL frame has been found in the input signal.

- `<freq>.demod.preamble.M1_found` (counter) - number of M sequences found. M is a pseudo-random bit sequence in the preamble of a HFDL frame, that indicates the modulation and interleaver type used to encode the frame. This counter is incremented when these parameters have been successfully determined with a reasonable confidence level.

- `<freq>.demod.preamble.errors.M1_not_found` (counter) - incremented when the decoder is unable to determine the modulation and interleaver type for the frame.

- `<freq>.frames.processed` (counter) - number of PDUs processed by the decoder. The following equation holds true for every channel: `frames.processed = frames.good + frame.errors.*`.

- `<freq>.frames.good` (counter) - number of successfully decoded PDUs. The following equation holds true for every channel: `frames.good = frame.dir.air2gnd + frame.dir.gnd2air`.

- `<freq>.frame.errors.bad_fcs` (counter) - number of PDUs which could not be decoded due to a bad Frame Check Sequence (CRC error).

- `<freq>.frame.errors.too_short` (counter) - number of PDUs which could not be decoded due to being unreasonably short.

- `<freq>.frame.dir.air2gnd` (counter) - number of successfully decoded downlink frames (MPDUs).

- `<freq>.frame.dir.gnd2air` (counter) - number of successfully decoded uplink frames (MPDUs and SPDUs).

- `<freq>.lpdus.processed` (counter) - number of LPDUs procssed by the decoder. The following equation holds true for every channel: `lpdus.processed = lpdus.good + lpdu.errors.*`.

- `<freq>.lpdus.good` (counter) - number of successfully decoded LPDUs.

- `<freq>.lpdu.errors.bad_fcs` (counter) - number of LPDUs which could not be decoded due to a bad Frame Check Sequence (CRC error).
- `<freq>.lpdu.errors.too_short` (counter) - number of LPDUs which could not be decoded due to being unreasonably short.

## ACARS reassembly metrics

- `<freq>.acars.reasm.unknown` (counter)
- `<freq>.acars.reasm.complete` (counter)
- `<freq>.acars.reasm.skipped` (counter)
- `<freq>.acars.reasm.duplicate` (counter)
- `<freq>.acars.reasm.out_of_seq` (counter)
- `<freq>.acars.reasm.invalid_args` (counter)

These counters are incremented for every decoded ACARS message. Their names correspond to the `Reassembly status` info line printed in the header of each ACARS message. Refer to libacars documentation for details.

## Per-cache metrics

dumphfdl maintains three in-memory caches:

- `ac_fwd` - per-frequency mappings of aircraft IDs to ICAO addresses

- `ac_inv` - mappings of ICAO addresses to aircraft IDs (ie. an inverted `ac_fwd` cache)

- `ac_data` - data retrieved from basestation.sqb database

Each cache has the following set of metrics:

- `<cache_name>.entries` (gauge) - number of entries in the cache. Goes up when new entries are created in the cache. Goes down when entries are expired from the cache.
