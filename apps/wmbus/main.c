/* WM-BUS: a Wireless M-Bus (EN 13757-4, OMS) receiver for the LR2021.
 *
 * Listens on 868.95 MHz for T1 and C1 telegrams at once (they share the
 * 0x543D sync word), on 868.3 MHz for S1, or on 868.3 MHz for T1, through the
 * API 2.4 FSK packet service. Each telegram is decoded (3-of-6 / NRZ / Manchester, CRC blocks),
 * grouped by meter, decrypted with the meter's AES-128 key (security mode 5,
 * mode 7 and ELL AES-CTR) and its data records shown as values with units.
 *
 * Keys are typed on the board (KEY on a meter, or KEYS > ADD), kept in the
 * app's own data, and also read at start from /sdcard/wmbus/keys.txt:
 *     # meter id   key
 *     12345678     000102030405060708090A0B0C0D0E0F
 *     *            <key used for every meter without its own>
 * LOG appends every telegram to /sdcard/wmbus/telegrams.log.
 *
 * Keys: up/down pick a meter, ENTER opens it, m mode, k key for the meter,
 * y the key list, g log, o OPTIONS, BACK returns. */
/* Everything used here is in API 2.4 (FSK packets, dialogs, settings, the
   board's controls), so firmware from 2.4 on runs it. */
#define LAP_TARGET_API_24
#include "lakeshark_app.h"
#include "lakeshark_ui.h"
#include "wm_proto.h"
#include <stdio.h>
#include <string.h>

LAP_ICON_PX("....................",
            "..666666666666666...",
            "..6.............6...",
            "..6.ffff.ffff...6...",
            "..6.f..f.f..f...6...",
            "..6.ffff.ffff...6...",
            "..6.............6...",
            "..666666666666666...",
            "........66..........",
            "......aaaaaa........",
            "....................",
            "...a...a...a........",
            "....a.a.a.a.........",
            ".....a...a..........",
            "....................",
            "..cccccccccccccccc..",
            "....................",
            "....................");

/* ---- configuration ------------------------------------------------------ */

/* P_T3: T mode (3-of-6) on 868.3 MHz, used by some electricity meters
   (Sagemcom XT211). New presets go at the end: the index is saved. */
enum { P_TC = 0, P_S, P_T3, P_COUNT };
static const char *const PRESET_NAME[P_COUNT] = { "T1+C1", "S1", "T1 868.3" };
static const char *const PRESET_LONG[P_COUNT] = { "T1+C1 868.950 MHz", "S1 868.300 MHz", "T1 868.300 MHz" };

#define NTRIM 17
#define TRIM_ZERO 8
static const int TRIM_KHZ[NTRIM] = { -40, -35, -30, -25, -20, -15, -10, -5, 0, 5, 10, 15, 20, 25, 30, 35, 40 };
static const char *const TRIM_NAMES[NTRIM] = { "-40 kHz", "-35 kHz", "-30 kHz", "-25 kHz", "-20 kHz", "-15 kHz",
                                               "-10 kHz", "-5 kHz", "0", "+5 kHz", "+10 kHz", "+15 kHz",
                                               "+20 kHz", "+25 kHz", "+30 kHz", "+35 kHz", "+40 kHz" };
/* RADIO SCAN: receiver settings tried in turn on a T mode, to find the ones
   a meter's long telegrams need. Profile 0 is the standard. */
typedef struct {
    const char *name;
    uint32_t bitrate, deviation, bandwidth;
    uint16_t preamble;
} prof_t;
static const prof_t PROFS[] = {
    { "standard", 100000, 50000, 300000, 32 },
    { "pre 8", 100000, 50000, 300000, 8 },
    { "pre 16", 100000, 50000, 300000, 16 },
    { "br 99920", 99920, 50000, 300000, 32 },
    { "dev 40k", 100000, 40000, 300000, 32 },
    { "dev 45k", 100000, 45000, 300000, 32 },
    { "dev 60k", 100000, 60000, 300000, 32 },
    { "bw 333k", 100000, 50000, 333333, 32 },
    { "bw 444k", 100000, 50000, 444444, 32 },
    { "bw 533k", 100000, 50000, 533333, 32 },
    { "p8 bw444", 100000, 50000, 444444, 8 },
    { "all 3", 99920, 50000, 444444, 8 },
};
#define NPROF ((int)(sizeof(PROFS) / sizeof(PROFS[0])))
enum { SK_TRIM, SK_PROF };
#define SCAN_STEP_MS 30000u     /* a meter sending every 10 s: three telegrams a step */

#define MAX_METERS 64
#define MAX_KEYS 32
#define MAX_RECS 48
#define LOG_DIR "/sdcard/wmbus"
#define LOG_PATH LOG_DIR "/telegrams.log"
#define KEYS_TXT LOG_DIR "/keys.txt"

typedef struct {
    uint32_t id;
    uint8_t any;                /* the key for every meter without its own */
    uint8_t key[16];
} wkey_t;

typedef struct {
    uint16_t mfr;
    uint32_t id;
    uint8_t ver, type, phy, dec, sec, ci;
    int8_t pubkey;              /* opened with a public key: its index, else -1 */
    int16_t rssi;               /* tenths of a dBm, INT16_MIN unknown */
    uint32_t count, last_ms;
    char summary[28];
    wm_frame_t frame;           /* the latest telegram, CRCs removed */
    /* The latest telegram with readings. Techem allocators and heat meters
       send their billing values openly (CI A0-A2) between encrypted OMS
       telegrams of the same id: keep showing the values when an encrypted
       one comes in. good.len 0: none yet. */
    wm_frame_t good;
    uint32_t good_ms;
    bool shows_good;            /* the latest has no readings: list and page show good */
    uint8_t gdec;               /* good's state and public key, when shown */
    int8_t gpub;
} meter_t;

enum { V_LIST, V_DETAIL, V_KEYS, V_SCAN };
enum { D_NONE, D_KEY_FOR_METER, D_NEW_ID, D_NEW_KEY };

static struct {
    /* radio */
    int radio;                  /* index in lap_radio_list, -1 none */
    char radio_name[16];
    int h;                      /* packet session, 0 closed */
    int err;                    /* last radio error */
    int preset;
    uint32_t retry_ms;
    /* counters */
    uint32_t syncs, frames, e_coding, e_crc, e_other, gaps;
    uint32_t tails;             /* frames found after another in the same packet */
    /* meters */
    meter_t m[MAX_METERS];
    int nm, sel, top;
    int view;
    /* FREQ TRIM: kHz added to the mode's frequency, for radio modules and
       meters a few kHz off. TRIM SCAN steps through TRIM_KHZ. */
    int trim;                   /* index in TRIM_KHZ */
    bool scan_on;
    int scan_kind;              /* SK_TRIM or SK_PROF */
    int scan_i, scan_back;      /* step, the trim to return to when stopped */
    int prof;                   /* receiver profile (PROFS), 0 the standard one; not saved */
    bool sc_open_err[17];       /* the receiver refused that step's settings */
    uint32_t scan_ms;
    uint32_t sc_frames[17], sc_bytes[17], sc_fixed[17], sc_max[17];
    /* detail */
    int det;                    /* meter index shown */
    uint32_t det_count;         /* its count when last decoded */
    wm_msg_t msg;
    wm_rec_t rec[MAX_RECS];
    int nrec;
    bool recbad, hex;
    int dscroll;
    /* keys */
    wkey_t keys[MAX_KEYS];
    int nk, ksel;
    /* dialogs */
    int dlg, dlg_kind;
    uint32_t pend_id;
    bool pend_any;
    /* keys.txt import */
    int impjob;
    bool imp_user;              /* asked for in OPTIONS: say when it is missing */
    char imp[4096];
    /* log */
    bool log_on, beep;
    bool lograw;                /* OPTIONS: every radio packet in the log too */
    bool nopub;                 /* OPTIONS: do not try the public keys */
    char logbuf[6144];
    int loglen, logjob, mkjob;
    uint32_t log_ms, logged, logdrop;
    /* status line */
    char status[72];
    uint32_t status_ms;
    lap_btn_t btn[6];
    char btn_val[3][12];
    wm_frame_t rxf;
    uint8_t raw[LAP_PACKET_MAX + 1];
} s;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s.status, sizeof(s.status), fmt, ap);
    va_end(ap);
    s.status_ms = lap_millis();
}

/* ---- keys ------------------------------------------------------------------ */

/* The meter's own key, else one typed for the id shown in the list (the link
   layer's, which differs for radio converters), else the default key. */
static const uint8_t *key_for(uint32_t id, uint32_t link_id, void *ctx)
{
    (void)ctx;
    const uint8_t *any = NULL, *link = NULL;
    for (int i = 0; i < s.nk; i++) {
        if (s.keys[i].any) any = s.keys[i].key;
        else if (s.keys[i].id == id) return s.keys[i].key;
        else if (s.keys[i].id == link_id) link = s.keys[i].key;
    }
    return link ? link : any;
}

static void keys_save(void)
{
    static uint8_t blob[4 + MAX_KEYS * sizeof(wkey_t)];
    blob[0] = 'W'; blob[1] = 'K'; blob[2] = 1; blob[3] = (uint8_t)s.nk;
    memcpy(blob + 4, s.keys, (size_t)s.nk * sizeof(wkey_t));
    if (lap_save("keys", blob, 4 + (size_t)s.nk * sizeof(wkey_t)) < 0) say("could not save the keys");
}

static void keys_load(void)
{
    static uint8_t blob[4 + MAX_KEYS * sizeof(wkey_t)];
    s.nk = 0;
    const int n = lap_load("keys", blob, sizeof(blob));
    if (n < 4 || blob[0] != 'W' || blob[1] != 'K' || blob[2] != 1) return;
    int k = blob[3];
    if (k > MAX_KEYS || 4 + (size_t)k * sizeof(wkey_t) > (size_t)n) return;
    memcpy(s.keys, blob + 4, (size_t)k * sizeof(wkey_t));
    s.nk = k;
}

/* Adds or replaces; false when the list is full. */
static bool key_put(uint32_t id, bool any, const uint8_t key[16])
{
    for (int i = 0; i < s.nk; i++) {
        if ((any && s.keys[i].any) || (!any && !s.keys[i].any && s.keys[i].id == id)) {
            memcpy(s.keys[i].key, key, 16);
            return true;
        }
    }
    if (s.nk >= MAX_KEYS) return false;
    s.keys[s.nk].id = any ? 0 : id;
    s.keys[s.nk].any = any;
    memcpy(s.keys[s.nk].key, key, 16);
    s.nk++;
    return true;
}

/* "12345678" or "*" -> id; false when it is neither. */
static bool parse_id(const char *t, uint32_t *id, bool *any)
{
    while (*t == ' ') t++;
    if (t[0] == '*' || !strncmp(t, "all", 3) || !strncmp(t, "ALL", 3)) {
        *any = true;
        *id = 0;
        return true;
    }
    uint8_t b[4];
    if (wm_hex_parse(t, b, 4) != 4) return false;
    *any = false;
    *id = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    return true;
}

static void reanalyze_all(void);

/* keys.txt: "<id or *> <32 hex digits>" a line, '#' starts a comment. */
static int import_keys(const char *text, int len)
{
    int added = 0, i = 0;
    static char line[128];
    while (i < len) {
        int o = 0;
        while (i < len && text[i] != '\n') {
            if (o < (int)sizeof(line) - 1) line[o++] = text[i];
            i++;
        }
        i++;
        line[o] = 0;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;
        char *sp = p;
        while (*sp && *sp != ' ' && *sp != '\t' && *sp != ';' && *sp != ',' && *sp != '=') sp++;
        if (!*sp) continue;
        *sp++ = 0;
        uint32_t id;
        bool any;
        uint8_t key[16];
        char *r = sp;
        for (char *q = sp; *q; q++)
            if (*q == '\r' || *q == ';' || *q == ',' || *q == '=') *q = ' ';
        if (!parse_id(p, &id, &any) || wm_hex_parse(r, key, 16) != 16) continue;
        if (key_put(id, any, key)) added++;
    }
    return added;
}

/* ---- meters ------------------------------------------------------------------ */

static int parse(const wm_frame_t *f, wm_msg_t *m)
{
    return wm_parse_ex(f, key_for, NULL, s.nopub ? 0 : WM_TRY_PUBLIC, m);
}

/* The telegram the list and the meter's page show. */
static const wm_frame_t *shown(const meter_t *mt)
{
    return mt->shows_good ? &mt->good : &mt->frame;
}

/* STATE column and log: "AES pub" when a public key opened it. */
static const char *state_name(int dec, int pubkey)
{
    return dec == WM_DEC_OK && pubkey >= 0 ? "AES pub" : wm_dec_name(dec);
}

/* Parses one telegram into msg and its one-line summary; returns the number of
   readings, -1 when it does not parse. */
static int summarize(const wm_frame_t *f, wm_msg_t *msg, char *sum, size_t cap)
{
    static wm_rec_t rec[MAX_RECS];
    sum[0] = 0;
    if (parse(f, msg) != WM_OK) {
        snprintf(sum, cap, "parse error");
        return -1;
    }
    if (msg->dec == WM_DEC_NOKEY) { snprintf(sum, cap, "encrypted"); return 0; }
    if (msg->dec == WM_DEC_BADKEY) { snprintf(sum, cap, "wrong key?"); return 0; }
    if (msg->dec == WM_DEC_UNSUP) { snprintf(sum, cap, "%s", msg->note); return 0; }
    bool bad;
    const int n = wm_msg_records(msg, rec, MAX_RECS, &bad);
    for (int i = 0; i < n; i++)
        if (rec[i].primary) { snprintf(sum, cap, "%s", rec[i].value); return n; }
    if (n) snprintf(sum, cap, "%.10s %s", rec[0].name, rec[0].value);
    return n;
}

static void analyze(meter_t *mt)
{
    static wm_msg_t msg;
    mt->shows_good = false;
    const int n = summarize(&mt->frame, &msg, mt->summary, sizeof(mt->summary));
    mt->dec = n < 0 ? WM_DEC_UNSUP : (uint8_t)msg.dec;
    mt->pubkey = n < 0 ? -1 : msg.pubkey;
    mt->sec = msg.sec;
    mt->ci = msg.ci;
    if (n > 0) {
        mt->good = mt->frame;
        mt->good_ms = mt->last_ms;
        return;
    }
    if (!mt->good.len || mt->dec == WM_DEC_PLAIN || mt->dec == WM_DEC_OK) return;
    static char sum[28];
    if (summarize(&mt->good, &msg, sum, sizeof(sum)) <= 0) return;
    mt->shows_good = true;
    mt->gdec = (uint8_t)msg.dec;
    mt->gpub = msg.pubkey;
    snprintf(mt->summary, sizeof(mt->summary), "%s", sum);
}

static void detail_refresh(void)
{
    if (s.det < 0 || s.det >= s.nm) return;
    meter_t *mt = &s.m[s.det];
    s.det_count = mt->count;
    s.nrec = 0;
    s.recbad = false;
    if (parse(shown(mt), &s.msg) != WM_OK) return;
    s.nrec = wm_msg_records(&s.msg, s.rec, MAX_RECS, &s.recbad);
}

static void reanalyze_all(void)
{
    for (int i = 0; i < s.nm; i++) analyze(&s.m[i]);
    if (s.view == V_DETAIL) detail_refresh();
}

static void log_telegram(const meter_t *mt)
{
    if (!s.log_on) return;
    static char line[600];
    lap_time_t t;
    lap_time(&t);
    char mfr[4];
    wm_mfr_str(mt->mfr, mfr);
    int o;
    if (t.synced)
        o = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d;", t.year, t.month, t.day,
                     t.hour, t.minute, t.second);
    else
        o = snprintf(line, sizeof(line), "+%lu;", (unsigned long)lap_millis());
    o += snprintf(line + o, sizeof(line) - (size_t)o, "%s;%d;%08lX;%s;%02X;%s;", wm_phy_name(mt->phy),
                  mt->rssi == INT16_MIN ? 0 : mt->rssi / 10, (unsigned long)mt->id, mfr, mt->type,
                  state_name(mt->dec, mt->pubkey));
    wm_hex_str(mt->frame.data, mt->frame.len, line + o, sizeof(line) - (size_t)o - 2);
    o += (int)strlen(line + o);
    line[o++] = '\n';
    line[o] = 0;
    if (s.loglen + o > (int)sizeof(s.logbuf)) { s.logdrop++; return; }
    memcpy(s.logbuf + s.loglen, line, (size_t)o);
    s.loglen += o;
    s.logged++;
}

static void log_pump(void)
{
    if (s.mkjob > 0 && lap_sd_poll(s.mkjob) != LAP_SD_PENDING) s.mkjob = 0;
    if (s.logjob > 0) {
        const int r = lap_sd_poll(s.logjob);
        if (r == LAP_SD_PENDING) return;
        if (r < 0) say("log write failed (%d): is the SD card in?", r);
        s.logjob = 0;
    }
    if (!s.loglen) return;
    if (s.loglen < 2048 && lap_millis() - s.log_ms < 2000) return;
    const int j = lap_sd_write(LOG_PATH, LAP_SD_APPEND, s.logbuf, (uint32_t)s.loglen);
    if (j >= 0) {
        s.logjob = j;
        s.loglen = 0;
        s.log_ms = lap_millis();
    }
}

static void log_set(bool on)
{
    s.log_on = on;
    if (on) {
        s.mkjob = lap_sd_mkdir(LOG_DIR);
        say("logging to " LOG_PATH);
    } else {
        say("log off, %lu telegrams written", (unsigned long)s.logged);
    }
}

static meter_t *meter_slot(uint16_t mfr, uint32_t id)
{
    for (int i = 0; i < s.nm; i++)
        if (s.m[i].mfr == mfr && s.m[i].id == id) return &s.m[i];
    if (s.nm < MAX_METERS) {
        meter_t *mt = &s.m[s.nm++];
        memset(mt, 0, sizeof(*mt));
        return mt;
    }
    int old = 0;                        /* full: the one heard longest ago */
    for (int i = 1; i < s.nm; i++)
        if (s.m[i].last_ms < s.m[old].last_ms) old = i;
    if (old == s.det && s.view == V_DETAIL) s.view = V_LIST;
    memset(&s.m[old], 0, sizeof(s.m[old]));
    return &s.m[old];
}

/* s.rxf holds a decoded frame: file it under its meter. */
static void scan_count(void);

static void on_frame(const lap_packet_meta_t *meta)
{
    s.frames++;
    scan_count();
    const uint8_t *d = s.rxf.data;
    const uint16_t mfr = (uint16_t)(d[2] | d[3] << 8);
    const uint32_t id = (uint32_t)d[4] | (uint32_t)d[5] << 8 | (uint32_t)d[6] << 16 | (uint32_t)d[7] << 24;
    meter_t *mt = meter_slot(mfr, id);
    const bool fresh = mt->count == 0;
    mt->mfr = mfr;
    mt->id = id;
    mt->ver = d[8];
    mt->type = d[9];
    mt->phy = s.rxf.phy;
    mt->rssi = meta->rssi_tenths_dbm;
    mt->count++;
    mt->last_ms = lap_millis();
    mt->frame = s.rxf;
    analyze(mt);
    log_telegram(mt);
    if (fresh && s.beep) lap_sound(LAP_SOUND_CHIRP);
    if (s.view == V_DETAIL && mt == &s.m[s.det]) detail_refresh();
}

/* RAW IN LOG: "time;RAW;dBm;result;255 bytes hex", whatever came of the packet,
   to see what follows a frame inside it. */
static void log_raw(const uint8_t *raw, int n, int e, const lap_packet_meta_t *meta)
{
    if (!s.log_on || !s.lograw) return;
    static char line[600];
    int o = snprintf(line, sizeof(line), "+%lu;RAW;%d;%d;", (unsigned long)lap_millis(),
                     meta->rssi_tenths_dbm == INT16_MIN ? 0 : meta->rssi_tenths_dbm / 10, e);
    wm_hex_str(raw, (size_t)n, line + o, sizeof(line) - (size_t)o - 2);
    o += (int)strlen(line + o);
    line[o++] = '\n';
    if (s.loglen + o > (int)sizeof(s.logbuf)) { s.logdrop++; return; }
    memcpy(s.logbuf + s.loglen, line, (size_t)o);
    s.loglen += o;
}

static void on_packet(const uint8_t *raw, int n, const lap_packet_meta_t *meta)
{
    s.syncs++;
    if (meta->flags & LAP_DATA_GAP) s.gaps++;
    const size_t len = (size_t)n;
    const int e = s.preset == P_S ? wm_phy_decode_s(raw, len, &s.rxf) : wm_phy_decode_tc(raw, len, &s.rxf);
    log_raw(raw, n, e, meta);
    if (e == WM_E_CODING || e == WM_E_SYNC) s.e_coding++;
    else if (e == WM_E_CRC) s.e_crc++;
    else if (e != WM_OK) s.e_other++;
    if (s.preset == P_S) {
        if (e == WM_OK) on_frame(meta);
        return;
    }
    size_t from = 1;
    if (e == WM_OK) {
        from = wm_phy_used_tc(&s.rxf, len);
        on_frame(meta);
    }
    /* The radio hands over 255 bytes after a sync word: a telegram sent right
       after a short one can be inside the same packet. */
    static uint8_t tail[LAP_PACKET_MAX];
    for (int k = 0; k < 4 && from + 8 < len; k++) {
        size_t at = 0;
        const size_t got = wm_find_sync_tc(raw, len, from, tail, sizeof(tail), &at);
        if (!got) break;
        if (wm_phy_decode_tc(tail, got, &s.rxf) != WM_OK) {
            from = at + 1;
            continue;
        }
        s.tails++;
        from = at + wm_phy_used_tc(&s.rxf, got);
        on_frame(meta);
    }
}

/* ---- the radio ------------------------------------------------------------------ */

static void radio_close(void)
{
    if (s.h > 0) lap_packet_close(s.h);
    s.h = 0;
}

static const char *err_text(int e)
{
    switch (e) {
    case LAP_RX_EACCES: return "radio access refused: reopen and allow RADIO";
    case LAP_RX_EBUSY: return "radio busy (MESH or another app holds it)";
    case LAP_RX_ENODEV: return "no LR2021 fitted";
    case LAP_RX_EUNSUPPORTED: return "FSK packets not supported here";
    case LAP_RX_ERANGE: return "frequency out of range";
    case LAP_RX_EINVAL: return "receiver refused the settings";
    default: return "radio error";
    }
}

static void find_radio(void)
{
    s.radio = -1;
    lap_caps_t caps;
    if (lap_caps(&caps, sizeof(caps)) < 0 || !(caps.hardware & LAP_HW_PACKET)) return;
    static lap_radio_info_t list[LAP_RADIOS_MAX];
    const int nr = lap_radio_list(list, LAP_RADIOS_MAX);
    for (int i = 0; i < caps.radio_count; i++) {
        lap_radio_modes_t md;
        if (lap_radio_modes(i, &md) || !(md.modes & LAP_MODE_FSK)) continue;
        s.radio = i;
        snprintf(s.radio_name, sizeof(s.radio_name), "%s", i < nr ? list[i].name : "radio");
        return;
    }
}

static void radio_open(void)
{
    radio_close();
    if (s.radio < 0) find_radio();
    if (s.radio < 0) { s.err = LAP_RX_ENODEV; return; }
    lap_packet_config_t c;
    memset(&c, 0, sizeof(c));
    c.size = sizeof(c);
    c.version = 1;
    c.modulation = LAP_MOD_FSK;
    c.payload_bytes = LAP_PACKET_MAX;
    c.sync_bits = 16;
    c.preamble = 32;
    c.crc = 0;
    if (s.preset == P_S) {
        c.frequency_hz = 868300000;
        c.bitrate = 32768;
        c.deviation_hz = 50000;
        c.bandwidth_hz = 156000;
        c.sync_word = 0x76960000u;
    } else {
        const prof_t *pf = &PROFS[s.prof];
        c.frequency_hz = 868950000;
        c.bitrate = pf->bitrate;
        c.deviation_hz = pf->deviation;
        c.bandwidth_hz = pf->bandwidth;
        c.preamble = pf->preamble;
        c.sync_word = 0x543D0000u;
        if (s.preset == P_T3) {
            c.frequency_hz = 868300000;
            /* The LR2021 follows the sender's clock only loosely. These
               meters send 99 920 chip/s, and at 100 000 their long telegrams
               fall apart after the first block. */
            if (s.prof == 0) c.bitrate = 99920;
        }
    }
    c.frequency_hz = (uint64_t)((int64_t)c.frequency_hz + (int64_t)TRIM_KHZ[s.trim] * 1000);
    const int h = lap_packet_open(s.radio, &c);
    if (h > 0) {
        s.h = h;
        s.err = 0;
    } else {
        s.err = h;
        if (s.scan_on) s.sc_open_err[s.scan_i] = true;
    }
}

static void radio_pump(void)
{
    if (s.h <= 0) {
        if (lap_millis() - s.retry_ms > 3000) {
            s.retry_ms = lap_millis();
            s.radio = -1;
            radio_open();
        }
        return;
    }
    lap_io_status_t st;
    if (lap_packet_status(s.h, &st) == 0 && (st.error || st.state == LAP_IO_FAILED)) {
        s.err = st.error ? st.error : LAP_RX_EIO;
        radio_close();
        s.retry_ms = lap_millis();
        return;
    }
    for (int k = 0; k < 8; k++) {
        lap_packet_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        meta.size = sizeof(meta);
        meta.version = 1;
        const int n = lap_packet_read(s.h, s.raw, sizeof(s.raw), &meta);
        if (n <= 0) break;
        on_packet(s.raw, n, &meta);
    }
}

static void set_trim(int i)
{
    s.trim = i < 0 ? 0 : i >= NTRIM ? NTRIM - 1 : i;
    lap_settings_set("trim", &s.trim, sizeof(s.trim));
    radio_open();
}

static int scan_n(void) { return s.scan_kind == SK_PROF ? NPROF : NTRIM; }
static const char *scan_label(int i) { return s.scan_kind == SK_PROF ? PROFS[i].name : TRIM_NAMES[i]; }

/* Puts step i of the scan on the air. */
static void scan_apply(int i)
{
    if (s.scan_kind == SK_PROF) s.prof = i;
    else s.trim = i;
    radio_open();
}

/* A decoded frame during a scan: how much of it came through. */
static void scan_count(void)
{
    if (!s.scan_on) return;
    const int i = s.scan_i;
    s.sc_frames[i]++;
    s.sc_bytes[i] += s.rxf.len;
    s.sc_fixed[i] += s.rxf.fixed;
    if (s.rxf.len > s.sc_max[i]) s.sc_max[i] = s.rxf.len;
}

static void scan_log(int i)
{
    if (!s.log_on) return;
    char line[96];
    const int o = snprintf(line, sizeof(line), "+%lu;SCAN;%s;%s;%lu;%lu;%lu;%lu%s\n", (unsigned long)lap_millis(),
                           PRESET_NAME[s.preset], scan_label(i), (unsigned long)s.sc_frames[i],
                           (unsigned long)s.sc_bytes[i], (unsigned long)s.sc_max[i], (unsigned long)s.sc_fixed[i],
                           s.sc_open_err[i] ? ";refused" : "");
    if (s.loglen + o <= (int)sizeof(s.logbuf)) {
        memcpy(s.logbuf + s.loglen, line, (size_t)o);
        s.loglen += o;
    }
}

static void scan_start(int kind)
{
    if (kind == SK_PROF && s.preset == P_S) {
        say("RADIO SCAN is for the T modes");
        return;
    }
    s.scan_kind = kind;
    memset(s.sc_open_err, 0, sizeof(s.sc_open_err));
    memset(s.sc_frames, 0, sizeof(s.sc_frames));
    memset(s.sc_bytes, 0, sizeof(s.sc_bytes));
    memset(s.sc_fixed, 0, sizeof(s.sc_fixed));
    memset(s.sc_max, 0, sizeof(s.sc_max));
    s.scan_back = s.trim;
    s.scan_on = true;
    s.scan_i = 0;
    s.scan_ms = lap_millis();
    scan_apply(0);
    s.view = V_SCAN;
}

/* The step that let the most bytes through; on a tie the trim nearer 0, or
   the earlier profile. */
static int scan_best(void)
{
    int best = -1;
    const int z = s.scan_kind == SK_PROF ? 0 : TRIM_ZERO;
    for (int i = 0; i < scan_n(); i++) {
        if (!s.sc_bytes[i]) continue;
        if (best < 0 || s.sc_bytes[i] > s.sc_bytes[best] ||
            (s.sc_bytes[i] == s.sc_bytes[best] && (i - z) * (i - z) < (best - z) * (best - z)))
            best = i;
    }
    return best;
}

static void scan_stop(bool finished)
{
    s.scan_on = false;
    const int best = finished ? scan_best() : -1;
    if (s.scan_kind == SK_PROF) {    /* back to the standard receiver */
        s.prof = 0;
        radio_open();
        if (best >= 0) say("radio scan done: most data with \"%s\"", PROFS[best].name);
        else say(finished ? "radio scan done: nothing heard" : "scan stopped");
        return;
    }
    if (best >= 0) {
        set_trim(best);
        say("scan done: trim %s kept", TRIM_NAMES[best]);
    } else {
        set_trim(s.scan_back);
        say(finished ? "scan done: nothing heard, trim unchanged" : "scan stopped");
    }
}

static void scan_pump(void)
{
    if (!s.scan_on || lap_millis() - s.scan_ms < SCAN_STEP_MS) return;
    scan_log(s.scan_i);
    if (s.scan_i + 1 >= scan_n()) {
        scan_stop(true);
        return;
    }
    s.scan_i++;
    s.scan_ms = lap_millis();
    scan_apply(s.scan_i);
}

static void set_preset(int p)
{
    s.preset = (p % P_COUNT + P_COUNT) % P_COUNT;
    s.syncs = s.frames = s.e_coding = s.e_crc = s.e_other = s.gaps = s.tails = 0;
    radio_open();
    say("listening: %s", PRESET_LONG[s.preset]);
    lap_settings_set("preset", &s.preset, sizeof(s.preset));
}

/* ---- dialogs --------------------------------------------------------------- */

static void ask_key(uint32_t id, bool any)
{
    static char prompt[96];
    s.pend_id = id;
    s.pend_any = any;
    if (any) snprintf(prompt, sizeof(prompt), "AES-128 key for ALL meters (32 hex digits)");
    else snprintf(prompt, sizeof(prompt), "AES-128 key for meter %08lX (32 hex digits)", (unsigned long)id);
    const uint8_t *cur = NULL;
    for (int i = 0; i < s.nk; i++)
        if ((any && s.keys[i].any) || (!any && !s.keys[i].any && s.keys[i].id == id)) cur = s.keys[i].key;
    static char init[40];
    init[0] = 0;
    if (cur) wm_hex_str(cur, 16, init, sizeof(init));
    s.dlg = lap_text_input(prompt, init, 47, 0);
    s.dlg_kind = s.dlg >= 0 ? D_NEW_KEY : D_NONE;
    if (s.dlg < 0) say("cannot open the keyboard (%d)", s.dlg);
}

static void ask_id(void)
{
    static char init[12];
    init[0] = 0;
    if (s.sel >= 0 && s.sel < s.nm) snprintf(init, sizeof(init), "%08lX", (unsigned long)s.m[s.sel].id);
    s.dlg = lap_text_input("Meter ID: 8 hex digits as shown, or * for all meters", init, 8, 0);
    s.dlg_kind = s.dlg >= 0 ? D_NEW_ID : D_NONE;
}

static void dialog_pump(void)
{
    if (s.dlg_kind == D_NONE) return;
    static char text[80];
    int value = 0;
    const int r = lap_dialog_poll(s.dlg, text, sizeof(text), &value);
    if (r == 0) return;
    const int kind = s.dlg_kind;
    s.dlg_kind = D_NONE;
    if (r != 1) return;                         /* cancelled or error */
    if (kind == D_NEW_ID) {
        uint32_t id;
        bool any;
        if (!parse_id(text, &id, &any)) { say("not a meter id: %.20s", text); return; }
        ask_key(id, any);
    } else if (kind == D_NEW_KEY) {
        uint8_t key[16];
        if (!text[0]) return;
        if (wm_hex_parse(text, key, 16) != 16) { say("a key is 32 hex digits (16 bytes)"); return; }
        if (!key_put(s.pend_id, s.pend_any, key)) { say("key list full (%d)", MAX_KEYS); return; }
        keys_save();
        reanalyze_all();
        if (s.pend_any) say("key for all meters saved");
        else say("key for %08lX saved", (unsigned long)s.pend_id);
    }
}

/* ---- OPTIONS ------------------------------------------------------------------ */

enum { O_MODE, O_LOG, O_BEEP, O_PUBLIC, O_KEYS, O_IMPORT, O_CLEAR, O_RAW, O_TRIM, O_SCAN, O_PSCAN };
static const char *const MODE_NAMES[P_COUNT] = { "T1+C1 868.95", "S1 868.30", "T1 868.30" };

static void import_start(bool user)
{
    s.imp_user = user;
    s.impjob = lap_sd_read(KEYS_TXT, 0, s.imp, sizeof(s.imp) - 1);
}

static void clear_meters(void)
{
    s.nm = s.sel = s.top = 0;
    s.view = V_LIST;
    s.syncs = s.frames = s.e_coding = s.e_crc = s.e_other = s.gaps = s.tails = 0;
}

static double opt_get(int id)
{
    switch (id) {
    case O_MODE: return s.preset;
    case O_LOG: return s.log_on;
    case O_BEEP: return s.beep;
    case O_RAW: return s.lograw;
    case O_TRIM: return TRIM_KHZ[s.trim];
    case O_PUBLIC: return !s.nopub;
    default: return 0;
    }
}

static void opt_set(int id, double v)
{
    switch (id) {
    case O_MODE: set_preset((int)v); break;
    case O_LOG: log_set(v > 0.5); break;
    case O_BEEP: s.beep = v > 0.5; break;
    case O_RAW: s.lograw = v > 0.5; break;
    case O_TRIM: {
        /* kHz from the LEVEL row -> the nearest step of TRIM_KHZ (CYCLE rows take 8 names at most) */
        const int k = (int)(v < 0 ? v - 2.5 : v + 2.5) / 5;
        if (s.scan_on) scan_stop(false);
        set_trim(k + TRIM_ZERO);
        break;
    }
    case O_SCAN: scan_start(SK_TRIM); break;
    case O_PSCAN: scan_start(SK_PROF); break;
    case O_PUBLIC: {
        const uint8_t off = v < 0.5;
        s.nopub = off;
        lap_settings_set("nopub", &off, sizeof(off));
        reanalyze_all();
        break;
    }
    case O_KEYS: s.view = V_KEYS; break;
    case O_IMPORT: import_start(true); break;
    case O_CLEAR: clear_meters(); break;
    }
}

static const lap_opt_t OPT_ROWS[] = {
    { .label = "MODE", .kind = LAP_OPT_CYCLE, .id = O_MODE, .names = MODE_NAMES, .n = P_COUNT },
    { .label = "LOG TO SD", .kind = LAP_OPT_TOGGLE, .id = O_LOG },
    { .label = "FREQ TRIM", .kind = LAP_OPT_LEVEL, .id = O_TRIM, .lo = -40, .hi = 40, .step = 5, .unit = "kHz" },
    { .label = "TRIM SCAN", .kind = LAP_OPT_ACTION, .id = O_SCAN },
    { .label = "RADIO SCAN", .kind = LAP_OPT_ACTION, .id = O_PSCAN },
    { .label = "RAW IN LOG", .kind = LAP_OPT_TOGGLE, .id = O_RAW },
    { .label = "BEEP NEW METER", .kind = LAP_OPT_TOGGLE, .id = O_BEEP },
    { .label = "PUBLIC KEYS", .kind = LAP_OPT_TOGGLE, .id = O_PUBLIC },
    { .label = "KEYS", .kind = LAP_OPT_ACTION, .id = O_KEYS },
    { .label = "READ KEYS.TXT", .kind = LAP_OPT_ACTION, .id = O_IMPORT },
    { .label = "CLEAR METERS", .kind = LAP_OPT_ACTION, .id = O_CLEAR },
};
static const lap_opt_list_t OPTIONS = { "WM-BUS", OPT_ROWS, 11, opt_get, opt_set };

/* ---- drawing ------------------------------------------------------------------- */

static void age_str(uint32_t ms, char *out, size_t cap)
{
    const uint32_t sec = (lap_millis() - ms) / 1000;
    if (sec < 100) snprintf(out, cap, "%lus", (unsigned long)sec);
    else if (sec < 6000) snprintf(out, cap, "%lum", (unsigned long)(sec / 60));
    else snprintf(out, cap, "%luh", (unsigned long)(sec / 3600));
}

static uint8_t dec_color(int dec)
{
    switch (dec) {
    case WM_DEC_PLAIN: return LAP_UI_DIM;
    case WM_DEC_OK: return LAP_ATTR(LAP_UI_LIVE, LAP_BLACK);
    case WM_DEC_NOKEY: return LAP_ATTR(LAP_UI_WARN, LAP_BLACK);
    case WM_DEC_BADKEY: return LAP_ATTR(LAP_UI_ALARM, LAP_BLACK);
    default: return LAP_UI_FAINT;
    }
}

static int draw_header(int w)
{
    char line[96];
    lap_put_str(0, 0, "WM-BUS", LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK));
    if (s.trim != TRIM_ZERO)
        snprintf(line, sizeof(line), "%s %+dk  %s", PRESET_LONG[s.preset], TRIM_KHZ[s.trim],
                 s.radio_name[0] ? s.radio_name : "");
    else
        snprintf(line, sizeof(line), "%s  %s", PRESET_LONG[s.preset], s.radio_name[0] ? s.radio_name : "");
    lap_put_str(7, 0, line, LAP_ATTR(LAP_UI_VALUE, LAP_BLACK));
    const bool live = s.h > 0;
    const char *st = live ? "RX" : "OFF";
    lap_put_str(w - (int)strlen(st) - 1, 0, st,
                LAP_ATTR(live ? LAP_UI_LIVE : LAP_UI_ALARM, LAP_BLACK));
    if (live)
        snprintf(line, sizeof(line), "ok %lu  sync %lu  crc %lu  noise %lu  meters %d  keys %d%s",
                 (unsigned long)s.frames, (unsigned long)s.syncs, (unsigned long)s.e_crc,
                 (unsigned long)(s.e_coding + s.e_other), s.nm, s.nk, s.log_on ? "  LOG" : "");
    else
        snprintf(line, sizeof(line), "%s", err_text(s.err));
    lap_put_str(0, 1, line, live ? LAP_UI_DIM : LAP_ATTR(LAP_UI_WARN, LAP_BLACK));
    lap_hline(0, 2, w, '-', LAP_UI_FAINT);
    return 3;
}

static void draw_list(int y0, int ky, int w)
{
    const bool wide = w >= 72;          /* portrait boards are about 52 columns */
    lap_put_str(0, y0, wide ? "ID       MFR TYPE        dBm    N  AGE STATE    VALUE"
                            : "ID       MFR  dBm    N STATE    VALUE", LAP_UI_DIM);
    const int rows = ky - y0 - 2;
    if (s.sel >= s.nm) s.sel = s.nm - 1;
    if (s.sel < 0) s.sel = 0;
    if (s.sel < s.top) s.top = s.sel;
    if (rows > 0 && s.sel >= s.top + rows) s.top = s.sel - rows + 1;
    if (!s.nm) {
        lap_put_str(1, y0 + 2, "Listening... no meters heard yet.", LAP_UI_TEXT);
        lap_put_str(1, y0 + 4, "Most meters send every 8 s to 15 min.", LAP_UI_DIM);
        lap_put_str(1, y0 + 5, "Put the board near the meter, or try", LAP_UI_DIM);
        lap_put_str(1, y0 + 6, "MODE (m): S1 or T1 on 868.3 MHz.", LAP_UI_DIM);
        lap_put_str(1, y0 + 8, "Keys: k on a meter, y for the key list,", LAP_UI_DIM);
        lap_put_str(1, y0 + 9, "or /sdcard/wmbus/keys.txt", LAP_UI_DIM);
    }
    for (int r = 0; r < rows && s.top + r < s.nm; r++) {
        const int i = s.top + r;
        const meter_t *mt = &s.m[i];
        char mfr[4], age[8], line[128], type[16], db[8];
        wm_mfr_str(mt->mfr, mfr);
        const uint8_t ty = shown(mt)->data[9];
        const char *tn = wm_type_name_mfr(mt->mfr, ty);
        if (tn) snprintf(type, sizeof(type), "%s", tn);
        else snprintf(type, sizeof(type), "type %02X", ty);
        age_str(mt->last_ms, age, sizeof(age));
        if (mt->rssi == INT16_MIN) snprintf(db, sizeof(db), "  ?");
        else snprintf(db, sizeof(db), "%4d", mt->rssi / 10);
        const unsigned long cnt = mt->count > 9999 ? 9999 : mt->count;
        if (wide)
            snprintf(line, sizeof(line), "%08lX %s %-11.11s %s %4lu %4s ", (unsigned long)mt->id, mfr, type, db,
                     cnt, age);
        else
            snprintf(line, sizeof(line), "%08lX %s %s %4lu ", (unsigned long)mt->id, mfr, db, cnt);
        const bool on = i == s.sel;
        const uint8_t base = on ? LAP_ATTR(LAP_BLACK, LAP_CYAN) : LAP_UI_TEXT;
        if (on) lap_fill(0, y0 + 1 + r, w, 1, ' ', base);
        lap_put_str(0, y0 + 1 + r, line, base);
        const int x = (int)strlen(line);
        const int dec = mt->shows_good ? mt->gdec : mt->dec;
        lap_put_str(x, y0 + 1 + r, state_name(dec, mt->shows_good ? mt->gpub : mt->pubkey),
                    on ? base : dec_color(dec));
        lap_put_str(x + 9, y0 + 1 + r, mt->summary, on ? base : LAP_ATTR(LAP_UI_VALUE, LAP_BLACK));
    }
}

static void draw_detail(int y0, int ky, int w)
{
    if (s.det < 0 || s.det >= s.nm) { s.view = V_LIST; return; }
    const meter_t *mt = &s.m[s.det];
    if (mt->count != s.det_count) detail_refresh();
    const wm_msg_t *m = &s.msg;
    const wm_frame_t *fr = shown(mt);
    char line[128], mfr[4], age[8];
    wm_mfr_str(mt->mfr, mfr);
    const char *tn = wm_type_name_mfr(m->mfr, m->type);
    age_str(mt->shows_good ? mt->good_ms : mt->last_ms, age, sizeof(age));
    int y = y0;
    snprintf(line, sizeof(line), "METER %08lX  %s  %s  ver %02X", (unsigned long)mt->id, mfr, tn ? tn : "?", m->ver);
    lap_put_str(0, y++, line, LAP_ATTR(LAP_UI_VALUE, LAP_BLACK));
    const char *cn = wm_c_name(m->c);
    snprintf(line, sizeof(line), "%s fmt %c  C %02X %s  CI %02X  %d dBm  n=%lu  %s ago", wm_phy_name(fr->phy),
             fr->format, m->c, cn ? cn : "", m->ci, mt->rssi == INT16_MIN ? 0 : mt->rssi / 10,
             (unsigned long)mt->count, age);
    lap_put_str(0, y++, line, LAP_UI_DIM);
    if (mt->shows_good) {
        age_str(mt->last_ms, age, sizeof(age));
        snprintf(line, sizeof(line), "newest (%s ago) %s; shown: earlier one", age,
                 mt->dec == WM_DEC_NOKEY ? "encrypted" : wm_dec_name(mt->dec));
        lap_put_str(0, y++, line, LAP_ATTR(LAP_UI_WARN, LAP_BLACK));
    }
    if (m->has_tpl)
        snprintf(line, sizeof(line), "ACC %02X  status %02X  security mode %u (%u blocks)%s", m->acc, m->status,
                 m->sec, m->enc_blocks, m->has_afl ? "  AFL" : "");
    else
        snprintf(line, sizeof(line), "no transport header%s", m->ell_ci ? "  (ELL)" : "");
    lap_put_str(0, y++, line, LAP_UI_DIM);
    if (m->long_tpl) {
        char tm[4];
        wm_mfr_str(m->tpl_mfr, tm);
        snprintf(line, sizeof(line), "inner meter %08lX %s ver %02X type %02X", (unsigned long)m->tpl_id, tm,
                 m->tpl_ver, m->tpl_type);
        lap_put_str(0, y++, line, LAP_UI_DIM);
    }
    const wm_pubkey_t *pk = m->dec == WM_DEC_OK ? wm_pubkey_get(m->pubkey) : NULL;
    if (pk)
        snprintf(line, sizeof(line), "AES ok: public key \"%s\"", pk->name);
    else if (m->dec == WM_DEC_NOKEY && !s.nopub)
        snprintf(line, sizeof(line), "no key: %s; public keys do not fit", m->note);
    else
        snprintf(line, sizeof(line), "%s%s%s", wm_dec_name(m->dec), m->note[0] ? ": " : "", m->note);
    lap_put_str(0, y++, line, dec_color(m->dec));
    if (m->cut || fr->fixed) {
        int o = 0;
        if (m->cut) o = snprintf(line, sizeof(line), "received: first %u of %u bytes", fr->len, fr->data[0] + 1u);
        if (fr->fixed)
            snprintf(line + o, sizeof(line) - (size_t)o, "%s%u symbols repaired (CRC)", o ? "; " : "", fr->fixed);
        lap_put_str(0, y++, line, LAP_UI_DIM);
    }
    lap_hline(0, y++, w, '-', LAP_UI_FAINT);

    const int rows = ky - y - 1;
    if (rows <= 0) return;
    if (s.hex) {
        const uint8_t *d = m->buf;
        const int n = fr->len, per = w >= 56 ? 16 : 8;
        const int lines = (n + per - 1) / per;
        if (s.dscroll > lines - rows) s.dscroll = lines - rows;
        if (s.dscroll < 0) s.dscroll = 0;
        for (int r = 0; r < rows && (s.dscroll + r) * per < n; r++) {
            const int off = (s.dscroll + r) * per;
            int o = snprintf(line, sizeof(line), "%03X ", off);
            for (int k = 0; k < per && off + k < n; k++) o += snprintf(line + o, sizeof(line) - (size_t)o, "%02X ", d[off + k]);
            lap_put_str(0, y + r, line, off + per <= m->app_off ? LAP_UI_DIM : LAP_UI_TEXT);
        }
        return;
    }
    if (s.dscroll > s.nrec - rows) s.dscroll = s.nrec - rows;
    if (s.dscroll < 0) s.dscroll = 0;
    if (!s.nrec) {
        lap_put_str(1, y + 1,
                    m->dec == WM_DEC_NOKEY ? "Encrypted. Press KEY (k) and type the meter's AES key."
                    : m->dec == WM_DEC_BADKEY ? "The key does not decrypt this telegram. Check it (k)."
                                              : "No data records. HEX (h) shows the raw telegram.",
                    LAP_UI_TEXT);
        return;
    }
    const int vx = w > 60 ? 34 : 24;
    for (int r = 0; r < rows && s.dscroll + r < s.nrec; r++) {
        const wm_rec_t *rc = &s.rec[s.dscroll + r];
        char tag[24] = "";
        int o = 0;
        static const char *const FN[4] = { "", "max", "min", "err" };
        if (rc->storage) o += snprintf(tag + o, sizeof(tag) - (size_t)o, "s%lu ", (unsigned long)rc->storage);
        if (rc->tariff) o += snprintf(tag + o, sizeof(tag) - (size_t)o, "t%u ", rc->tariff);
        if (rc->func) o += snprintf(tag + o, sizeof(tag) - (size_t)o, "%s ", FN[rc->func]);
        snprintf(line, sizeof(line), "%-18.18s %s", rc->name, tag);
        lap_put_str(0, y + r, line, rc->primary ? LAP_UI_TEXT : LAP_UI_DIM);
        const int lx = (int)strlen(line);
        lap_put_str(lx > vx ? lx : vx, y + r, rc->value, LAP_ATTR(rc->primary ? LAP_UI_LIVE : LAP_UI_VALUE, LAP_BLACK));
    }
    if (s.recbad) lap_put_str(w - 10, ky - 1, "truncated", LAP_ATTR(LAP_UI_WARN, LAP_BLACK));
}

/* Built-in public keys at the bottom of the key list; returns rows used. */
static int draw_pubkeys(int ky, int w)
{
    const int n = wm_pubkey_count(), used = n + 2;
    int y = ky - used;
    char line[96];
    snprintf(line, sizeof(line), "PUBLIC KEYS, built in: %s (OPTIONS)", s.nopub ? "off" : "on");
    lap_put_str(0, y++, line, LAP_UI_DIM);
    for (int i = 0; i < n; i++) {
        const wm_pubkey_t *k = wm_pubkey_get(i);
        snprintf(line, sizeof(line), " %-13s %s", k->name, k->who);
        if ((int)strlen(line) > w) line[w] = 0;
        lap_put_str(0, y++, line, s.nopub ? LAP_UI_FAINT : LAP_UI_TEXT);
    }
    return used;
}

static void draw_keys(int y0, int ky, int w)
{
    lap_put_str(0, y0, w >= 60 ? "AES-128 KEYS   (also read from /sdcard/wmbus/keys.txt)"
                               : "AES-128 KEYS (+ /sdcard/wmbus/keys.txt)", LAP_UI_DIM);
    ky -= draw_pubkeys(ky, w);
    const int rows = ky - y0 - 2;
    if (s.ksel >= s.nk) s.ksel = s.nk - 1;
    if (s.ksel < 0) s.ksel = 0;
    if (!s.nk) {
        lap_put_str(1, y0 + 2, "No keys. ADD (a) one, or KEY (k) on a meter.", LAP_UI_TEXT);
        return;
    }
    const int top = s.ksel >= rows ? s.ksel - rows + 1 : 0;
    for (int r = 0; r < rows && top + r < s.nk; r++) {
        const wkey_t *k = &s.keys[top + r];
        char hex[40], line[80], id[12];
        wm_hex_str(k->key, 16, hex, sizeof(hex));
        if (k->any) snprintf(id, sizeof(id), "* (all)");
        else snprintf(id, sizeof(id), "%08lX", (unsigned long)k->id);
        snprintf(line, sizeof(line), "%-9s %.4s........................%.4s", id, hex, hex + 28);
        const bool on = top + r == s.ksel;
        lap_put_str(1, y0 + 1 + r, line, on ? LAP_ATTR(LAP_BLACK, LAP_CYAN) : LAP_UI_TEXT);
    }
}

static int setup_buttons(void)
{
    int n = 0;
    if (s.view == V_LIST) {
        snprintf(s.btn_val[0], sizeof(s.btn_val[0]), "%s", PRESET_NAME[s.preset]);
        snprintf(s.btn_val[1], sizeof(s.btn_val[1]), "%s", s.log_on ? "on" : "off");
        snprintf(s.btn_val[2], sizeof(s.btn_val[2]), "%d", s.nk);
        s.btn[n++] = (lap_btn_t){ "MODE", s.btn_val[0], 'm', false, false, false };
        s.btn[n++] = (lap_btn_t){ "KEY", NULL, 'k', false, s.nm == 0, false };
        s.btn[n++] = (lap_btn_t){ "KEYS", s.btn_val[2], 'y', false, false, false };
        s.btn[n++] = (lap_btn_t){ "LOG", s.btn_val[1], 'g', s.log_on, false, false };
        s.btn[n++] = (lap_btn_t)LAP_BTN_OPTIONS;
    } else if (s.view == V_DETAIL) {
        s.btn[n++] = (lap_btn_t){ "BACK", NULL, 'b', false, false, false };
        s.btn[n++] = (lap_btn_t){ "KEY", NULL, 'k', false, false, false };
        s.btn[n++] = (lap_btn_t){ "HEX", NULL, 'h', s.hex, false, false };
    } else if (s.view == V_SCAN) {
        s.btn[n++] = (lap_btn_t){ "BACK", NULL, 'b', false, false, false };
        s.btn[n++] = (lap_btn_t){ s.scan_on ? "STOP" : "AGAIN", NULL, 's', false, false, false };
    } else {
        s.btn[n++] = (lap_btn_t){ "BACK", NULL, 'b', false, false, false };
        s.btn[n++] = (lap_btn_t){ "ADD", NULL, 'a', false, false, false };
        s.btn[n++] = (lap_btn_t){ "DELETE", NULL, 'd', false, s.nk == 0, false };
    }
    return n;
}

static void draw_scan(int y0, int ky, int w)
{
    char line[96];
    (void)w;
    const char *what = s.scan_kind == SK_PROF ? "RADIO SCAN" : "TRIM SCAN";
    if (s.scan_on)
        snprintf(line, sizeof(line), "%s %s: step %d/%d, %lus left", what, PRESET_NAME[s.preset], s.scan_i + 1,
                 scan_n(), (unsigned long)((SCAN_STEP_MS - (lap_millis() - s.scan_ms)) / 1000));
    else if (s.scan_kind == SK_PROF)
        snprintf(line, sizeof(line), "%s %s: done, back to standard", what, PRESET_NAME[s.preset]);
    else
        snprintf(line, sizeof(line), "%s %s: done, trim now %s", what, PRESET_NAME[s.preset], TRIM_NAMES[s.trim]);
    lap_put_str(0, y0, line, LAP_ATTR(LAP_UI_VALUE, LAP_BLACK));
    lap_put_str(0, y0 + 1, s.scan_kind == SK_PROF ? " receiver frames  bytes  longest  repaired"
                                                  : " trim     frames  bytes  longest  repaired", LAP_UI_DIM);
    const int best = s.scan_on ? -1 : scan_best();
    for (int i = 0; i < scan_n() && y0 + 2 + i < ky - 1; i++) {
        const bool done = !s.scan_on || i < s.scan_i, now = s.scan_on && i == s.scan_i;
        if (done || now)
            snprintf(line, sizeof(line), " %-8s %6lu %6lu %8lu %9lu%s", scan_label(i), (unsigned long)s.sc_frames[i],
                     (unsigned long)s.sc_bytes[i], (unsigned long)s.sc_max[i], (unsigned long)s.sc_fixed[i],
                     now ? "  <" : s.sc_open_err[i] ? "  refused" : i == best ? "  best" : "");
        else
            snprintf(line, sizeof(line), " %-8s", scan_label(i));
        lap_put_str(0, y0 + 2 + i, line, now ? LAP_ATTR(LAP_BLACK, LAP_CYAN) : i == best ? LAP_ATTR(LAP_UI_LIVE, LAP_BLACK)
                                                                             : done ? LAP_UI_TEXT : LAP_UI_FAINT);
    }
}

static void draw(void)
{
    radio_pump();
    dialog_pump();
    log_pump();
    scan_pump();
    if (s.impjob > 0) {
        const int r = lap_sd_poll(s.impjob);
        if (r != LAP_SD_PENDING) {
            s.impjob = 0;
            if (r > 0) {
                s.imp[r < (int)sizeof(s.imp) ? r : (int)sizeof(s.imp) - 1] = 0;
                const int k = import_keys(s.imp, r);
                if (k) {
                    keys_save();
                    reanalyze_all();
                }
                say("keys.txt: %d key(s) read", k);
            } else if (s.imp_user) {
                say(r == LAP_SD_ENOENT ? "no " KEYS_TXT : "cannot read " KEYS_TXT);
            }
        }
    }

    const int w = lap_cols(), h = lap_rows();
    lap_clear(LAP_UI_DIM);
    const int y0 = draw_header(w);
    const int n = setup_buttons();
    const int kh = lap_ui_keys_rows(n), ky = h - kh;
    if (s.view == V_LIST) draw_list(y0, ky, w);
    else if (s.view == V_DETAIL) draw_detail(y0, ky, w);
    else if (s.view == V_SCAN) draw_scan(y0, ky, w);
    else draw_keys(y0, ky, w);
    if (s.status[0] && lap_millis() - s.status_ms < 6000)
        lap_put_str(0, ky - 1, s.status, LAP_ATTR(LAP_UI_WARN, LAP_BLACK));
    lap_ui_keys(0, ky, w, kh, s.btn, n, -1);
}

/* ---- input ----------------------------------------------------------------------- */

static void open_detail(int i)
{
    if (i < 0 || i >= s.nm) return;
    s.det = i;
    s.view = V_DETAIL;
    s.dscroll = 0;
    detail_refresh();
}

static bool action(char ch)
{
    switch (s.view) {
    case V_LIST:
        switch (ch) {
        case 'm': set_preset(s.preset + 1); return true;
        case 'k':
            if (s.nm) ask_key(s.m[s.sel].id, false);
            return true;
        case 'y': s.view = V_KEYS; return true;
        case 'x': s.view = V_SCAN; return true;
        case 'g': log_set(!s.log_on); return true;
        case 'o': return lap_options_open(&OPTIONS);
        }
        break;
    case V_DETAIL:
        switch (ch) {
        case 'b': s.view = V_LIST; return true;
        case 'k': ask_key(s.m[s.det].id, false); return true;
        case 'h': s.hex = !s.hex; s.dscroll = 0; return true;
        }
        break;
    case V_SCAN:
        switch (ch) {
        case 'b': s.view = V_LIST; return true;   /* a running scan goes on */
        case 's':
            if (s.scan_on) scan_stop(false);
            else scan_start(s.scan_kind);
            return true;
        }
        break;
    case V_KEYS:
        switch (ch) {
        case 'b': s.view = V_LIST; return true;
        case 'a': ask_id(); return true;
        case 'd':
            if (s.nk && s.ksel < s.nk) {
                memmove(&s.keys[s.ksel], &s.keys[s.ksel + 1], (size_t)(s.nk - s.ksel - 1) * sizeof(wkey_t));
                s.nk--;
                keys_save();
                reanalyze_all();
                say("key deleted");
            }
            return true;
        }
        break;
    }
    return false;
}

static bool key(lap_key_t k, char ch)
{
    if (s.dlg_kind != D_NONE) return false;
    const int page = 8;
    switch (k) {
    case LAP_KEY_UP:
    case LAP_KEY_DOWN:
    case LAP_KEY_PGUP:
    case LAP_KEY_PGDN: {
        const int d = k == LAP_KEY_UP ? -1 : k == LAP_KEY_DOWN ? 1 : k == LAP_KEY_PGUP ? -page : page;
        if (s.view == V_LIST) s.sel += d;
        else if (s.view == V_DETAIL) s.dscroll += d;
        else s.ksel += d;
        return true;
    }
    case LAP_KEY_ENTER:
        if (s.view == V_LIST) open_detail(s.sel);
        return true;
    case LAP_KEY_BACK:
        if (s.view != V_LIST) { s.view = V_LIST; return true; }
        return false;
    case LAP_KEY_CHAR:
        return action((char)(ch | 0x20));
    default:
        return false;
    }
}

static bool touch(int col, int row)
{
    const int b = lap_ui_key_hit(col, row);
    if (b >= 0 && b < 6 && s.btn[b].key) return action(s.btn[b].key);
    (void)col;
    if (s.view == V_LIST) {
        const int i = s.top + row - 4;  /* header 3 rows + column titles */
        if (row >= 4 && i < s.nm) {
            if (i == s.sel) open_detail(i);
            else s.sel = i;
            return true;
        }
    } else if (s.view == V_KEYS) {
        const int i = row - 4;
        if (row >= 4 && i < s.nk) { s.ksel = i; return true; }
    } else if (s.view == V_DETAIL) {
        s.dscroll += row > lap_rows() / 2 ? 4 : -4;
        return true;
    }
    return false;
}

static void enter(void)
{
    memset(&s, 0, sizeof(s));
    s.radio = -1;
    s.det = -1;
    int p = 0;
    if (lap_settings_get("preset", &p, sizeof(p)) == (int)sizeof(p) && p >= 0 && p < P_COUNT) s.preset = p;
    uint8_t off = 0;
    if (lap_settings_get("nopub", &off, sizeof(off)) == (int)sizeof(off)) s.nopub = off != 0;
    int t = TRIM_ZERO;
    s.trim = TRIM_ZERO;
    if (lap_settings_get("trim", &t, sizeof(t)) == (int)sizeof(t) && t >= 0 && t < NTRIM) s.trim = t;
    keys_load();
    import_start(false);
    radio_open();
    s.retry_ms = lap_millis();
    say("listening: %s", PRESET_LONG[s.preset]);
}

static void leave(void)
{
    radio_close();
    if (s.loglen) lap_sd_write(LOG_PATH, LAP_SD_APPEND, s.logbuf, (uint32_t)s.loglen);
    s.loglen = 0;
}

LAP_APP(.id = "wmbus", .name = "WM-BUS", .sub = "Wireless M-Bus meters", .version = "0.3",
        .author = "zenobik", .color = LAP_CYAN, .flags = LAP_CAP_RADIO_RX,
        .icon_art = { " .------. ", " | 0123 | ", " '------' ", "   ))))   ", "  T1 C1   ", "  WM-BUS  " },
        .enter = enter, .leave = leave, .draw = draw, .key = key, .touch = touch);
