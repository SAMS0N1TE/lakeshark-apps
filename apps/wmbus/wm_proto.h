/* Wireless M-Bus (EN 13757-4 / -3, OMS) receive side: what the radio hands
 * over after the sync word, down to the meter's readings.
 *
 *   wm_phy_*     radio bytes -> link-layer frame (3-of-6, NRZ or Manchester),
 *                CRC blocks checked and removed
 *   wm_parse     link layer, ELL, AFL and transport layer; decrypts security
 *                mode 5 (AES-CBC), mode 7 (AES-CBC, CMAC-derived key) and
 *                ELL AES-CTR with the key the caller supplies
 *   wm_records   DIF/VIF data records -> name, value and unit as text
 *
 * Pure C, no board calls: the same file is unit-tested on a PC. */
#ifndef WM_PROTO_H
#define WM_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Radio modes, as the frame arrived. */
enum { WM_PHY_T1 = 1, WM_PHY_C1A, WM_PHY_C1B, WM_PHY_S1 };
const char *wm_phy_name(int phy);

/* Errors. */
enum {
    WM_OK = 0,
    WM_E_CODING = -1,           /* not a valid 3-of-6 / Manchester symbol */
    WM_E_SHORT = -2,            /* fewer bytes than the L field announces */
    WM_E_CRC = -3,
    WM_E_LEN = -4,              /* L field too small */
    WM_E_SYNC = -5,             /* C mode: second sync word not 54CD/543D */
};

#define WM_FRAME_MAX 256

/* A link-layer frame with the CRCs removed: data[0] is the L field as if the
   frame were format A (bytes after L, without CRCs), data[1] the C field. */
typedef struct {
    uint8_t phy;                /* WM_PHY_* */
    uint8_t format;             /* 'A' or 'B' */
    uint16_t len;               /* bytes in data */
    uint8_t cut;                /* the radio packet ended inside the frame: data holds
                                   only its first CRC blocks, data[0] keeps the
                                   frame's own L field; also when a later block was
                                   past repair */
    uint8_t fixed;              /* T1: 3-of-6 symbols repaired with the block CRCs */
    uint8_t data[WM_FRAME_MAX];
} wm_frame_t;

uint16_t wm_crc(const uint8_t *d, size_t n);

/* raw: bytes that followed the 16-bit sync word 0x543D (T1 and C1 share it,
   so both are tried: C1 starts with 54CD / 543D, T1 is 3-of-6 coded).
   A format A frame longer than the packet (a 209-byte T1 telegram needs 356
   radio bytes, the radio gives 255) comes back cut: its complete CRC blocks,
   at least the header and one data block. Returns WM_OK or WM_E_*. */
int wm_phy_decode_tc(const uint8_t *raw, size_t n, wm_frame_t *out);
/* Radio bytes taken by the frame wm_phy_decode_tc just decoded from raw
   (n when it was cut). */
size_t wm_phy_used_tc(const wm_frame_t *f, size_t n);
/* Looks for another T/C frame in raw[from..n): preamble end and sync
   (bits 0x55543D) at any bit position. Copies the bytes after the sync,
   realigned, to out (at most cap) and returns their count, or 0. *next is
   where to look for a further one. */
size_t wm_find_sync_tc(const uint8_t *raw, size_t n, size_t from, uint8_t *out, size_t cap, size_t *next);
/* raw: bytes that followed the S-mode sync 0x7696, Manchester coded. */
int wm_phy_decode_s(const uint8_t *raw, size_t n, wm_frame_t *out);
/* Undo the CRC blocks of a frame already decoded to bytes (format 'A'/'B'). */
int wm_strip_crc(const uint8_t *in, size_t n, int format, wm_frame_t *out);

/* Decryption state of a parsed telegram. */
enum {
    WM_DEC_PLAIN = 0,           /* nothing was encrypted */
    WM_DEC_OK,                  /* decrypted, 2F2F check passed */
    WM_DEC_NOKEY,               /* encrypted, no key for this meter */
    WM_DEC_BADKEY,              /* decrypted with the key, check failed */
    WM_DEC_UNSUP,               /* a security mode / layer this app does not do */
};
const char *wm_dec_name(int dec);

/* Key lookup -> 16-byte key or NULL. id is the meter the key belongs to (the
   long transport header's id when there is one, else the link layer's), link_id
   the link layer's id (the same number without a long header). Both are the
   32-bit little-endian A-field number as printed. A radio converter or repeater
   sends its own id in the link layer and the meter's in the long header, so the
   callback should try a key for id, then for link_id, then a default key. */
typedef const uint8_t *(*wm_keyfn_t)(uint32_t id, uint32_t link_id, void *ctx);

/* Manufacturer code of three letters, as in the M field: WM_MFR('D','M','E'). */
#define WM_MFR(a, b, c) ((uint16_t)((((a) - 64) << 10) | (((b) - 64) << 5) | ((c) - 64)))

/* Public keys: AES keys that are not secret because the manufacturer ships every
   meter of a family with them unless the installer sets its own (documented in
   open-source decoders and integrators' compatibility lists, see wm_proto.c).
   They are tried only after the user's key, only with WM_TRY_PUBLIC, and a
   result counts only when the 2F2F check and the data records both hold. */
enum { WM_PK_MODE5 = 1, WM_PK_MODE7 = 2, WM_PK_ELL = 4 };
typedef struct {
    const char *name;           /* short, shown on the meter's page */
    const char *who;            /* the meters it is known for */
    uint16_t mfr[6];            /* the manufacturers it is tried for; none: every meter */
    uint8_t modes;              /* WM_PK_* */
    uint8_t key[16];
} wm_pubkey_t;
int wm_pubkey_count(void);
const wm_pubkey_t *wm_pubkey_get(int i);

typedef struct {
    /* link layer */
    uint8_t c, ver, type;
    uint16_t mfr;
    uint32_t id;
    /* transport layer, when there is one */
    uint8_t ci;                 /* CI of the transport/application layer */
    bool has_tpl, long_tpl;
    uint32_t tpl_id;
    uint16_t tpl_mfr;
    uint8_t tpl_ver, tpl_type;
    uint8_t acc, status;
    uint16_t cfg;
    uint8_t sec;                /* security mode, cfg bits 8..12 */
    uint8_t enc_blocks;
    /* extended link layer / authentication layer */
    uint8_t ell_ci, ell_cc, ell_acc;
    uint32_t ell_sn;
    bool has_afl, has_mcr;
    uint32_t mcr;
    /* result */
    int dec;                    /* WM_DEC_* */
    int8_t pubkey;              /* WM_DEC_OK with a public key: its index, else -1 */
    uint8_t mfct;               /* a manufacturer format wm_msg_records reads (WM_MFCT_*) */
    bool cut;                   /* the frame was cut by the radio (wm_frame_t.cut) */
    char note[40];              /* why, when not PLAIN/OK */
    uint8_t buf[WM_FRAME_MAX];  /* the frame, decrypted in place */
    uint16_t app_off, app_len;  /* the data records in buf */
} wm_msg_t;

int wm_parse(const wm_frame_t *f, wm_keyfn_t keyfn, void *ctx, wm_msg_t *m);
/* flags: WM_TRY_PUBLIC also tries the public keys that fit the meter. */
enum { WM_TRY_PUBLIC = 1 };
int wm_parse_ex(const wm_frame_t *f, wm_keyfn_t keyfn, void *ctx, unsigned flags, wm_msg_t *m);

/* Manufacturer formats read without a key (CI A0 / A1 / A2 of Techem heat
   meters and heat cost allocators). */
enum { WM_MFCT_NONE = 0, WM_MFCT_TCH_VARIO451, WM_MFCT_TCH_COMPACT5, WM_MFCT_TCH_FHKV3, WM_MFCT_TCH_RADIO };

/* "ABC" from the 16-bit manufacturer field. */
void wm_mfr_str(uint16_t mfr, char out[4]);
const char *wm_type_name(uint8_t type);
/* The same, with the device types a manufacturer numbers its own way
   (Techem 0x80 heat cost allocator, 0x43 heat meter, ...). */
const char *wm_type_name_mfr(uint16_t mfr, uint8_t type);
const char *wm_c_name(uint8_t c);

typedef struct {
    char name[20];              /* "Volume", "Date", "Error flags" ... */
    char value[24];             /* "123.456 m3" */
    uint8_t func;               /* 0 inst, 1 max, 2 min, 3 error */
    uint8_t tariff;
    uint16_t unit;
    uint32_t storage;
    bool primary;               /* volume, energy or HCA, storage 0, inst. */
} wm_rec_t;

/* Decodes up to max records from the application data. Returns the count;
   *bad is set when the data ended inside a record or held a reserved code. */
int wm_records(const uint8_t *d, size_t n, wm_rec_t *out, int max, bool *bad);
/* The records of a parsed telegram: wm_records over its data, or the readings
   of a manufacturer format (m->mfct). None unless it is plain or decrypted. */
int wm_msg_records(const wm_msg_t *m, wm_rec_t *out, int max, bool *bad);

/* Hex helpers, used for keys and logs. */
int wm_hex_parse(const char *s, uint8_t *out, int max);  /* bytes, or -1 */
void wm_hex_str(const uint8_t *d, size_t n, char *out, size_t cap);

#endif
