# WM-BUS — Wireless M-Bus receiver for LakeShark (LR2021)

A `.lap` app for LakeShark that receives Wireless M-Bus meter telegrams
(EN 13757-4 / OMS) with the LR2021 radio, decrypts them with an AES-128 key and
shows the readings (volume, energy, power, dates, error flags…).

## What it handles

| Layer | Coverage |
|---|---|
| Radio | **T1 + C1** together at 868.950 MHz (100 kb/s, ±50 kHz, shared sync word `0x543D`), **S1** at 868.300 MHz (32.768 kchip/s, Manchester), **T1** at 868.300 MHz (e.g. Sagemcom XT211 electricity meters) |
| Coding | 3-of-6 (T1), NRZ (C1), Manchester (S1, both conventions) |
| Frames | format A (T1, C1, S1) and format B (C1), CRC-16 blocks (EN 13757) checked and removed; a frame longer than the radio packet is taken in whole CRC blocks; in T1, corrupted 3-of-6 symbols are repaired with the block CRC |
| Layers | link layer, ELL (CI 8C/8D/8E), AFL (CI 90, message counter), short TPL 7A, long 72, none 78 |
| Encryption | **mode 5** (AES-128-CBC, IV = M+A+ACC×8), **mode 7** (AES-128-CBC, key from the AES-CMAC KDF, IV = 0), **ELL AES-128-CTR** |
| Data | DIF/DIFE/VIF/VIFE records: volume, energy (including export), mass, power, flow, temperatures, pressure, times, dates (types G, F, I), text, BCD, int8–64, real32, FD/FB tables (selected), manufacturer data |
| Techem | open manufacturer frames: Vario 4 type 4.5.1 heat meters (TCH 27/04, 27/C3, 17/C3) and Compact V (TCH 45/04, 45/C3, 22/43, 45/43, 39/43), FHKV data III heat cost allocators (TCH 69/80, 94/80), radio 3/4 water meters (TCH 74, 95, 70 / 62, 72) |
| Public keys | manufacturers' built-in factory keys, tried automatically (see below) |

Key check: decrypted data must start with `2F 2F` (modes 5/7), or the payload
CRC must match (ELL).

## Installing

Copy `wmbus.lap` to `/sdcard/apps/` on the SD card, or use `lsdk`:

```
lsdk install --open        # in the wmbus/ folder
```

Building from source (LakeShark SDK + ESP-IDF 5.5 RISC-V compiler):

```
python <LakeShark>/app-sdk/lap.py build wmbus -o wmbus.lap
```

On first start the firmware asks for radio access (RADIO RX). The app only
receives; it never transmits.

## Controls

| Key | Screen | Action |
|---|---|---|
| ↑ ↓ PgUp PgDn | list | pick a meter |
| ENTER / tap | list | meter details |
| `m` MODE | list | T1+C1 868.95 → S1 868.3 → T1 868.3 |
| `k` KEY | list, details | AES key for the selected meter |
| `y` KEYS | list | key list: `a` add, `d` delete |
| `g` LOG | list | log telegrams to the SD card |
| `o` OPTIONS | list | mode, log, FREQ TRIM, TRIM SCAN, RADIO SCAN, RAW IN LOG, new-meter beep, public keys, read keys.txt, clear the list |
| `x` | list | results of the last scan |
| `h` HEX | details | raw telegram (after decryption) |
| `b` / BACK | details, keys, scan | back |

STATE column: `plain` (not encrypted), `AES ok` (your key), `AES pub` (a built-in
public key), `no key` (no key), `bad key` (the key does not fit), `unsupp.`
(unsupported mode or layer).

## Keys

A key is 16 bytes, i.e. 32 hex digits (spaces, `:` and `-` are ignored).

1. **On the board**: pick a meter → `k` → type the key. Or `y` → `a` → the meter
   ID (8 hex digits, as in the list) or `*` (the default key for every meter)
   → the key.
2. **File** `/sdcard/wmbus/keys.txt`, read at every start of the app and with
   OPTIONS → READ KEYS.TXT:

   ```
   # meter ID      AES-128 key
   12345678        000102030405060708090A0B0C0D0E0F
   87654321        00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF
   *               <default key for the other meters>
   ```

Keys are stored in the app's data (`lap_save`, slot `keys`) on the SD card in
plain form. Do not leave the card in untrusted hands.

A key can be given for the ID in the list (link layer) or for the meter's own
ID. They differ when the telegram comes through a radio converter or repeater
(the list shows the converter's ID, the details "inner meter …"). The app looks
first for a key for the meter ID from the long transport header (CI 72), then
for the ID in the list, last for the default key `*`.

## Public keys

Some manufacturers ship their meters with one shared factory key that is not
secret: the installer can change it, but often does not. Such keys are built
into the app and tried automatically when a telegram is encrypted and your key
is missing or does not fit:

| Name | AES-128 key | For | Modes | Source |
|---|---|---|---|---|
| Diehl default | `51728910E66D83F851728910E66D83F8` | Diehl Metering and group brands: DME, HYD (Hydrometer), SAP (Sappel), SPL, EWT, e.g. Hydrus, Sharky, IZAR in OMS mode | 5 | wmbusmeters, `src/manufacturer_specificities.cc` (`addDefaultManufacturerKeyIfAny`) |
| zero key | `00000000000000000000000000000000` | any meter; the factory key of e.g. Apator AT-WMBUS-16-2 (APA 05/07), B METERS Hydrodigit / Hydrocal-M3 (BMT), Apator/Amiteq UniSmart (AMX) | 5, 7, ELL | wmbusmeters driver tests, MeterRadioBridge compatibility list |

A result with a public key counts only when the decrypted data starts with
`2F 2F` **and** its data records decode cleanly. The `2F2F` check alone would
let one wrong key in 65,536 through.

Such a meter shows `AES pub` in the list, and its details name the key. To turn
them off: OPTIONS → PUBLIC KEYS (remembered). The built-in keys are also listed
at the bottom of the KEYS screen (`y`).

## Particular meters

**Sagemcom XT211.** This electricity meter sends every ~10 s at 868.3 MHz in
T-mode coding: choose MODE `T1 868.3`. Its telegram (209 bytes, OMS mode 7 with
AFL) is encrypted with a key from the grid operator, entered like any other.
The mode receives at 99,920 b/s, the rate these meters send at, because the
LR2021 receiver tolerates little clock difference. On air the telegram takes
356 bytes while the radio hands over 255, so the app gets the first 8 CRC blocks
and decrypts the first 96 bytes of data: date and time, serial number, imported
energy (total and tariffs 1–3), exported energy and current power. Exported
power and the power limits do not fit. The details then show
`received: first 138 of 209 bytes` and `truncated`.

**Techem.** Techem publishes no shared AES key. Some devices still send their
billing data in an unencrypted manufacturer format:

- Vario 4 type 4.5.1 and Compact V heat meters: total energy, energy this
  period and last period. Vario sends MJ, shown as GJ; Compact V sends and
  shows kWh. Vario 4.5.1 MID (TCH 17/04) sends an OMS telegram in C1 and, a few
  seconds later, its own 17/C3 frame in T1;
- FHKV data III heat cost allocators: HCA units this period and last period,
  reading date, end of the last period, room and radiator temperature;
- radio 3/4 water meters: total volume, volume this period and last period,
  dates.

The same devices alternate with encrypted OMS telegrams under the same ID. The
list then keeps the latest telegram with readings, and the details show
`newest (…) encrypted; shown: earlier one`. Encrypted telegrams need the
meter's individual key, which Techem gives to the building manager; enter it
like any other key.

## Receiver tuning

- **FREQ TRIM** (−40…+40 kHz in 5 kHz steps, remembered, shared by all modes)
  shifts this app's receive frequency, e.g. for an LR2021 module without a TCXO
  or a meter that is off frequency.
- **TRIM SCAN** goes through every FREQ TRIM value for 30 s each and keeps the
  one that let the most data through.
- **RADIO SCAN** goes through 12 receiver settings (preamble, bitrate,
  deviation, bandwidth) for 30 s each in a T mode, then returns to the standard
  settings.

Both scans show a table (`x` from the list): frames, bytes passing CRC, longest
frame and repaired symbols. With LOG on they also write it to the log as
`SCAN;…` lines. They are useful for a meter that sends often.

## Log

LOG writes every valid telegram to `/sdcard/wmbus/telegrams.log`:

```
2026-11-20 10:43:12;T1;-78;12345678;APA;07;AES ok;2E440106785634120507...
```

The last field is the telegram without CRC bytes (L field recalculated as for
format A), readable by e.g. `wmbusmeters --analyze`. OPTIONS → RAW IN LOG (not
remembered) also writes every radio packet in full, including failed ones:
`+ms;RAW;dBm;result;255 bytes hex`.

## Limitations

- The radio packet has a fixed length of 255 bytes (the firmware's FSK API has
  no variable length). That covers T1 telegrams up to about 170 bytes and C1 up
  to about 250 bytes with CRCs. Longer ones are read in part.
- Mode N (169 MHz), two-way modes (T2/C2/S2: receiving works, sending does
  not), AFL fragmentation, security modes other than 0/5/7 and ELL encrypted
  with the receiver address (CI 8F) are not supported. The mode 7 MAC is not
  verified (only decryption and the `2F2F` check).
- Manufacturer protocols (e.g. Diehl PRIOS) and Techem variants not listed
  above are shown as raw data (`unsupp. CI 0xA2` and so on).
- The radio listens on one frequency at a time: meters at 868.95 MHz and
  868.3 MHz need a mode change.

## Files

| File | Contents |
|---|---|
| `main.c` | UI, FSK radio session, meter table, keys, log, scans |
| `wm_proto.c/.h` | physical layer, CRC, layer parser, decryption, manufacturer formats, DIF/VIF |
| `wm_aes.c/.h` | AES-128, CBC, CTR, CMAC |

Host tests (FIPS-197, SP 800-38A, RFC 4493 and CRC vectors, full frames) are in
the source repository in `tests/test_wmbus.c`; any C11 compiler builds them:

```
cc -std=c11 -Iwmbus tests/test_wmbus.c wmbus/wm_aes.c wmbus/wm_proto.c -o test_wmbus && ./test_wmbus
```

## Author

zenobik — zenobik@gmail.com. MIT license (file `LICENSE`).
