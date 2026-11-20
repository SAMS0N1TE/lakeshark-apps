/* Wireless M-Bus receive path, written from EN 13757-3/-4/-7 and OMS
 * Specification Vol. 2. See wm_proto.h. */
#include "wm_proto.h"
#include "wm_aes.h"
#include <stdio.h>
#include <string.h>

/* ---- small helpers --------------------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char *wm_phy_name(int phy)
{
    switch (phy) {
    case WM_PHY_T1: return "T1";
    case WM_PHY_C1A: return "C1a";
    case WM_PHY_C1B: return "C1b";
    case WM_PHY_S1: return "S1";
    default: return "?";
    }
}

const char *wm_dec_name(int dec)
{
    switch (dec) {
    case WM_DEC_PLAIN: return "plain";
    case WM_DEC_OK: return "AES ok";
    case WM_DEC_NOKEY: return "no key";
    case WM_DEC_BADKEY: return "bad key";
    default: return "unsupp.";
    }
}

/* CRC-16 of EN 13757: polynomial 0x3D65, initial 0, complemented; sent high byte first. */
uint16_t wm_crc(const uint8_t *d, size_t n)
{
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(d[i] << 8);
        for (int b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x3D65 : c << 1);
    }
    return (uint16_t)~c;
}

static bool crc_ok(const uint8_t *d, size_t n) { return wm_crc(d, n) == (uint16_t)(d[n] << 8 | d[n + 1]); }

/* ---- CRC blocks -------------------------------------------------------- */

/* Bytes on air of a format A frame with this L field, CRCs included. */
static size_t len_a(uint8_t l) { return (size_t)l + 1 + 2 * (1 + ((size_t)l - 9 + 15) / 16); }

int wm_strip_crc(const uint8_t *in, size_t n, int format, wm_frame_t *out)
{
    if (n < 1) return WM_E_SHORT;
    const uint8_t l = in[0];
    size_t o = 0;
    bool cut = false;
    if (format == 'A') {
        if (l < 9) return WM_E_LEN;
        if (n < 12) return WM_E_SHORT;
        if (!crc_ok(in, 10)) return WM_E_CRC;
        memcpy(out->data, in, 10);
        o = 10;
        size_t pos = 12, left = (size_t)l - 9;
        while (left) {
            const size_t blk = left > 16 ? 16 : left;
            if (pos + blk + 2 > n) {    /* the packet ends here: keep the blocks so far */
                cut = true;
                break;
            }
            if (!crc_ok(in + pos, blk)) return WM_E_CRC;
            memcpy(out->data + o, in + pos, blk);
            o += blk;
            pos += blk + 2;
            left -= blk;
        }
        if (cut && o < 26) return WM_E_SHORT;   /* not even one data block */
    } else {
        const size_t total = (size_t)l + 1;
        if (total < 12) return WM_E_LEN;
        if (total > n) return WM_E_SHORT;
        if (total <= 128) {
            if (!crc_ok(in, total - 2)) return WM_E_CRC;
            memcpy(out->data, in, total - 2);
            o = total - 2;
        } else {
            if (total < 131 || !crc_ok(in, 126) || !crc_ok(in + 128, total - 130)) return WM_E_CRC;
            memcpy(out->data, in, 126);
            memcpy(out->data + 126, in + 128, total - 130);
            o = 126 + total - 130;
        }
    }
    out->format = (uint8_t)format;
    out->len = (uint16_t)o;
    out->cut = cut;
    out->fixed = 0;
    out->data[0] = cut ? l : (uint8_t)(o - 1);
    return WM_OK;
}

/* ---- physical layer ------------------------------------------------------ */

/* 3-of-6: each nibble is six chips with three ones (EN 13757-4 table). */
static const uint8_t CODE36[16] = { 22, 13, 14, 11, 28, 25, 26, 19, 44, 37, 38, 35, 52, 49, 50, 41 };

static int dec36(uint8_t sym)
{
    for (int i = 0; i < 16; i++)
        if (CODE36[i] == sym) return i;
    return -1;
}

static int bits_at(const uint8_t *raw, size_t bit, int count)
{
    int v = 0;
    for (int i = 0; i < count; i++, bit++) v = v << 1 | ((raw[bit >> 3] >> (7 - (bit & 7))) & 1);
    return v;
}

/* One byte = two symbols = 12 chips, high nibble first. */
static int dec36_byte(const uint8_t *raw, size_t index)
{
    const int hi = dec36((uint8_t)bits_at(raw, index * 12, 6));
    const int lo = dec36((uint8_t)bits_at(raw, index * 12 + 6, 6));
    return hi < 0 || lo < 0 ? -1 : hi << 4 | lo;
}

static int ones6(unsigned v)
{
    int k = 0;
    for (; v; v &= v - 1) k++;
    return k;
}

/* Nibbles a received 3-of-6 symbol may have been: itself when it is a code;
   else the codes one chip away (two or four ones: one chip flipped), or two
   chips away (three ones but not a code). Weak T1 telegrams come with single
   chip errors now and then. */
static int sym_cands(unsigned s, uint8_t *c)
{
    const int d = dec36((uint8_t)s);
    if (d >= 0) {
        c[0] = (uint8_t)d;
        return 1;
    }
    const int want = ones6(s) == 3 ? 2 : 1;
    int k = 0;
    for (int i = 0; i < 16; i++)
        if (ones6(s ^ CODE36[i]) == want) c[k++] = (uint8_t)i;
    return k;
}

enum { T1_CODING = -1, T1_CRC = -2, T1_MAX_TRIES = 64 };

/* One CRC block of a T1 frame: n bytes from byte `first` (the data and its two
   CRC bytes). Bad symbols are replaced by their candidates until the CRC holds,
   at most T1_MAX_TRIES combinations, which keeps the chance of a wrong block
   passing near 1 in 1000. Returns the number of symbols repaired or T1_*. */
static int t1_block(const uint8_t *raw, size_t first, size_t n, uint8_t *out)
{
    uint8_t c[36][16], k[36], at[36];
    const size_t ns = 2 * n;
    unsigned tries = 1;
    int fixed = 0;
    for (size_t i = 0; i < ns; i++) {
        k[i] = (uint8_t)sym_cands((unsigned)bits_at(raw, (2 * first + i) * 6, 6), c[i]);
        at[i] = 0;
        if (!k[i]) return T1_CODING;
        if (k[i] > 1) fixed++;
        if (tries * k[i] > T1_MAX_TRIES) return T1_CODING;
        tries *= k[i];
    }
    for (unsigned t = 0; t < tries; t++) {
        for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(c[2 * i][at[2 * i]] << 4 | c[2 * i + 1][at[2 * i + 1]]);
        if (crc_ok(out, n - 2)) return fixed;
        for (size_t i = 0; i < ns; i++) {
            if (++at[i] < k[i]) break;
            at[i] = 0;
        }
    }
    return fixed ? T1_CODING : T1_CRC;
}

int wm_phy_decode_tc(const uint8_t *raw, size_t n, wm_frame_t *out)
{
    out->fixed = 0;
    if (n >= 3 && raw[0] == 0x54 && (raw[1] == 0xCD || raw[1] == 0x3D)) {
        const int fmt = raw[1] == 0xCD ? 'A' : 'B';
        const int e = wm_strip_crc(raw + 2, n - 2, fmt, out);
        if (e == WM_OK) out->phy = fmt == 'A' ? WM_PHY_C1A : WM_PHY_C1B;
        return e;
    }
    /* T1: 010101 is not a 3-of-6 symbol, so 54xx above was never T1. */
    const size_t max_bytes = n * 8 / 12;
    if (max_bytes < 12) return WM_E_SHORT;
    const int l = dec36_byte(raw, 0);
    if (l < 0) return WM_E_CODING;
    if (l < 9) return WM_E_LEN;
    uint8_t blk[18];
    int fixed = t1_block(raw, 0, 12, blk);
    if (fixed < 0) return fixed == T1_CODING ? WM_E_CODING : WM_E_CRC;
    memcpy(out->data, blk, 10);
    size_t o = 10, pos = 12, left = (size_t)l - 9;
    bool cut = false;
    int err = WM_OK;
    while (left) {
        const size_t b = left > 16 ? 16 : left;
        if (pos + b + 2 > max_bytes) {  /* the packet ends inside the frame */
            cut = true;
            err = WM_E_SHORT;
            break;
        }
        const int r = t1_block(raw, pos, b + 2, blk);
        if (r < 0) {                    /* a block past repair: keep the ones before */
            cut = true;
            err = r == T1_CODING ? WM_E_CODING : WM_E_CRC;
            break;
        }
        fixed += r;
        memcpy(out->data + o, blk, b);
        o += b;
        pos += b + 2;
        left -= b;
    }
    if (cut && o < 26) return err;      /* not even one data block */
    out->phy = WM_PHY_T1;
    out->format = 'A';
    out->len = (uint16_t)o;
    out->cut = cut;
    out->fixed = (uint8_t)(fixed > 255 ? 255 : fixed);
    out->data[0] = cut ? (uint8_t)l : (uint8_t)(o - 1);
    return WM_OK;
}

size_t wm_phy_used_tc(const wm_frame_t *f, size_t n)
{
    if (f->cut) return n;
    size_t used;
    if (f->phy == WM_PHY_C1B)
        used = 2 + (f->len <= 126 ? (size_t)f->len + 2 : (size_t)f->len + 4);
    else if (f->phy == WM_PHY_C1A)
        used = 2 + len_a(f->data[0]);
    else
        used = (len_a(f->data[0]) * 12 + 7) / 8;
    return used < n ? used : n;
}

size_t wm_find_sync_tc(const uint8_t *raw, size_t n, size_t from, uint8_t *out, size_t cap, size_t *at)
{
    uint32_t win = 0;
    const size_t end = n * 8;
    for (size_t b = from * 8, got = 0; b < end; b++, got++) {
        win = ((win << 1) | (uint32_t)((raw[b >> 3] >> (7 - (b & 7))) & 1)) & 0xFFFFFFu;
        if (got < 23 || win != 0x55543Du) continue;
        const size_t start = b + 1;
        size_t k = 0;
        for (; k < cap && start + 8 * (k + 1) <= end; k++) out[k] = (uint8_t)bits_at(raw, start + 8 * k, 8);
        if (!k) return 0;
        *at = start / 8;
        return k;
    }
    return 0;
}

int wm_phy_decode_s(const uint8_t *raw, size_t n, wm_frame_t *out)
{
    uint8_t bytes[WM_FRAME_MAX / 2 + 8];
    int err = WM_E_CODING;
    for (int pol = 0; pol < 2; pol++) {
        size_t got = 0;
        for (size_t i = 0; i + 1 < n && got < sizeof(bytes); i += 2) {
            const unsigned w = (unsigned)raw[i] << 8 | raw[i + 1];
            unsigned v = 0;
            bool ok = true;
            for (int k = 0; k < 8; k++) {
                const unsigned pair = (w >> (14 - 2 * k)) & 3;
                if (pair != 1 && pair != 2) { ok = false; break; }
                v = v << 1 | ((pair == 2) ^ (unsigned)pol);
            }
            if (!ok) break;
            bytes[got++] = (uint8_t)v;
        }
        if (!got) continue;
        err = wm_strip_crc(bytes, got, 'A', out);
        if (err == WM_OK) {
            out->phy = WM_PHY_S1;
            return WM_OK;
        }
    }
    return err;
}

/* ---- public keys --------------------------------------------------------- */

/* Only keys with a public, checkable source, each verified on telegrams from
   that source (tests/test_wmbus.c):
   - Diehl default: the OMS default key of Diehl Metering meters (Hydrus, Sharky,
     IZAR in OMS mode; also sold as Hydrometer and Sappel). It is the second
     PRIOS default key twice; wmbusmeters, src/manufacturer_specificities.cc
     (addDefaultManufacturerKeyIfAny) uses it for mode 5 telegrams of DME, EWT,
     HYD, SAP and SPL.
   - All zeros: the factory key of Apator AT-WMBUS-16-2 (APA 05/07), B METERS
     Hydrodigit / Hydrocal-M3 (BMT) and Apator/Amiteq UniSmart gas (AMX), per
     the wmbusmeters test telegrams and the MeterRadioBridge compatibility list.
     Installers often leave it, so it is tried for every meter. */
static const wm_pubkey_t PUBKEYS[] = {
    { "Diehl default", "Diehl Hydrus, Sharky, IZAR (OMS)",
      { WM_MFR('D', 'M', 'E'), WM_MFR('E', 'W', 'T'), WM_MFR('H', 'Y', 'D'), WM_MFR('S', 'A', 'P'),
        WM_MFR('S', 'P', 'L') },
      WM_PK_MODE5,
      { 0x51, 0x72, 0x89, 0x10, 0xE6, 0x6D, 0x83, 0xF8, 0x51, 0x72, 0x89, 0x10, 0xE6, 0x6D, 0x83, 0xF8 } },
    { "zero key", "Apator, B METERS, UniSmart; default",
      { 0 },
      WM_PK_MODE5 | WM_PK_MODE7 | WM_PK_ELL,
      { 0 } },
};
#define NPUB ((int)(sizeof(PUBKEYS) / sizeof(PUBKEYS[0])))

int wm_pubkey_count(void) { return NPUB; }
const wm_pubkey_t *wm_pubkey_get(int i) { return i >= 0 && i < NPUB ? &PUBKEYS[i] : NULL; }

static bool pub_fits(const wm_pubkey_t *k, uint8_t mode, uint16_t m1, uint16_t m2)
{
    if (!(k->modes & mode)) return false;
    if (!k->mfr[0]) return true;
    for (int i = 0; i < 6 && k->mfr[i]; i++)
        if (k->mfr[i] == m1 || k->mfr[i] == m2) return true;
    return false;
}

/* One attempt with one key: decrypt from the saved ciphertext, then check. */
typedef bool (*try_fn)(wm_msg_t *m, const uint8_t *key, const void *st, bool strict);

/* The user's key, then (WM_TRY_PUBLIC) the public keys that fit. Leaves the
   decrypted data in m->buf on success. Returns WM_DEC_OK, _BADKEY or _NOKEY. */
static int with_keys(wm_msg_t *m, const uint8_t *ukey, unsigned flags, uint8_t mode, uint16_t mfr2,
                     try_fn fn, const void *st)
{
    m->pubkey = -1;
    if (ukey && fn(m, ukey, st, false)) return WM_DEC_OK;
    if (flags & WM_TRY_PUBLIC)
        for (int i = 0; i < NPUB; i++) {
            if (!pub_fits(&PUBKEYS[i], mode, m->mfr, mfr2)) continue;
            if (ukey && !memcmp(ukey, PUBKEYS[i].key, 16)) continue;
            if (fn(m, PUBKEYS[i].key, st, true)) {
                m->pubkey = (int8_t)i;
                return WM_DEC_OK;
            }
        }
    return ukey ? WM_DEC_BADKEY : WM_DEC_NOKEY;
}

/* A key nobody typed for this meter must also give data records that parse:
   the 2F2F check alone lets one wrong key in 65536 through. */
static bool records_hold(const uint8_t *d, size_t n)
{
    static wm_rec_t tmp[24];
    bool bad = true;
    const int k = wm_records(d, n, tmp, 24, &bad);
    return k > 0 && !bad;
}

typedef struct {
    size_t off, n;              /* encrypted part: buf[off..n) */
    uint8_t ct[WM_FRAME_MAX];
    uint8_t iv[16];
} ell_try_t;

static bool ell_try(wm_msg_t *m, const uint8_t *key, const void *stp, bool strict)
{
    (void)strict;               /* the payload CRC is the check */
    const ell_try_t *st = stp;
    uint8_t *d = m->buf;
    memcpy(d + st->off, st->ct, st->n - st->off);
    uint8_t iv[16];
    memcpy(iv, st->iv, 16);
    wm_aes_ctr(key, iv, d + st->off, st->n - st->off);
    const uint16_t pc = wm_crc(d + st->off + 2, st->n - (st->off + 2));
    return pc == le16(d + st->off) || pc == (uint16_t)(d[st->off] << 8 | d[st->off + 1]);
}

typedef struct {
    size_t data, len, avail;    /* encrypted: buf[data..data+len); the telegram ends at data+avail */
    uint8_t ct[WM_FRAME_MAX];
    uint8_t ivaddr[8];
    uint32_t keyid;
} tpl_try_t;

static bool tpl_try(wm_msg_t *m, const uint8_t *key, const void *stp, bool strict)
{
    const tpl_try_t *st = stp;
    uint8_t *d = m->buf;
    memcpy(d + st->data, st->ct, st->len);
    uint8_t iv[16];
    if (m->sec == 5) {
        memcpy(iv, st->ivaddr, 8);
        memset(iv + 8, m->acc, 8);
        wm_aes_cbc_decrypt(key, iv, d + st->data, st->len);
    } else {
        /* Kenc = CMAC(K, 00 | counter | id | 07 x 7) (OMS Vol. 2, KDF). */
        uint8_t in[16], kenc[16];
        in[0] = 0x00;
        for (int i = 0; i < 4; i++) in[1 + i] = (uint8_t)(m->mcr >> (8 * i));
        for (int i = 0; i < 4; i++) in[5 + i] = (uint8_t)(st->keyid >> (8 * i));
        memset(in + 9, 0x07, 7);
        wm_aes_cmac(key, in, 16, kenc);
        memset(iv, 0, 16);
        wm_aes_cbc_decrypt(kenc, iv, d + st->data, st->len);
    }
    if (st->len < 2 || d[st->data] != 0x2F || d[st->data + 1] != 0x2F) return false;
    /* only the decrypted blocks: plain bytes may follow them (UniSmart sends 2) */
    return !strict || records_hold(d + st->data, st->len);
}

/* ---- manufacturer formats ------------------------------------------------ */

/* Techem heat meters send their billing values in a frame of their own (CI A1 /
   A2) without encryption. Layout as documented by the wmbusmeters vario451 and
   compact5 drivers: after the CI 3 bytes, the previous billing period, a gap,
   the current period. Vario 4 (type 4.5.1) counts MJ in 16 bits, Compact V
   kWh in 24 bits. */
/* Techem heat cost allocators, FHKV data III (type 0x80, version 0x69 or 0x94,
   CI A0 / A2), after the wmbusmeters fhkvdataiii driver: a first byte (01 or
   11; 0F on version 0x94, which has one more byte before the temperatures),
   then 16-bit little-endian words: date and units of the previous billing
   period, date and units now, room and radiator temperature in 1/100 C. */
static void put_rec(wm_rec_t *r, const char *name, const char *value, bool primary);

static size_t fhkv3_extra(const uint8_t *d, uint8_t ver)
{
    if (d[0] == 0x0F) return 1;
    if (d[0] == 0x01 || d[0] == 0x11) return 0;
    return ver == 0x94;
}

static uint8_t mfct_kind(const wm_msg_t *m, uint8_t ci, size_t len)
{
    if (m->mfr != WM_MFR('T', 'C', 'H')) return WM_MFCT_NONE;
    const uint16_t vt = (uint16_t)(m->ver << 8 | m->type);
    /* 17/C3: the open frame of Vario 4.5.1 MID, whose 17/04 telegrams are OMS
       in mode 5; same layout. */
    if (ci == 0xA2 && (vt == 0x2704 || vt == 0x27C3 || vt == 0x17C3) && len >= 9) return WM_MFCT_TCH_VARIO451;
    if ((ci == 0xA1 || ci == 0xA2) && len >= 10 &&
        (vt == 0x4504 || vt == 0x45C3 || vt == 0x2243 || vt == 0x4543 || vt == 0x3943))
        return WM_MFCT_TCH_COMPACT5;
    if ((ci == 0xA0 || ci == 0xA2) && (vt == 0x6980 || vt == 0x9480) && len >= 13 &&
        len >= 13 + fhkv3_extra(m->buf + m->app_off, m->ver))
        return WM_MFCT_TCH_FHKV3;
    if (ci == 0xA2 && len >= 9 && (m->type == 0x62 || m->type == 0x72) &&
        (m->ver == 0x74 || m->ver == 0x95 || m->ver == 0x70))
        return WM_MFCT_TCH_RADIO;
    return WM_MFCT_NONE;
}

/* Dates of the Techem allocator and water meter frames: the end of the last
   billing period as day | month << 5 | year << 9, the current day as
   day << 4 | month << 9 without a year (the year after the period end, or the
   same year while the period has not come round again). */
static void tch_dates(const uint8_t *d, char *prev, char *curr, size_t cap)
{
    const uint16_t pd = le16(d + 1), cd = le16(d + 5);
    const int pday = pd & 31, pmon = (pd >> 5) & 15, pyear = pd >> 9;
    int cday = (cd >> 4) & 31, cmon = (cd >> 9) & 15;
    if (!cday) cday = 1;
    if (!cmon) cmon = 12;
    const int cyear = pyear + (cmon < pmon || (cmon == pmon && cday <= pday));
    snprintf(prev, cap, "%04d-%02d-%02d", 2000 + pyear, pmon, pday);
    snprintf(curr, cap, "%04d-%02d-%02d", 2000 + cyear, cmon, cday);
}

/* Techem radio 3 / radio 4 water meters (TCH 74/62, 74/72, 95/62, 95/72,
   70/62, 70/72, CI A2), after the wmbusmeters mkradio3 / mkradio4 drivers: the
   same dates as above around two 16-bit volumes in 0.1 m3, the one at the
   period end and the one since; the meter total is their sum. */
static int radio_records(const wm_msg_t *m, wm_rec_t *out, int max)
{
    const uint8_t *d = m->buf + m->app_off;
    const unsigned prev = le16(d + 3), curr = le16(d + 7), tot = prev + curr;
    char v[5][24];
    snprintf(v[0], sizeof(v[0]), "%u.%u m3", tot / 10, tot % 10);
    snprintf(v[1], sizeof(v[1]), "%u.%u m3", curr / 10, curr % 10);
    snprintf(v[3], sizeof(v[3]), "%u.%u m3", prev / 10, prev % 10);
    tch_dates(d, v[4], v[2], sizeof(v[2]));
    static const char *const NAME[5] = { "Volume", "Volume this period", "Date", "Volume last period",
                                         "Period end" };
    int k = 0;
    for (; k < 5 && k < max; k++) put_rec(&out[k], NAME[k], v[k], k == 0);
    return k;
}

static void temp_str(char *out, size_t cap, uint16_t centi)
{
    const int v = (int16_t)centi;
    const int a = v < 0 ? -v : v;
    snprintf(out, cap, "%s%d.%02d C", v < 0 ? "-" : "", a / 100, a % 100);
}

static int fhkv3_records(const wm_msg_t *m, wm_rec_t *out, int max)
{
    const uint8_t *d = m->buf + m->app_off;
    const size_t x = fhkv3_extra(d, m->ver);
    char v[6][24];
    snprintf(v[0], sizeof(v[0]), "%u HCA", (unsigned)le16(d + 7));
    snprintf(v[2], sizeof(v[2]), "%u HCA", (unsigned)le16(d + 3));
    tch_dates(d, v[3], v[1], sizeof(v[1]));
    temp_str(v[4], sizeof(v[4]), le16(d + 9 + x));
    temp_str(v[5], sizeof(v[5]), le16(d + 11 + x));
    static const char *const NAME[6] = { "HCA this period", "Date", "HCA last period", "Period end",
                                         "Room temp", "Radiator temp" };
    int k = 0;
    for (; k < 6 && k < max; k++) put_rec(&out[k], NAME[k], v[k], k == 0);
    return k;
}

static void put_rec(wm_rec_t *r, const char *name, const char *value, bool primary)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    snprintf(r->value, sizeof(r->value), "%s", value);
    r->primary = primary;
}

static int mfct_records(const wm_msg_t *m, wm_rec_t *out, int max)
{
    if (m->mfct == WM_MFCT_TCH_FHKV3) return fhkv3_records(m, out, max);
    if (m->mfct == WM_MFCT_TCH_RADIO) return radio_records(m, out, max);
    const uint8_t *d = m->buf + m->app_off;
    char v[3][24];
    if (m->mfct == WM_MFCT_TCH_VARIO451) {
        /* MJ as sent, shown in GJ like the meter's own display (1 MJ = 0.001 GJ) */
        const uint32_t prev = le16(d + 3), curr = le16(d + 7);
        const uint32_t mj[3] = { prev + curr, curr, prev };
        for (int i = 0; i < 3; i++)
            snprintf(v[i], sizeof(v[i]), "%lu.%03lu GJ", (unsigned long)(mj[i] / 1000), (unsigned long)(mj[i] % 1000));
    } else {
        const uint32_t prev = (uint32_t)d[3] | (uint32_t)d[4] << 8 | (uint32_t)d[5] << 16;   /* kWh */
        const uint32_t curr = (uint32_t)d[7] | (uint32_t)d[8] << 8 | (uint32_t)d[9] << 16;
        snprintf(v[0], sizeof(v[0]), "%lu kWh", (unsigned long)(prev + curr));
        snprintf(v[1], sizeof(v[1]), "%lu kWh", (unsigned long)curr);
        snprintf(v[2], sizeof(v[2]), "%lu kWh", (unsigned long)prev);
    }
    static const char *const NAME[3] = { "Energy", "Energy this period", "Energy last period" };
    int k = 0;
    for (; k < 3 && k < max; k++) put_rec(&out[k], NAME[k], v[k], k == 0);
    return k;
}

/* ---- link, ELL, AFL and transport layers -------------------------------- */

static int give_up(wm_msg_t *m, int dec, const char *why)
{
    m->dec = dec;
    snprintf(m->note, sizeof(m->note), "%s", why);
    return WM_OK;
}

int wm_parse(const wm_frame_t *f, wm_keyfn_t keyfn, void *ctx, wm_msg_t *m)
{
    return wm_parse_ex(f, keyfn, ctx, 0, m);
}

int wm_parse_ex(const wm_frame_t *f, wm_keyfn_t keyfn, void *ctx, unsigned flags, wm_msg_t *m)
{
    static ell_try_t ell;
    static tpl_try_t tpl;
    memset(m, 0, sizeof(*m));
    m->pubkey = -1;
    const size_t n = f->len;
    if (n < 10 || n > WM_FRAME_MAX) return WM_E_LEN;
    memcpy(m->buf, f->data, n);
    uint8_t *d = m->buf;
    m->c = d[1];
    m->mfr = le16(d + 2);
    m->id = le32(d + 4);
    m->ver = d[8];
    m->type = d[9];
    m->dec = WM_DEC_PLAIN;
    m->cut = f->cut != 0;
    m->app_off = (uint16_t)n;
    if (n == 10) return WM_OK;

    size_t p = 10;
    uint8_t ci = d[p];
    bool decrypted = false;

    /* Extended link layer */
    if (ci >= 0x8C && ci <= 0x8F) {
        static const uint8_t NEED[4] = { 3, 9, 11, 17 };
        const size_t need = NEED[ci - 0x8C];
        m->ell_ci = ci;
        if (p + need > n) return give_up(m, WM_DEC_UNSUP, "ELL truncated");
        m->ell_cc = d[p + 1];
        m->ell_acc = d[p + 2];
        if (ci == 0x8D || ci == 0x8F) {
            const size_t sp = ci == 0x8D ? p + 3 : p + 11;
            m->ell_sn = le32(d + sp);
            const unsigned enc = (m->ell_sn >> 29) & 7;
            if (enc == 1) {
                if (ci == 0x8F) return give_up(m, WM_DEC_UNSUP, "ELL 8F encryption");
                if (sp + 6 > n) return give_up(m, WM_DEC_UNSUP, "ELL truncated");
                ell.off = sp + 4;
                ell.n = n;
                memcpy(ell.ct, d + ell.off, n - ell.off);
                memset(ell.iv, 0, 16);
                memcpy(ell.iv, d + 2, 8);       /* M, A of the link layer */
                ell.iv[8] = m->ell_cc;
                memcpy(ell.iv + 9, d + sp, 4);  /* SN; FN and BC stay 0 */
                const uint8_t *key = keyfn ? keyfn(m->id, m->id, ctx) : NULL;
                const int r = with_keys(m, key, flags, WM_PK_ELL, m->mfr, ell_try, &ell);
                if (r != WM_DEC_OK) {
                    memcpy(d + ell.off, ell.ct, n - ell.off);
                    m->app_off = (uint16_t)ell.off;
                    m->app_len = (uint16_t)(n - ell.off);
                    return give_up(m, r, r == WM_DEC_NOKEY ? "ELL AES-CTR" : "ELL payload CRC");
                }
                decrypted = true;
            } else if (enc) {
                return give_up(m, WM_DEC_UNSUP, "ELL encryption mode");
            }
            p = sp + 6;
        } else {
            p += need;
        }
        if (p >= n) {
            m->dec = decrypted ? WM_DEC_OK : WM_DEC_PLAIN;
            return WM_OK;
        }
        ci = d[p];
    }

    /* Authentication and fragmentation layer: only the message counter is used. */
    if (ci == 0x90) {
        if (p + 4 > n) return give_up(m, WM_DEC_UNSUP, "AFL truncated");
        const size_t afll = d[p + 1];
        const uint16_t fcl = le16(d + p + 2);
        size_t q = p + 4;
        m->has_afl = true;
        if (fcl & 0x4000) return give_up(m, WM_DEC_UNSUP, "AFL fragmented");
        if (fcl & 0x2000) q += 1;       /* MCL */
        if (fcl & 0x0200) q += 2;       /* KI */
        if (fcl & 0x0800) {             /* MCR */
            if (q + 4 > n) return give_up(m, WM_DEC_UNSUP, "AFL truncated");
            m->mcr = le32(d + q);
            m->has_mcr = true;
        }
        p += 2 + afll;
        if (p >= n) return give_up(m, WM_DEC_UNSUP, "AFL truncated");
        ci = d[p];
    }

    /* Transport layer */
    m->ci = ci;
    uint8_t ivaddr[8];
    size_t data;
    uint32_t keyid = m->id;
    if (ci == 0x72) {
        if (p + 13 > n) return give_up(m, WM_DEC_UNSUP, "TPL truncated");
        m->has_tpl = m->long_tpl = true;
        m->tpl_id = le32(d + p + 1);
        m->tpl_mfr = le16(d + p + 5);
        m->tpl_ver = d[p + 7];
        m->tpl_type = d[p + 8];
        m->acc = d[p + 9];
        m->status = d[p + 10];
        m->cfg = le16(d + p + 11);
        memcpy(ivaddr, d + p + 5, 2);
        memcpy(ivaddr + 2, d + p + 1, 4);
        ivaddr[6] = m->tpl_ver;
        ivaddr[7] = m->tpl_type;
        keyid = m->tpl_id;
        data = p + 13;
    } else if (ci == 0x7A) {
        if (p + 5 > n) return give_up(m, WM_DEC_UNSUP, "TPL truncated");
        m->has_tpl = true;
        m->acc = d[p + 1];
        m->status = d[p + 2];
        m->cfg = le16(d + p + 3);
        memcpy(ivaddr, d + 2, 8);
        data = p + 5;
    } else if (ci == 0x78) {
        data = p + 1;
    } else {
        m->app_off = (uint16_t)(p + 1);
        m->app_len = (uint16_t)(n - p - 1);
        m->mfct = mfct_kind(m, ci, m->app_len);
        if (m->mfct) return give_up(m, WM_DEC_PLAIN, "Techem format");
        char why[24];
        snprintf(why, sizeof(why), "CI 0x%02X", ci);
        give_up(m, WM_DEC_UNSUP, why);
        return WM_OK;
    }

    if (m->has_tpl) {
        m->sec = (uint8_t)((m->cfg >> 8) & 0x1F);
        m->enc_blocks = (uint8_t)((m->cfg >> 4) & 0x0F);
        if (m->sec == 7) data += 1;     /* configuration field extension */
        if (data > n) return give_up(m, WM_DEC_UNSUP, "TPL truncated");
        if (m->sec == 5 || m->sec == 7) {
            const size_t avail = n - data;
            size_t len = (size_t)m->enc_blocks * 16;
            if (!m->enc_blocks) len = avail & ~(size_t)15;
            m->app_off = (uint16_t)data;
            m->app_len = (uint16_t)avail;
            if (len > avail && m->cut) {
                /* CBC: the blocks that came in decrypt without the rest */
                len = avail & ~(size_t)15;
                m->app_len = (uint16_t)len;
                if (!len) return give_up(m, WM_DEC_UNSUP, "cut before the data");
            }
            if (len > avail) return give_up(m, WM_DEC_BADKEY, "frame shorter than encrypted blocks");
            if (m->sec == 7 && !m->has_mcr) return give_up(m, WM_DEC_UNSUP, "mode 7 without AFL counter");
            tpl.data = data;
            tpl.len = len;
            tpl.avail = avail;
            memcpy(tpl.ct, d + data, len);
            memcpy(tpl.ivaddr, ivaddr, 8);
            tpl.keyid = keyid;
            const uint8_t *key = keyfn ? keyfn(keyid, m->id, ctx) : NULL;
            const int r = with_keys(m, key, flags, m->sec == 5 ? WM_PK_MODE5 : WM_PK_MODE7,
                                    m->long_tpl ? m->tpl_mfr : m->mfr, tpl_try, &tpl);
            if (r == WM_DEC_OK) {
                m->dec = WM_DEC_OK;
                return WM_OK;
            }
            memcpy(d + data, tpl.ct, len);      /* show what came over the air */
            if (r == WM_DEC_BADKEY) return give_up(m, r, "2F2F check failed");
            return give_up(m, r, m->sec == 5 ? "mode 5 AES-CBC" : "mode 7 AES-CBC");
        }
        if (m->sec != 0) {
            m->app_off = (uint16_t)data;
            m->app_len = (uint16_t)(n - data);
            char why[24];
            snprintf(why, sizeof(why), "security mode %u", m->sec);
            return give_up(m, WM_DEC_UNSUP, why);
        }
    }
    m->app_off = (uint16_t)data;
    m->app_len = (uint16_t)(n - data);
    m->dec = decrypted ? WM_DEC_OK : WM_DEC_PLAIN;
    return WM_OK;
}

/* ---- names -------------------------------------------------------------- */

void wm_mfr_str(uint16_t mfr, char out[4])
{
    out[0] = (char)(((mfr >> 10) & 0x1F) + 64);
    out[1] = (char)(((mfr >> 5) & 0x1F) + 64);
    out[2] = (char)((mfr & 0x1F) + 64);
    out[3] = 0;
    for (int i = 0; i < 3; i++)
        if (out[i] < 'A' || out[i] > 'Z') out[i] = '?';
}

const char *wm_type_name(uint8_t t)
{
    switch (t) {
    case 0x00: return "other";
    case 0x01: return "oil";
    case 0x02: return "electric";
    case 0x03: return "gas";
    case 0x04: return "heat";
    case 0x05: return "steam";
    case 0x06: return "warm water";
    case 0x07: return "water";
    case 0x08: return "HCA";
    case 0x09: return "compr. air";
    case 0x0A: case 0x0B: return "cooling";
    case 0x0C: return "heat";
    case 0x0D: return "heat/cool";
    case 0x0E: return "bus/system";
    case 0x0F: return "unknown";
    case 0x15: return "hot water";
    case 0x16: return "cold water";
    case 0x17: return "dual water";
    case 0x18: return "pressure";
    case 0x19: return "A/D conv.";
    case 0x1A: return "smoke det.";
    case 0x1B: return "room sensor";
    case 0x1C: return "gas det.";
    case 0x20: return "breaker";
    case 0x21: return "valve";
    case 0x25: return "display";
    case 0x28: return "waste water";
    case 0x29: return "garbage";
    case 0x31: return "OMS MUC";
    case 0x32: return "repeater";
    case 0x33: return "repeater";
    case 0x36: return "radio conv.";
    case 0x37: return "radio conv.";
    default: return NULL;
    }
}

const char *wm_type_name_mfr(uint16_t mfr, uint8_t t)
{
    if (mfr == WM_MFR('T', 'C', 'H')) {
        switch (t) {
        case 0x80: return "HCA";
        case 0x43: case 0xC3: return "heat";
        case 0x62: return "warm water";
        case 0x72: return "water";
        }
    }
    return wm_type_name(t);
}

const char *wm_c_name(uint8_t c)
{
    switch (c) {
    case 0x44: return "SND_NR";
    case 0x46: return "SND_IR";
    case 0x47: return "ACC_NR";
    case 0x48: return "ACC_DMD";
    case 0x40: return "SND_NKE";
    case 0x53: case 0x73: return "SND_UD";
    case 0x5A: case 0x7A: return "REQ_UD1";
    case 0x5B: case 0x7B: return "REQ_UD2";
    case 0x08: case 0x18: case 0x28: case 0x38: return "RSP_UD";
    case 0x00: case 0x10: case 0x20: case 0x30: return "ACK";
    default: return NULL;
    }
}

/* ---- hex ------------------------------------------------------------------ */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int wm_hex_parse(const char *s, uint8_t *out, int max)
{
    int n = 0, half = -1;
    for (; *s; s++) {
        if (*s == ' ' || *s == ':' || *s == '-' || *s == '\t') continue;
        const int v = hexval(*s);
        if (v < 0) return -1;
        if (half < 0) {
            half = v;
        } else {
            if (n >= max) return -1;
            out[n++] = (uint8_t)(half << 4 | v);
            half = -1;
        }
    }
    return half < 0 ? n : -1;
}

void wm_hex_str(const uint8_t *d, size_t n, char *out, size_t cap)
{
    static const char H[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) {
        out[o++] = H[d[i] >> 4];
        out[o++] = H[d[i] & 15];
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
}

/* ---- data records (EN 13757-3) ------------------------------------------- */

enum { VK_NUM, VK_DATE, VK_DATETIME, VK_HEX, VK_TEXT };
enum { U_NONE, U_VOLUME, U_ENERGY, U_HCA };

typedef struct {
    char name[20];
    const char *unit;
    int exp;
    uint8_t kind;
    uint8_t cat;
} vinfo_t;

static const char *const TIME_UNIT[4] = { "s", "min", "h", "d" };

static void vif_info(uint8_t table, uint8_t code, vinfo_t *v)
{
    const uint8_t c = code & 0x7F;
    v->unit = "";
    v->exp = 0;
    v->kind = VK_NUM;
    v->cat = U_NONE;
#define NAME(s) snprintf(v->name, sizeof(v->name), "%s", s)
    if (table == 0) {
        if (c <= 0x07) { NAME("Energy"); v->unit = "kWh"; v->exp = (c & 7) - 6; v->cat = U_ENERGY; }
        else if (c <= 0x0F) { NAME("Energy"); v->unit = "MJ"; v->exp = (c & 7) - 6; v->cat = U_ENERGY; }
        else if (c <= 0x17) { NAME("Volume"); v->unit = "m3"; v->exp = (c & 7) - 6; v->cat = U_VOLUME; }
        else if (c <= 0x1F) { NAME("Mass"); v->unit = "kg"; v->exp = (c & 7) - 3; }
        else if (c <= 0x23) { NAME("On time"); v->unit = TIME_UNIT[c & 3]; }
        else if (c <= 0x27) { NAME("Operating time"); v->unit = TIME_UNIT[c & 3]; }
        else if (c <= 0x2F) { NAME("Power"); v->unit = "W"; v->exp = (c & 7) - 3; }
        else if (c <= 0x37) { NAME("Power"); v->unit = "J/h"; v->exp = c & 7; }
        else if (c <= 0x3F) { NAME("Volume flow"); v->unit = "m3/h"; v->exp = (c & 7) - 6; }
        else if (c <= 0x47) { NAME("Volume flow"); v->unit = "m3/min"; v->exp = (c & 7) - 7; }
        else if (c <= 0x4F) { NAME("Volume flow"); v->unit = "m3/s"; v->exp = (c & 7) - 9; }
        else if (c <= 0x57) { NAME("Mass flow"); v->unit = "kg/h"; v->exp = (c & 7) - 3; }
        else if (c <= 0x5B) { NAME("Flow temp"); v->unit = "C"; v->exp = (c & 3) - 3; }
        else if (c <= 0x5F) { NAME("Return temp"); v->unit = "C"; v->exp = (c & 3) - 3; }
        else if (c <= 0x63) { NAME("Temp diff"); v->unit = "K"; v->exp = (c & 3) - 3; }
        else if (c <= 0x67) { NAME("External temp"); v->unit = "C"; v->exp = (c & 3) - 3; }
        else if (c <= 0x6B) { NAME("Pressure"); v->unit = "bar"; v->exp = (c & 3) - 3; }
        else if (c == 0x6C) { NAME("Date"); v->kind = VK_DATE; }
        else if (c == 0x6D) { NAME("Date/time"); v->kind = VK_DATETIME; }
        else if (c == 0x6E) { NAME("HCA units"); v->cat = U_HCA; }
        else if (c <= 0x73 && c >= 0x70) { NAME("Avg. duration"); v->unit = TIME_UNIT[c & 3]; }
        else if (c <= 0x77 && c >= 0x74) { NAME("Actual. duration"); v->unit = TIME_UNIT[c & 3]; }
        else if (c == 0x78) { NAME("Fabrication no"); }
        else if (c == 0x79) { NAME("Enhanced ID"); }
        else if (c == 0x7A) { NAME("Bus address"); }
        else if (c == 0x7F) { NAME("Mfr specific"); v->kind = VK_HEX; }
        else snprintf(v->name, sizeof(v->name), "VIF %02X", c);
    } else if (table == 0xFD) {
        switch (c) {
        case 0x08: NAME("Access number"); break;
        case 0x09: NAME("Medium"); break;
        case 0x0A: NAME("Manufacturer"); break;
        case 0x0B: NAME("Parameter set"); break;
        case 0x0C: NAME("Model/version"); break;
        case 0x0D: NAME("HW version"); break;
        case 0x0E: NAME("FW version"); break;
        case 0x0F: NAME("SW version"); break;
        case 0x10: NAME("Cust. location"); break;
        case 0x11: NAME("Customer"); break;
        case 0x16: NAME("Password"); v->kind = VK_HEX; break;
        case 0x17: NAME("Error flags"); v->kind = VK_HEX; break;
        case 0x1A: NAME("Digital output"); v->kind = VK_HEX; break;
        case 0x1B: NAME("Digital input"); v->kind = VK_HEX; break;
        case 0x3A: NAME("Count"); break;
        case 0x60: NAME("Reset counter"); break;
        case 0x61: NAME("Cumul. counter"); break;
        default:
            if (c >= 0x40 && c <= 0x4F) { NAME("Voltage"); v->unit = "V"; v->exp = (c & 15) - 9; }
            else if (c >= 0x50 && c <= 0x5F) { NAME("Current"); v->unit = "A"; v->exp = (c & 15) - 12; }
            else snprintf(v->name, sizeof(v->name), "VIF FD %02X", c);
        }
    } else {                            /* 0xFB */
        if (c <= 0x01) { NAME("Energy"); v->unit = "kWh"; v->exp = (c & 1) + 2; v->cat = U_ENERGY; }
        else if (c >= 0x08 && c <= 0x09) { NAME("Energy"); v->unit = "GJ"; v->exp = (c & 1) - 1; v->cat = U_ENERGY; }
        else if (c >= 0x10 && c <= 0x11) { NAME("Volume"); v->unit = "m3"; v->exp = (c & 1) + 2; v->cat = U_VOLUME; }
        else if (c >= 0x18 && c <= 0x19) { NAME("Mass"); v->unit = "t"; v->exp = (c & 1) + 2; }
        else if (c >= 0x1A && c <= 0x1B) { NAME("Rel. humidity"); v->unit = "%"; v->exp = (c & 1) - 1; }
        else snprintf(v->name, sizeof(v->name), "VIF FB %02X", c);
    }
#undef NAME
}

/* Signed 64-bit to decimal with the point `exp` places in (exp<0), or
   exp zeros appended. */
static void fmt_scaled(char *out, size_t cap, int64_t v, int exp, const char *unit)
{
    char digits[32];
    int nd = 0;
    const bool neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do {
        digits[nd++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u && nd < 24);
    char s[48];
    size_t o = 0;
    if (neg) s[o++] = '-';
    if (exp >= 0) {
        for (int i = nd - 1; i >= 0; i--) s[o++] = digits[i];
        if (!(nd == 1 && digits[0] == '0'))
            for (int i = 0; i < exp && o < 40; i++) s[o++] = '0';
    } else {
        const int dec = -exp;
        if (nd <= dec) {
            s[o++] = '0';
            s[o++] = '.';
            for (int i = 0; i < dec - nd; i++) s[o++] = '0';
            for (int i = nd - 1; i >= 0; i--) s[o++] = digits[i];
        } else {
            for (int i = nd - 1; i >= 0; i--) {
                s[o++] = digits[i];
                if (i == dec) s[o++] = '.';
            }
        }
    }
    s[o] = 0;
    snprintf(out, cap, unit && *unit ? "%s %s" : "%s", s, unit);
}

static bool get_bcd(const uint8_t *p, int len, int64_t *out)
{
    int64_t v = 0;
    bool neg = false;
    for (int i = len - 1; i >= 0; i--) {
        int hi = p[i] >> 4;
        const int lo = p[i] & 15;
        if (i == len - 1 && hi == 0xF) { neg = true; hi = 0; }
        if (hi > 9 || lo > 9) return false;
        v = v * 100 + hi * 10 + lo;
    }
    *out = neg ? -v : v;
    return true;
}

static int64_t get_int(const uint8_t *p, int len)
{
    uint64_t v = 0;
    for (int i = len - 1; i >= 0; i--) v = v << 8 | p[i];
    if (len < 8 && (p[len - 1] & 0x80)) v |= ~(uint64_t)0 << (8 * len);   /* sign-extend */
    return (int64_t)v;
}

static void fmt_date(char *out, size_t cap, const uint8_t *p, int len, bool with_time)
{
    if (len == 2) {
        const int day = p[0] & 0x1F, mon = p[1] & 0x0F;
        const int year = ((p[0] & 0xE0) >> 5) | ((p[1] & 0xF0) >> 1);
        snprintf(out, cap, "%04d-%02d-%02d", 2000 + year, mon, day);
    } else if (len == 4) {
        const int min = p[0] & 0x3F, hour = p[1] & 0x1F, day = p[2] & 0x1F, mon = p[3] & 0x0F;
        const int year = ((p[2] & 0xE0) >> 5) | ((p[3] & 0xF0) >> 1);
        if (with_time)
            snprintf(out, cap, "%04d-%02d-%02d %02d:%02d%s", 2000 + year, mon, day, hour, min,
                     (p[0] & 0x80) ? " inv" : "");
        else
            snprintf(out, cap, "%04d-%02d-%02d", 2000 + year, mon, day);
    } else if (len == 6) {
        const int sec = p[0] & 0x3F, min = p[1] & 0x3F, hour = p[2] & 0x1F;
        const int day = p[3] & 0x1F, mon = p[4] & 0x0F;
        const int year = ((p[3] & 0xE0) >> 5) | ((p[4] & 0xF0) >> 1);
        snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d", 2000 + year, mon, day, hour, min, sec);
    } else {
        wm_hex_str(p, (size_t)len, out, cap);
    }
}

int wm_records(const uint8_t *d, size_t n, wm_rec_t *out, int max, bool *bad)
{
    static const int8_t DLEN[16] = { 0, 1, 2, 3, 4, 4, 6, 8, 0, 1, 2, 3, 4, -1, 6, 0 };
    size_t i = 0;
    int cnt = 0;
    if (bad) *bad = false;
    while (i < n && cnt < max) {
        const uint8_t dif = d[i++];
        if (dif == 0x2F) continue;      /* idle filler */
        wm_rec_t *r = &out[cnt];
        memset(r, 0, sizeof(*r));
        if (dif == 0x0F || dif == 0x1F) {
            snprintf(r->name, sizeof(r->name), "Mfr data");
            const size_t left = n - i;
            wm_hex_str(d + i, left > 10 ? 10 : left, r->value, sizeof(r->value));
            if (left > 10) snprintf(r->value + 20, sizeof(r->value) - 20, "..");
            cnt++;
            break;
        }
        if ((dif & 0x0F) == 0x0F) {     /* other special functions */
            if (bad) *bad = true;
            break;
        }
        r->func = (uint8_t)((dif >> 4) & 3);
        r->storage = (dif >> 6) & 1;
        uint8_t e = dif;
        int sh = 1, tsh = 0, ush = 0;
        while (e & 0x80) {
            if (i >= n || sh > 29) goto truncated;
            e = d[i++];
            r->storage |= (uint32_t)(e & 0x0F) << sh;
            r->tariff |= (uint8_t)(((e >> 4) & 3) << tsh);
            r->unit |= (uint16_t)(((e >> 6) & 1) << ush);
            sh += 4;
            tsh += 2;
            ush += 1;
        }
        if (i >= n) goto truncated;
        const uint8_t vif = d[i++];
        uint8_t table = 0, code = vif;
        if (vif == 0xFD || vif == 0xFB) {
            if (i >= n) goto truncated;
            table = vif;
            code = d[i++];
        }
        vinfo_t vi;
        vif_info(table, code, &vi);
        bool vife = false, back = false;
        e = code;
        while (e & 0x80) {
            if (i >= n) goto truncated;
            e = d[i++];
            vife = true;
            if ((e & 0x7F) == 0x3C) back = true;    /* only negative contributions: backward flow */
        }
        if (back) {
            const bool export_ = vi.cat == U_ENERGY || !strcmp(vi.name, "Power");
            char nm[20];
            snprintf(nm, sizeof(nm), "%.10s %s", vi.name, export_ ? "export" : "backflow");
            snprintf(vi.name, sizeof(vi.name), "%s", nm);
        }
        char ptext[12] = "";
        if (table == 0 && (vif & 0x7F) == 0x7C) {   /* unit in plain text, reversed */
            if (i >= n) goto truncated;
            const size_t l = d[i++];
            if (i + l > n) goto truncated;
            size_t o = 0;
            for (size_t k = l; k > 0 && o + 1 < sizeof(ptext); k--) ptext[o++] = (char)d[i + k - 1];
            ptext[o] = 0;
            i += l;
            snprintf(vi.name, sizeof(vi.name), "%s", "Value");
            vi.unit = ptext;
        }
        const int df = dif & 0x0F;
        int len = DLEN[df];
        bool text = false, bcd_var = false, neg_var = false;
        if (len < 0) {                  /* variable length */
            if (i >= n) goto truncated;
            const uint8_t lvar = d[i++];
            if (lvar < 0xC0) { len = lvar; text = true; }
            else if (lvar < 0xD0) { len = lvar - 0xC0; bcd_var = true; }
            else if (lvar < 0xE0) { len = lvar - 0xD0; bcd_var = neg_var = true; }
            else if (lvar < 0xF0) { len = lvar - 0xE0; }
            else if (lvar <= 0xFA) { len = 4 * (lvar - 0xF0) + 16; }
            else { if (bad) *bad = true; break; }
        }
        if (i + (size_t)len > n) goto truncated;
        const uint8_t *p = d + i;
        i += (size_t)len;
        snprintf(r->name, sizeof(r->name), "%s%s", vi.name, vife && !back ? "*" : "");
        if (df == 0 || df == 8) {
            snprintf(r->value, sizeof(r->value), "-");
        } else if (text && len && !p[0] && !memcmp(p, p + 1, (size_t)len - 1)) {
            snprintf(r->value, sizeof(r->value), "(empty)");
        } else if (text) {
            size_t o = 0;
            for (int k = len; k > 0 && o + 1 < sizeof(r->value); k--) {
                const char ch = (char)p[k - 1];
                r->value[o++] = ch >= 32 && ch < 127 ? ch : '.';
            }
            r->value[o] = 0;
        } else if (vi.kind == VK_DATE || vi.kind == VK_DATETIME) {
            fmt_date(r->value, sizeof(r->value), p, len, vi.kind == VK_DATETIME);
        } else if (vi.kind == VK_HEX || len > 8) {
            char hex[40];
            uint8_t rev[16];
            const int hl = len > 16 ? 16 : len;
            for (int k = 0; k < hl; k++) rev[k] = p[len - 1 - k];   /* most significant first */
            wm_hex_str(rev, (size_t)hl, hex, sizeof(hex));
            snprintf(r->value, sizeof(r->value), "0x%s", hex);
        } else if (df == 5) {           /* 32-bit real */
            union { uint32_t u; float f; } fu;
            fu.u = (uint32_t)get_int(p, 4);
            float fv = fu.f;
            for (int k = 0; k < vi.exp; k++) fv *= 10.0f;
            for (int k = 0; k > vi.exp; k--) fv /= 10.0f;
            /* via double: the board exports the double helpers, not float->int64 */
            const double dv = (double)fv * 1000.0;
            const int64_t milli = (int64_t)(dv + (dv < 0 ? -0.5 : 0.5));
            fmt_scaled(r->value, sizeof(r->value), milli, -3, vi.unit);
        } else {
            int64_t v;
            const bool is_bcd = (df >= 9 && df <= 0x0E && df != 0x0D) || bcd_var;
            if (is_bcd) {
                if (!get_bcd(p, len, &v)) {
                    char hex[24];
                    uint8_t rev[8];
                    for (int k = 0; k < len; k++) rev[k] = p[len - 1 - k];
                    wm_hex_str(rev, (size_t)len, hex, sizeof(hex));
                    snprintf(r->value, sizeof(r->value), "bcd %s", hex);
                    goto stored;
                }
                if (neg_var) v = -v;
            } else {
                v = len ? get_int(p, len) : 0;
            }
            fmt_scaled(r->value, sizeof(r->value), v, vi.exp, vi.unit);
        }
    stored:
        r->primary = vi.cat != U_NONE && r->storage == 0 && r->func == 0 && r->tariff == 0 && !vife;
        cnt++;
        continue;
    truncated:
        if (bad) *bad = true;
        break;
    }
    return cnt;
}

int wm_msg_records(const wm_msg_t *m, wm_rec_t *out, int max, bool *bad)
{
    if (bad) *bad = false;
    if (m->dec != WM_DEC_PLAIN && m->dec != WM_DEC_OK) return 0;
    if (m->mfct) return mfct_records(m, out, max);
    return wm_records(m->buf + m->app_off, m->app_len, out, max, bad);
}
