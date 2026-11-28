/*
 * ROLIMA - ladeira abaixo v0.16
 * Carrinho de rolima descendo o morro de Osasco, o morro da vo ou a Consolacao.
 * 3D por software (estrada em segmentos + casas em caixas 3D), cor cheia
 * em RGB565 com lap_image, no mesmo caminho do OSASCO RACER.
 *
 * Autores: wmo586 & Claude AI - 2026-11-26
 * v0.2: visual mais quadrado (low poly): carrinho e piloto em caixas 3D com
 *       sombreamento chapado, rolamentos em bloco, camera mais alta e atras.
 * v0.3: trilha eletronica (128 BPM) e efeitos gerados no codigo, no mixer;
 *       botao SOM liga/desliga.
 * v0.4: trilha mais pesada, com base de rock industrial.
 * v0.5: pista de ~90 s so de descida; tela cheia com escala inteira; botoes
 *       SAIR e SOM grandes dentro da imagem; muro nao deixa mais o carrinho
 *       parado (volta para a calcada e empurra com o pe quando esta devagar).
 * v0.6: vibra so quando o carrinho bate no chao depois da lombada e quando
 *       bate em outro carrinho.
 * v0.7: pista 2 "Morro da Vo" (estrada de terra no sitio, ~70 s, com o
 *       TACA-LE PAU! na tela) e menu para escolher a pista.
 * v0.8: trilha propria da pista 2: bluegrass com banjo.
 * v0.9: pista 3 "Rua da Consolacao" (avenida larga ao entardecer, predios,
 *       postes acesos, carros na mao contraria) com trilha punk; menu com 3 pistas.
 * v0.10: escolha do piloto: executiva, funkeiro, descolada e ET; os outros
 *        tres viram os adversarios.
 * v0.11: nomes dos pilotos: Monalisa, MC 171, Luiza e Bilu.
 * v0.12: final animado: campeao pula com os bracos para cima (e o aparelho
 *        vibra); quem perde balanca a cabeca baixa; o Bilu perdendo explode e
 *        a meleca verde gruda na tela.
 * v0.13: pista 4 "Pista Pro": pista profissional de descida na serra, com
 *        guard rail, faixas pintadas, chicanes com zebra e cones, feno,
 *        arquibancadas e portais com faixa.
 * v0.14: desempenho: fisica em passo fixo (60/s), neblina em tabela, casas e
 *        objetos ordenados (so a parte visivel e percorrida), janelas menores
 *        que 1 pixel nao sao desenhadas, celulas limpas uma vez so; medidor
 *        de QPS (toque no relogio); trilha surf rock na Pista Pro.
 * v0.15: campeonato (4 pistas, pontos 10/6/3/1, podio), recorde por pista
 *        salvo no cartao, carrinho fantasma da melhor descida, turbo de
 *        vacuo, dificuldade (facil/normal/dificil), cachorro e galinha
 *        atravessando, onibus na Consolacao, torcida pulando na Pista Pro.
 * v0.16: desenho mais leve: poligonos e trapezios preenchidos em ponto
 *        fixo 22.10 (uma divisao por aresta, nao por linha), trechos
 *        horizontais escritos 2 pixels por vez (32 bits), listras da pista
 *        como trechos inteiros, sombra/brilho/cores em inteiros, letras em
 *        trechos, seno por tabela nas animacoes, audio sem divisao por
 *        amostra. Nenhuma alocacao dentro do draw().
 *
 * Controles (toque): lado esquerdo vira para a esquerda, lado direito vira
 * para a direita, meio da tela = pe no chao (freio). Teclado BT opcional:
 * setas esquerda/direita, seta para baixo ou espaco freia.
 * Botoes SAIR (X vermelho) e SOM no canto superior esquerdo.
 */
#pragma GCC optimize("O2")
#include "lakeshark_app.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define WANT_LANDSCAPE 1

/* ---- imagem ----------------------------------------------------------- */
/* a imagem cobre a tela toda; o tamanho sai da tela dividida por um inteiro
   (1168x540 -> 292x135), para cada pixel virar um bloco exato na tela */
#define FW_MAX 400
#define FH_MAX 240
static int FW = 320, FH = 180;
static float HALFW = 160.0f, HOR = 58.0f;

/* ---- mundo (500 unidades ~ 1 metro) ----------------------------------- */
#define SEG 200.0f          /* comprimento de um segmento */
static float RW = 2000.0f;    /* meia largura da rua (a Consolacao e mais larga) */
#define CAM_H 1100.0f       /* altura da camera sobre a rua do jogador */
#define CAM_DEPTH 0.84f
#define PLAYER_DZ 1800.0f   /* distancia camera -> carrinho (camera atras e acima) */
#define DRAW_N 110          /* segmentos desenhados */
#define NSEG_MAX 4400

#define GRAV 4900.0f
#define DRAG 6.6e-6f
#define ROLL 60.0f
#define VMAX 11000.0f
#define START_PUSH 2000.0f

typedef struct { uint8_t r, g, b; } rgb_t;

typedef uint32_t __attribute__((may_alias)) u32a;   /* acesso de 32 bits ao quadro de 16 */
static uint16_t *fb, *bgimg;
static bool g_blend;                    /* desenho translucido (o fantasma) */
static uint32_t serial;
static bool mem_fail, bg_ready;

/* ======================================================================
   cores e pixels
   ====================================================================== */

/* seno por tabela (1024 pontos, interpolado): usado em tudo que roda a cada quadro */
#define SIN_N 1024
static float sin_tab[SIN_N + 1];
static void sin_init(void)
{
    for (int i = 0; i <= SIN_N; i++) sin_tab[i] = sinf((float)i * (2.0f * 3.14159265f / SIN_N));
}
static inline float fsin(float a)
{
    float t = a * (SIN_N / (2.0f * 3.14159265f));
    float fl = floorf(t);
    int i = (int)fl & (SIN_N - 1);
    float f = t - fl;
    return sin_tab[i] + (sin_tab[i + 1] - sin_tab[i]) * f;
}
static inline float fcos(float a) { return fsin(a + 1.5707963f); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline uint16_t pk(int r, int g, int b)
{
    r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static rgb_t FOGC = { 240, 150, 120 };
#define NCHAR 4
static int my_char = 0;                 /* piloto escolhido */
static int trk = 0;                     /* 0 = morro ao por do sol (Osasco), 1 = morro da vo (sitio) */
static uint32_t shout_t0, last_shout;   /* "TACA-LE PAU!" na tela */
static bool shout_on;

/* cor c clareada/escurecida por k e misturada com a neblina f (0..1) */
static uint16_t shade(rgb_t c, float k, float f)
{
    int ki = (int)(k * 256.0f), fi = (int)(f * 256.0f);   /* ponto fixo 8.8 */
    int r = (c.r * ki) >> 8, g = (c.g * ki) >> 8, b = (c.b * ki) >> 8;
    r += ((FOGC.r - r) * fi) >> 8; g += ((FOGC.g - g) * fi) >> 8; b += ((FOGC.b - b) * fi) >> 8;
    return pk(r, g, b);
}
static inline void hspan(int y, int x0, int x1, uint16_t c)
{
    if (y < 0 || y >= FH) return;
    if (x0 < 0) x0 = 0;
    if (x1 > FW - 1) x1 = FW - 1;
    if (x1 < x0) return;
    uint16_t *p = fb + y * FW;
    if (g_blend) {                                    /* meio a meio com o que ja esta na tela */
        uint16_t h = (uint16_t)((c >> 1) & 0x7BEF);
        for (int x = x0; x <= x1; x++) p[x] = (uint16_t)(((p[x] >> 1) & 0x7BEF) + h);
        return;
    }
    /* dois pixels por escrita de 32 bits: metade dos acessos a memoria */
    p += x0;
    int n = x1 - x0 + 1;
    if ((uintptr_t)p & 2) { *p++ = c; n--; }
    u32a *q = (u32a *)p;
    uint32_t cc = (uint32_t)c | ((uint32_t)c << 16);
    for (; n >= 2; n -= 2) *q++ = cc;
    if (n) *(uint16_t *)q = c;
}
static inline void pset(int x, int y, uint16_t c)
{
    if (x >= 0 && x < FW && y >= 0 && y < FH) fb[y * FW + x] = c;
}
static void rect(float x0, float y0, float x1, float y1, uint16_t c)
{
    int ya = (int)floorf(y0), yb2 = (int)floorf(y1);
    for (int y = ya; y <= yb2; y++) hspan(y, (int)floorf(x0), (int)floorf(x1), c);
}

/* ---- preenchimento em ponto fixo 22.10 -----------------------------------
   As bordas andam linha a linha somando um passo inteiro (sem divisao por
   linha). Coordenadas absurdas (vertice quase na camera) caem no caminho
   em float, que tambem anda por soma. */
#define FXB 10
#define FXH (1 << (FXB - 1))
#define FX_LIM 1.0e6f
static inline bool fx_ok(float v) { return v > -FX_LIM && v < FX_LIM; }
/* piso e teto sem chamar a biblioteca (valores dentro de +-2e9) */
static inline int32_t ifloor(float v) { int32_t i = (int32_t)v; return i - (v < (float)i); }
static inline int32_t iceil(float v) { int32_t i = (int32_t)v; return i + (v > (float)i); }
static inline int32_t fx_of(float v) { return ifloor(v * (float)(1 << FXB) + 0.5f); }

/* trapezio horizontal: base perto (x1,y1,meia largura w1), longe (x2,y2,w2) */
static void trap(float x1, float y1, float w1, float x2, float y2, float w2, uint16_t c)
{
    float dy = y1 - y2;
    if (dy < 0.001f) return;
    if (y1 < 0 || y2 >= FH) return;
    int ys = y2 < -1 ? 0 : ifloor(y2) + 1, ye = y1 > FH - 1 ? FH - 1 : ifloor(y1);
    if (ys < 0) ys = 0;
    if (ys > ye) return;
    float sl = ((x1 - w1) - (x2 - w2)) / dy, sr = ((x1 + w1) - (x2 + w2)) / dy, t0 = ys - y2;
    float l0 = (x2 - w2) + sl * t0, r0 = (x2 + w2) + sr * t0;
    float l1 = l0 + sl * (ye - ys), r1 = r0 + sr * (ye - ys);
    if (fx_ok(l0) && fx_ok(r0) && fx_ok(l1) && fx_ok(r1)) {
        int32_t lf = fx_of(l0), rf = fx_of(r0), dl = fx_of(sl), dr = fx_of(sr);
        for (int y = ys; y <= ye; y++, lf += dl, rf += dr) hspan(y, lf >> FXB, rf >> FXB, c);
    } else {
        for (int y = ys; y <= ye; y++, l0 += sl, r0 += sr) hspan(y, ifloor(clampf(l0, -FX_LIM, FX_LIM)), ifloor(clampf(r0, -FX_LIM, FX_LIM)), c);
    }
}
/* o mesmo, listrado (lombada, chegada): cada listra vira um trecho horizontal */
static void trap_stripes(float x1, float y1, float w1, float x2, float y2, float w2,
                         uint16_t c1, uint16_t c2, int n, int flip)
{
    float dy = y1 - y2;
    if (dy < 0.001f || n < 1) return;
    if (y1 < 0 || y2 >= FH) return;
    int ys = y2 < -1 ? 0 : ifloor(y2) + 1, ye = y1 > FH - 1 ? FH - 1 : ifloor(y1);
    if (ys < 0) ys = 0;
    float sl = ((x1 - w1) - (x2 - w2)) / dy, sr = ((x1 + w1) - (x2 + w2)) / dy, t0 = ys - y2;
    float l0 = (x2 - w2) + sl * t0, r0 = (x2 + w2) + sr * t0;
    for (int y = ys; y <= ye; y++, l0 += sl, r0 += sr) {
        if (!fx_ok(l0) || !fx_ok(r0)) continue;
        int xa = ifloor(l0), xb = ifloor(r0);
        if (xb <= xa) continue;
        int w = xb - xa + 1;
        for (int k = 0; k < n; k++) {
            int sa = xa + (k * w + n - 1) / n, sb = xa + ((k + 1) * w + n - 1) / n - 1;
            if (sb < 0 || sa > FW - 1) continue;
            hspan(y, sa, sb, ((k + flip) & 1) ? c2 : c1);
        }
    }
}

/* bordas de um poligono convexo (ate 8 pontos), linha a linha:
   sc_l = borda esquerda ja arredondada, sc_r = direita */
static int32_t sc_l[FH_MAX], sc_r[FH_MAX];
static bool scan_poly(const float *xs, const float *ys, int n, int *py0, int *py1)
{
    float miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    if (maxy < 0 || miny >= FH) return false;
    int y0 = miny < 0 ? 0 : iceil(miny), y1 = maxy > FH - 1 ? FH - 1 : ifloor(maxy);
    if (y0 > y1) return false;
    for (int y = y0; y <= y1; y++) { sc_l[y] = INT32_MAX; sc_r[y] = INT32_MIN; }
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xa = xs[j], ya = ys[j], xb = xs[i], yb = ys[i];
        if (ya == yb) continue;
        if (ya > yb) { float t = xa; xa = xb; xb = t; t = ya; ya = yb; yb = t; }   /* sempre de cima para baixo */
        if (yb <= y0 || ya > y1 + 1) continue;                      /* aresta fora das linhas da tela */
        int ea = ya < y0 ? y0 : iceil(ya), eb = yb > y1 + 1 ? y1 : iceil(yb) - 1;   /* linhas com ya <= y < yb */
        if (ea < y0) ea = y0;
        if (eb > y1) eb = y1;
        if (ea > eb) continue;
        float s = (xb - xa) / (yb - ya);                           /* uma divisao por aresta */
        float x0 = xa + (ea - ya) * s, xe = x0 + (eb - ea) * s;
        if (fx_ok(x0) && fx_ok(xe) && s > -FX_LIM && s < FX_LIM) {
            int32_t xf = fx_of(x0), dx = fx_of(s);
            for (int y = ea; y <= eb; y++, xf += dx) {
                int32_t l = (xf + FXH) >> FXB, r = (xf - FXH) >> FXB;
                if (l < sc_l[y]) sc_l[y] = l;
                if (r > sc_r[y]) sc_r[y] = r;
            }
        } else {
            for (int y = ea; y <= eb; y++, x0 += s) {
                float xc = clampf(x0, -FX_LIM, FX_LIM);
                int32_t l = ifloor(xc + 0.5f), r = ifloor(xc - 0.5f);
                if (l < sc_l[y]) sc_l[y] = l;
                if (r > sc_r[y]) sc_r[y] = r;
            }
        }
    }
    *py0 = y0; *py1 = y1;
    return true;
}
/* poligono convexo (ate 8 pontos) */
static void poly(const float *xs, const float *ys, int n, uint16_t c)
{
    int y0, y1;
    if (!scan_poly(xs, ys, n, &y0, &y1)) return;
    for (int y = y0; y <= y1; y++) {
        int32_t xa = sc_l[y], xb = sc_r[y];
        if (xa == INT32_MAX) continue;
        hspan(y, xa, xb < xa ? xa : xb, c);
    }
}

static void line(float fx0, float fy0, float fx1, float fy1, uint16_t c)
{
    int x0 = (int)fx0, y0 = (int)fy0, x1 = (int)fx1, y1 = (int)fy1;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int guard = 0; guard < 2000; guard++) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/* poligono escurecido (sombra no chao) */
static void poly_dark(const float *xs, const float *ys, int n, float k)
{
    int y0, y1;
    if (!scan_poly(xs, ys, n, &y0, &y1)) return;
    int ki = (int)(k * 256.0f);
    for (int y = y0; y <= y1; y++) {
        int32_t xl = sc_l[y], xr = sc_r[y];
        if (xl == INT32_MAX) continue;
        int xa = clampi((int)xl, 0, FW - 1), xb = clampi((int)xr, 0, FW - 1);
        uint16_t *p = fb + y * FW;
        for (int x = xa; x <= xb; x++) {
            uint16_t v = p[x];
            int r = (((v >> 11) & 31) * ki) >> 8, g = (((v >> 5) & 63) * ki) >> 8, bb = ((v & 31) * ki) >> 8;
            p[x] = (uint16_t)((r << 11) | (g << 5) | bb);
        }
    }
}

/* ---- fonte 5x7 dentro da imagem --------------------------------------- */
static const char FCH[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!:./-";
static const uint8_t FNT[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},{0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
};
static int8_t glyph_ix[128];                     /* caractere -> indice na fonte (-1 = nada) */
static void glyph_init(void)
{
    for (int i = 0; i < 128; i++) glyph_ix[i] = -1;
    for (int i = 0; FCH[i]; i++) glyph_ix[(uint8_t)FCH[i]] = (int8_t)i;
}
static void glyph_rows(int x, int y, char ch, int sc, uint16_t c, int nrows)
{
    if ((uint8_t)ch >= 128 || glyph_ix[(uint8_t)ch] < 0) return;
    const uint8_t *g = FNT[glyph_ix[(uint8_t)ch]];
    for (int r = 0; r < nrows; r++) {
        uint8_t bits = g[r];
        for (int k = 0; k < 5;) {
            if (!(bits & (0x10 >> k))) { k++; continue; }
            int k0 = k;
            while (k < 5 && (bits & (0x10 >> k))) k++;             /* pixels seguidos: um trecho so */
            for (int yy = 0; yy < sc; yy++) hspan(y + r * sc + yy, x + k0 * sc, x + k * sc - 1, c);
        }
    }
}
static void glyph(int x, int y, char ch, int sc, uint16_t c) { glyph_rows(x, y, ch, sc, c, 7); }
static int text_w(const char *s, int sc) { int n = (int)strlen(s); return n ? n * 6 * sc - sc : 0; }
static void text(int x, int y, const char *s, int sc, uint16_t c, bool shadow)
{
    for (int i = 0; s[i]; i++) {
        int o = sc > 3 ? 2 : (sc > 1 ? 1 : 1);
        if (shadow) glyph(x + i * 6 * sc + o, y + o, s[i], sc, 0);
        glyph(x + i * 6 * sc, y, s[i], sc, c);
    }
}
static void text_c(int y, const char *s, int sc, uint16_t c) { text((FW - text_w(s, sc)) / 2, y, s, sc, c, true); }

/* ======================================================================
   a pista
   ====================================================================== */
static float yb[NSEG_MAX + 1];      /* altura da rua em cada divisa */
static float curv[NSEG_MAX];        /* curva de cada segmento */
static uint8_t sflag[NSEG_MAX];     /* 1 lombada, 2 largada, 3 chegada */
static int nseg;
#define PI_F 3.14159265f
#define START_SEG 11

static float ease_in(float a, float b, float p) { return a + (b - a) * p * p; }
static float ease_out(float a, float b, float p) { return a + (b - a) * (1.0f - (1.0f - p) * (1.0f - p)); }
static float ease_io(float a, float b, float p) { return a + (b - a) * (0.5f - 0.5f * cosf(p * PI_F)); }

static void add_road(int enter, int hold, int leave, float c, float dy)
{
    float y0 = yb[nseg];
    int total = enter + hold + leave;
    for (int i = 0; i < total && nseg < NSEG_MAX; i++) {
        float cc;
        if (i < enter) cc = ease_in(0, c, (float)i / (float)enter);
        else if (i < enter + hold) cc = c;
        else cc = ease_out(c, 0, (float)(i - enter - hold) / (float)leave);
        curv[nseg] = cc;
        sflag[nseg] = 0;
        yb[nseg + 1] = ease_io(y0, y0 + dy, (float)(i + 1) / (float)total);
        nseg++;
    }
}

typedef struct { int16_t e, h, l; float c, dy; } sec_t;
static const sec_t SECS[] = {
    {3, 10, 3, 0, 0},              /* largada */
    {20, 60, 20, 0, -2600},        /* primeira descida */
    {25, 50, 25, 3.0f, -2800},     /* curva a direita */
    {15, 40, 15, 0, -1600},
    {25, 60, 25, -4.0f, -3200},    /* curva a esquerda */
    {15, 30, 15, 0, -700},         /* trecho quase plano (sem subida: carrinho nunca empaca) */
    {25, 50, 25, 4.5f, -3000},     /* curva fechada */
    {20, 80, 20, 0, -4200},        /* retao ingreme */
    {25, 40, 25, -3.0f, -2200},
    {20, 60, 20, 2.0f, -2400},
    {25, 70, 25, -5.0f, -3600},    /* curvona a esquerda */
    {15, 50, 15, 0, -2200},
    {25, 40, 25, 3.5f, -2400},     /* S */
    {25, 40, 25, -3.5f, -2400},
    {20, 90, 20, 0, -5000},        /* retao longo */
    {25, 60, 25, 5.0f, -3400},
    {15, 40, 15, 0, -1200},
    {25, 50, 25, -4.5f, -3000},
    {20, 60, 20, 2.5f, -2800},
    {25, 50, 25, -2.5f, -2600},
    {20, 100, 20, 0, -5200},       /* a ladeira mais longa */
    {25, 50, 25, 4.0f, -3000},
    {25, 50, 25, -4.0f, -3000},
    {25, 60, 25, 3.0f, -3000},
    {20, 70, 20, 0, -3600},
    {25, 50, 25, -5.0f, -3000},
    {15, 40, 15, 0, -1500},
    {25, 50, 25, 4.5f, -3200},
    {20, 80, 20, 0, -4400},
    {25, 40, 25, -3.0f, -2500},
    {25, 50, 25, 3.5f, -2800},
    {20, 60, 20, -2.0f, -2600},
    {25, 60, 25, 4.0f, -3200},
    {20, 80, 20, 0, -4000},
    {25, 50, 25, -4.5f, -3000},
    {25, 40, 25, 3.0f, -2400},
    {25, 50, 25, -3.0f, -2800},
    {20, 90, 20, 0, -4600},
    {25, 60, 25, 2.5f, -2800},
    {10, 70, 10, 0, -1800},        /* reta da chegada */
    {5, 140, 5, 0, -300},          /* depois da chegada */
};
/* pista 2: estrada de terra do sitio, bem mais ingreme e com curvas fechadas */
static const sec_t SECS2[] = {
    {3, 10, 3, 0, 0},              /* largada no alto do morro, na casa da vo */
    {15, 40, 15, 0, -3000},        /* despenca */
    {20, 40, 20, -5.0f, -3600},
    {15, 30, 15, 0, -2600},
    {20, 40, 20, 5.5f, -3800},
    {20, 30, 20, -5.5f, -3600},    /* S fechado */
    {15, 60, 15, 0, -5200},
    {25, 40, 25, 4.0f, -3400},
    {15, 40, 15, 0, -2400},
    {20, 50, 20, -6.0f, -4200},    /* grampo */
    {15, 70, 15, 0, -6000},        /* retao ingreme */
    {20, 40, 20, 5.0f, -3600},
    {20, 40, 20, -4.0f, -3200},
    {15, 50, 15, 0, -3600},
    {25, 40, 25, 6.0f, -4000},
    {20, 40, 20, -3.0f, -2800},
    {15, 90, 15, 0, -6400},
    {20, 40, 20, 4.5f, -3400},
    {20, 50, 20, -5.0f, -3800},
    {15, 80, 15, 0, -5600},
    {20, 40, 20, 5.0f, -3400},
    {25, 40, 25, -4.5f, -3400},
    {15, 70, 15, 0, -5400},
    {20, 40, 20, 5.5f, -3800},     /* curva da porteira */
    {20, 40, 20, -5.5f, -3800},
    {15, 50, 15, 0, -3600},
    {25, 50, 25, 4.0f, -3600},
    {15, 100, 15, 0, -7000},       /* o trecho mais ingreme: TACA-LE PAU */
    {20, 40, 20, -6.0f, -4000},    /* grampo de baixo */
    {20, 40, 20, 3.0f, -3000},
    {15, 60, 15, 0, -4200},
    {25, 40, 25, -4.0f, -3400},
    {20, 50, 20, 5.0f, -3800},
    {15, 80, 15, 0, -5600},
    {20, 40, 20, -3.5f, -3000},
    {20, 40, 20, 5.0f, -3600},
    {15, 70, 15, 0, -5200},
    {25, 50, 25, -5.0f, -3800},
    {15, 60, 15, 0, -3400},
    {10, 60, 10, 0, -1600},        /* chegada no pe do morro */
    {5, 140, 5, 0, -300},
};
/* pista 3: Consolacao, avenida larga quase reta, descida constante */
static const sec_t SECS3[] = {
    {3, 10, 3, 0, 0},              /* largada perto da Paulista */
    {20, 120, 20, 0, -2400},
    {30, 80, 30, -1.5f, -2600},
    {20, 140, 20, 0, -3200},
    {30, 90, 30, 1.8f, -3000},
    {20, 160, 20, 0, -3600},       /* reta comprida, predio para todo lado */
    {30, 80, 30, -1.2f, -2400},
    {20, 140, 20, 0, -3200},
    {30, 100, 30, 2.0f, -3200},
    {20, 160, 20, 0, -3600},
    {30, 80, 30, -1.8f, -2800},
    {20, 140, 20, 0, -3000},
    {30, 90, 30, 1.5f, -2800},
    {20, 120, 20, 0, -2600},
    {10, 80, 10, 0, -1400},        /* chegada la embaixo */
    {5, 140, 5, 0, -200},
};
/* pista 4: pista profissional de descida, com chicanes e grampos */
#define CHI(c) {8, 8, 8, (c), -900}
static const sec_t SECS4[] = {
    {3, 10, 3, 0, 0},              /* rampa de largada */
    {20, 100, 20, 0, -3600},
    CHI(5.5f), CHI(-5.5f),         /* chicane 1 */
    {20, 80, 20, 0, -3000},
    {25, 50, 25, 4.0f, -3000},
    {20, 60, 20, 0, -2600},
    CHI(-5.5f), CHI(5.5f), CHI(-5.5f),   /* chicane tripla */
    {20, 120, 20, 0, -4400},
    {30, 40, 30, -6.5f, -3000},    /* grampo */
    {20, 70, 20, 0, -2800},
    {25, 50, 25, 3.5f, -2800},
    CHI(5.5f), CHI(-5.5f),
    {20, 140, 20, 0, -5000},       /* retao */
    {25, 60, 25, -4.5f, -3200},
    {25, 60, 25, 4.5f, -3200},
    CHI(-5.5f), CHI(5.5f),
    {20, 100, 20, 0, -3800},
    {30, 40, 30, 6.5f, -3000},     /* grampo */
    {20, 80, 20, 0, -3000},
    CHI(5.5f), CHI(-5.5f), CHI(5.5f),
    {20, 120, 20, 0, -4200},
    {25, 50, 25, -3.5f, -2800},
    {20, 90, 20, 0, -3400},
    CHI(-5.5f), CHI(5.5f),
    {25, 60, 25, 4.0f, -3200},
    {20, 110, 20, 0, -4000},
    CHI(-5.5f), CHI(5.5f), CHI(-5.5f),
    {20, 100, 20, 0, -3800},
    {30, 40, 30, -6.0f, -3000},    /* ultimo grampo */
    {20, 120, 20, 0, -4400},
    CHI(5.5f), CHI(-5.5f),
    {10, 80, 10, 0, -1600},        /* reta da chegada */
    {5, 140, 5, 0, -300},
};
static int fin_seg;
static float fin_z;

static void build_track(void)
{
    nseg = 0;
    yb[0] = 0;
    const sec_t *sc = trk == 1 ? SECS2 : (trk == 2 ? SECS3 : (trk == 3 ? SECS4 : SECS));
    unsigned n = trk == 1 ? sizeof SECS2 / sizeof SECS2[0] : (trk == 2 ? sizeof SECS3 / sizeof SECS3[0] :
                 (trk == 3 ? sizeof SECS4 / sizeof SECS4[0] : sizeof SECS / sizeof SECS[0]));
    for (unsigned i = 0; i < n; i++) add_road(sc[i].e, sc[i].h, sc[i].l, sc[i].c, sc[i].dy);
    if (trk == 3) {                                       /* pista profissional: sem lombada */
    } else if (trk == 2) {                                /* avenida: sem lombada, com faixas de pedestre */
        for (int i = 120; i < nseg - 200; i += 160) sflag[i] = 4;
    } else
    for (int i = 140; i < nseg - 300; i += trk ? 170 : 290) {          /* lombadas nos trechos retos */
        int j = i;
        while (j < nseg - 300 && fabsf(curv[j]) > 0.01f) j++;
        sflag[j] = 1;
    }
    fin_seg = nseg - 150;
    if (fin_seg < 20) fin_seg = 20;
    fin_z = fin_seg * SEG;
    sflag[START_SEG] = 2;
    sflag[fin_seg] = 3;
    sflag[fin_seg + 1] = 3;
}

static float road_y(float z)
{
    float fs = z / SEG;
    int i = (int)fs;
    if (i < 0) i = 0;
    if (i > nseg - 1) i = nseg - 1;
    float f = clampf(fs - i, 0, 1);
    return yb[i] + (yb[i + 1] - yb[i]) * f;
}
static int seg_of(float z) { int i = (int)(z / SEG); return clampi(i, 0, nseg - 1); }

/* ---- casas ------------------------------------------------------------ */
typedef struct {
    int16_t a, len;
    int8_t side;
    uint8_t brick, tank, floors;
    float u0, u1, h;
    rgb_t c;
    uint32_t seed;
} house_t;
#define HMAX 800
static house_t hs[HMAX];
static int nh;
static uint32_t rng;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t hxy(uint32_t x, uint32_t y)
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static const rgb_t HCOL[7] = {
    {225, 95, 75}, {85, 165, 215}, {240, 200, 85}, {120, 195, 120},
    {215, 120, 175}, {240, 150, 60}, {160, 140, 215},
};

static int cmp_house(const void *a, const void *b) { return ((const house_t *)a)->a - ((const house_t *)b)->a; }
static int lower_house(int a)                  /* primeira casa com inicio >= a */
{
    int lo = 0, hi = nh;
    while (lo < hi) { int m = (lo + hi) / 2; if (hs[m].a < a) lo = m + 1; else hi = m; }
    return lo;
}
static void build_houses(void)
{
    nh = 0;
    rng = 0x2468ACE1u;
    for (int side = -1; side <= 1; side += 2) {
        int s = 2;
        while (s < nseg - 15 && nh < HMAX) {
            if (rnd() % 9 == 0) { s += 4 + (int)(rnd() % 6); continue; }  /* terreno baldio */
            house_t *h = &hs[nh++];
            h->a = (int16_t)s;
            h->len = (int16_t)(10 + rnd() % 6);
            h->side = (int8_t)side;
            uint32_t r = rnd() % 10;
            h->floors = (uint8_t)(r < 4 ? 1 : (r < 8 ? 2 : 3));
            h->h = h->floors * 1400.0f + 250.0f;
            float depth = 2600.0f + (float)(rnd() % 1000);
            float inner = RW * 1.75f;
            if (side > 0) { h->u0 = inner; h->u1 = inner + depth; }
            else { h->u0 = -inner - depth; h->u1 = -inner; }
            h->c = HCOL[rnd() % 7];
            h->brick = (rnd() % 10) < 6;
            h->tank = (rnd() % 10) < 7;
            h->seed = rnd();
            s += h->len + 1;
        }
    }
}

/* ======================================================================
   som: trilha eletronica gerada no codigo + efeitos (rolamentos, lombada,
   batida). PCM 16 kHz no mixer do aparelho (MUSIC e EFFECTS).
   128 BPM, La menor, Am - F - C - G, base de rock industrial (guitarra
   distorcida em palhetada abafada, caixa pesada, bigorna). Partes: intro (4 compassos),
   batida com arpejo (8), break (4, com subida), drop com melodia (8);
   depois volta para a batida.
   ====================================================================== */
#define SR 16000
#define INV_SR (1.0f / 16000.0f)
#define STEP_SMP 1875                  /* uma semicolcheia a 128 BPM */
#define ABLK 256

static float sintab[256];
static uint32_t nz = 0x9E3779B9u;
static inline float noise1(void)
{
    nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5;
    return (float)(int32_t)nz * (1.0f / 2147483648.0f);
}
static inline float sinp(float ph) { return sintab[(int)(ph * 256.0f) & 255]; }
static float mtof(int m) { return 440.0f * expf((m - 69) * 0.05776227f); }   /* sem powf (vira exp2f) */
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }

static const uint8_t CH_ROOT[4] = { 45, 41, 48, 43 };                    /* A2 F2 C3 G2 */
static const uint8_t CH_T[4][3] = { {57, 60, 64}, {53, 57, 60}, {60, 64, 67}, {55, 59, 62} };
static const uint8_t LEAD[4][16] = {                                     /* 0 = nada novo */
    {76, 0, 0, 76, 0, 0, 74, 0, 72, 0, 74, 0, 76, 0, 79, 0},
    {77, 0, 0, 76, 0, 0, 72, 0, 69, 0, 0, 0, 72, 0, 74, 0},
    {76, 0, 0, 79, 0, 0, 76, 0, 72, 0, 76, 0, 79, 0, 84, 0},
    {83, 0, 0, 79, 0, 0, 74, 0, 79, 0, 81, 0, 83, 0, 86, 0},
};

typedef struct {
    uint32_t t;
    float k_ph, k_env, k_pe;
    float c_env, h_env, h_dec, n_prev;
    float b_ph, b_f, b_env, b_lp;
    float a_ph, a_f, a_env;
    float l_ph1, l_ph2, l_f, l_env, l_lp;
    float p_ph[3], p_f[3], p_lvl, p_tgt;
    float riser;
    float g_ph[3], g_f, g_env, g_dec, g_lp, g_cut;       /* guitarra distorcida */
    float m_env, m_ph1, m_ph2;                           /* metal (bigorna) */
    float s_env, s_ph;                                   /* corpo da caixa */
} synth_t;
static synth_t sy;

static int song_bar(int bar) { return bar < 24 ? bar : 4 + (bar - 4) % 20; }
/* guitarra: 2 = palhetada aberta com acento, 1 = abafada (chug), 0 = pausa */
static const uint8_t RIFF[16] = { 2, 1, 1, 0, 1, 1, 2, 0, 1, 0, 1, 1, 2, 0, 1, 1 };

static void synth_step(int step)
{
    int s = step % 16, b = song_bar(step / 16), ch = b % 4;
    bool intro = b < 4, brk = b >= 12 && b < 16, drop = b >= 16, main_ = !intro && !brk && !drop;
    if (!brk && (s % 4) == 0) { sy.k_env = 1; sy.k_pe = 1; sy.k_ph = 0; }               /* bumbo */
    if ((main_ || drop) && (s == 4 || s == 12)) { sy.c_env = 1; sy.s_env = 1; sy.s_ph = 0; } /* caixa + palma */
    if ((main_ || drop) && (s == 6 || s == 14)) { sy.m_env = s == 14 ? 1.0f : 0.6f; }      /* bigorna */
    if (!brk && RIFF[s]) {                                                                 /* guitarra */
        bool open = RIFF[s] == 2;
        sy.g_f = mtof(CH_ROOT[ch]);
        sy.g_env = intro ? (open ? 0.7f : 0.5f) : 1.0f;
        sy.g_dec = open ? 0.99985f : 0.9993f;
        sy.g_cut = open ? 0.32f : 0.12f;
    }
    if (b == 15 && (s % 2 == 0 || s >= 8)) sy.c_env = 0.3f + 0.04f * s;                /* virada */
    if (!brk) {                                                                         /* chimbal */
        if (s % 4 == 2) { sy.h_env = 1; sy.h_dec = 0.9990f; }
        else if ((main_ || drop) && (s & 1)) { sy.h_env = 0.4f; sy.h_dec = 0.9965f; }
    }
    if (!brk && (s % 4 == 2 || s == 7 || s == 15)) {                                    /* baixo */
        sy.b_f = mtof(CH_ROOT[ch] + ((s == 7 || s == 15) ? 12 : 0));
        sy.b_env = 1;
    }
    if (!intro) {                                                                       /* arpejo */
        static const int ord[4] = { 0, 1, 2, 1 };
        sy.a_f = mtof(CH_T[ch][ord[s % 4]] + 12 + (((s / 4) & 1) ? 12 : 0));
        sy.a_env = 1;
    }
    if (s == 0) for (int i = 0; i < 3; i++) sy.p_f[i] = mtof(CH_T[ch][i]);               /* acorde */
    sy.p_tgt = brk ? 1.0f : (drop ? 0.55f : (main_ ? 0.3f : 0.0f));
    if (drop && LEAD[ch][s]) { sy.l_f = mtof(LEAD[ch][s]); sy.l_env = 1; }              /* melodia */
    sy.riser = b == 15 ? s / 16.0f : 0.0f;
}

static void synth_render(int16_t *out, int n)
{
    for (int i = 0; i < n; i++, sy.t++) {
        if (sy.t % STEP_SMP == 0) synth_step((int)(sy.t / STEP_SMP));
        float o = 0, nn = noise1(), hp = nn - sy.n_prev;
        sy.n_prev = nn;
        /* bumbo: seno caindo de 150 para 45 Hz */
        if (sy.k_env > 0.001f) {
            sy.k_ph = wrap1(sy.k_ph + (45.0f + 105.0f * sy.k_pe) * INV_SR);
            o += clampf(sinp(sy.k_ph) * sy.k_env * 1.6f, -1, 1) * 0.85f;   /* bumbo com drive */
            sy.k_env *= 0.99944f; sy.k_pe *= 0.99813f;
        }
        float duck = 1.0f - 0.7f * sy.k_env;                    /* "bombeado" do bumbo */
        if (sy.c_env > 0.001f) { o += hp * sy.c_env * 0.32f; sy.c_env *= 0.99948f; }
        if (sy.h_env > 0.001f) { o += hp * sy.h_env * 0.10f; sy.h_env *= sy.h_dec; }
        if (sy.b_env > 0.001f) {
            sy.b_ph = wrap1(sy.b_ph + sy.b_f * INV_SR);
            sy.b_lp += ((2 * sy.b_ph - 1) - sy.b_lp) * 0.22f;
            o += sy.b_lp * sy.b_env * 0.42f * duck;
            sy.b_env *= 0.99975f;
        }
        if (sy.a_env > 0.001f) {
            sy.a_ph = wrap1(sy.a_ph + sy.a_f * INV_SR);
            o += (sy.a_ph < 0.25f ? 1.0f : -1.0f) * sy.a_env * 0.05f;
            sy.a_env *= 0.99925f;
        }
        if (sy.l_env > 0.001f) {
            sy.l_ph1 = wrap1(sy.l_ph1 + sy.l_f * INV_SR);
            sy.l_ph2 = wrap1(sy.l_ph2 + sy.l_f * 1.007f * INV_SR);
            float x = sy.l_ph1 + sy.l_ph2 - 1.0f;
            sy.l_lp += (x - sy.l_lp) * 0.35f;
            o += sy.l_lp * sy.l_env * 0.17f;
            sy.l_env *= 0.99990f;
        }
        if (sy.s_env > 0.001f) {                                  /* corpo da caixa, 190 Hz */
            sy.s_ph = wrap1(sy.s_ph + 190.0f * INV_SR);
            o += sinp(sy.s_ph) * sy.s_env * 0.35f;
            sy.s_env *= 0.9990f;
        }
        if (sy.m_env > 0.001f) {                                  /* metal: dois quadrados multiplicados */
            sy.m_ph1 = wrap1(sy.m_ph1 + 587.0f * INV_SR);
            sy.m_ph2 = wrap1(sy.m_ph2 + 823.0f * INV_SR);
            float q = (sy.m_ph1 < 0.5f ? 1.0f : -1.0f) * (sy.m_ph2 < 0.5f ? 1.0f : -1.0f);
            o += (q * 0.8f + hp * 0.3f) * sy.m_env * 0.16f;
            sy.m_env *= 0.99960f;
        }
        if (sy.g_env > 0.001f) {                                  /* guitarra: quinta + oitava, saturada */
            sy.g_ph[0] = wrap1(sy.g_ph[0] + sy.g_f * INV_SR);
            sy.g_ph[1] = wrap1(sy.g_ph[1] + sy.g_f * 1.0055f * INV_SR);
            sy.g_ph[2] = wrap1(sy.g_ph[2] + sy.g_f * 1.4983f * INV_SR);
            float x = (2 * sy.g_ph[0] - 1) + (2 * sy.g_ph[1] - 1) + 0.8f * (2 * sy.g_ph[2] - 1);
            x = clampf(x * sy.g_env * 3.5f, -1, 1);
            sy.g_lp += (x - sy.g_lp) * sy.g_cut;
            o += sy.g_lp * 0.30f * duck;
            sy.g_env *= sy.g_dec;
        }
        sy.p_lvl += (sy.p_tgt - sy.p_lvl) * 0.0004f;
        if (sy.p_lvl > 0.002f) {
            float pad = 0;
            for (int v = 0; v < 3; v++) {
                sy.p_ph[v] = wrap1(sy.p_ph[v] + sy.p_f[v] * INV_SR);
                float ph = sy.p_ph[v];
                pad += ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
            }
            o += pad * sy.p_lvl * 0.10f * duck;
        }
        if (sy.riser > 0) o += hp * sy.riser * 0.12f;
        o = o * (27 + o * o) / (27 + 9 * o * o);                 /* saturacao suave */
        out[i] = (int16_t)(clampf(o * 0.85f, -1, 1) * 26000.0f);
    }
}

/* efeitos: ronco dos rolamentos (cresce com a velocidade), lombada, batida */
static float fx_roll, fx_roll_tgt, fx_rough, fx_lp, fx_hum, fx_b_env, fx_b_ph, fx_b_pe, fx_h_env;
static void sfx_bump(void) { fx_b_env = 1; fx_b_pe = 1; }
static void sfx_hit(void) { fx_h_env = 1; }
static void sfx_render(int16_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        fx_roll += (fx_roll_tgt - fx_roll) * 0.002f;
        float nn = noise1(), o = 0;
        fx_lp += (nn - fx_lp) * (0.06f + 0.25f * fx_rough);
        o += fx_lp * fx_roll * (0.7f + 0.8f * fx_rough);
        fx_hum = wrap1(fx_hum + (35.0f + 90.0f * fx_roll) * INV_SR);
        o += (2 * fx_hum - 1) * fx_roll * 0.10f;
        if (fx_b_env > 0.001f) {
            fx_b_ph = wrap1(fx_b_ph + (40.0f + 90.0f * fx_b_pe) * INV_SR);
            o += sinp(fx_b_ph) * fx_b_env * 0.9f;
            fx_b_env *= 0.99955f; fx_b_pe *= 0.9985f;
        }
        if (fx_h_env > 0.001f) { o += nn * fx_h_env * 0.6f; fx_h_env *= 0.9993f; }
        out[i] = (int16_t)(clampf(o, -1, 1) * 24000.0f);
    }
}

/* ---- trilha da pista 2: bluegrass com banjo ---------------------------
   140 BPM, Sol maior. Banjo em "forward roll" (cordas pinçadas por
   Karplus-Strong, com a 5a corda solta em Sol), baixo acústico no
   1 e no 3, violão abafado no 2 e no 4 ("chuck") e caixa de vassourinha.
   Compassos 1-8 so a base; 9-16 entra uma rabeca (violino) com a melodia.
   Acordes: G C G D G Em C D.
   ---------------------------------------------------------------------- */
#define BSTEP 1714                     /* semicolcheia a 140 BPM */
#define KS_LEN 420
typedef struct { float buf[KS_LEN]; int len, pos; float damp, amp; } ks_t;
typedef struct {
    uint32_t t;
    ks_t bj[3], bass, gt[3];
    int bj_next;
    float sn_env;
    float fd_ph1, fd_ph2, fd_f, fd_env, fd_vib, fd_lp, fd_tgt;
} banjo_t;
static banjo_t bjs;

static void ks_pluck(ks_t *k, float hz, float amp, float damp, float bright)
{
    int n = (int)(SR / hz + 0.5f);
    if (n < 4) n = 4;
    if (n > KS_LEN) n = KS_LEN;
    k->len = n; k->pos = 0; k->damp = damp; k->amp = amp;
    float lp = 0;
    for (int i = 0; i < n; i++) {                 /* ruido filtrado: mais brilho = mais agudo */
        float x = noise1();
        lp += (x - lp) * bright;
        k->buf[i] = lp;
    }
}
static inline float ks_tick(ks_t *k)
{
    if (k->len == 0) return 0;
    int p = k->pos, q = p + 1 == k->len ? 0 : p + 1;
    float out = k->buf[p];
    k->buf[p] = (k->buf[p] + k->buf[q]) * 0.5f * k->damp;
    k->pos = q;
    return out * k->amp;
}

static const uint8_t BJ_CH[8][3] = {       /* tons do acorde em cada compasso */
    {55, 59, 62}, {60, 64, 67}, {55, 59, 62}, {62, 66, 69},
    {55, 59, 62}, {64, 67, 71}, {60, 64, 67}, {62, 66, 69},
};
static const uint8_t BJ_BASS[8] = { 43, 48, 43, 38, 43, 40, 48, 38 };
static const uint8_t FIDDLE[8][8] = {      /* colcheias */
    {71, 74, 76, 74, 71, 69, 67, 69}, {72, 76, 79, 76, 72, 71, 69, 67},
    {71, 67, 71, 74, 79, 76, 74, 71}, {69, 74, 78, 74, 69, 66, 62, 66},
    {74, 76, 79, 81, 79, 76, 74, 71}, {76, 79, 83, 79, 76, 71, 67, 71},
    {72, 76, 79, 84, 79, 76, 72, 76}, {74, 78, 81, 78, 74, 69, 66, 62},
};

static void banjo_step(int step)
{
    int s = step % 16, bar = (step / 16) % 16, b = bar % 8;
    bool fiddle = bar >= 8;
    const uint8_t *c = BJ_CH[b];
    /* forward roll: polegar, indicador, medio... e a 5a corda (Sol 67) */
    static const int8_t ROLLPAT[8] = { 0, 1, 2, 3, 1, 2, 0, 3 };
    int r = ROLLPAT[s % 8];
    int note = r == 3 ? 67 : c[r] + ((s % 8) >= 4 && r == 2 ? 12 : 0);
    ks_pluck(&bjs.bj[bjs.bj_next], mtof(note), (s % 4 == 0) ? 1.0f : 0.75f, 0.9965f, 0.85f);
    bjs.bj_next = (bjs.bj_next + 1) % 3;
    if (s == 0 || s == 8) ks_pluck(&bjs.bass, mtof(BJ_BASS[b] + (s == 8 ? 7 : 0)), 1.0f, 0.9992f, 0.25f);
    if (s == 14 && (bar & 1)) ks_pluck(&bjs.bass, mtof(BJ_BASS[(b + 1) % 8] - 1), 0.7f, 0.999f, 0.25f);  /* passagem */
    if (s == 4 || s == 12) {                                   /* violao abafado + vassourinha */
        for (int i = 0; i < 3; i++) ks_pluck(&bjs.gt[i], mtof(c[i] - 12), 0.6f, 0.975f, 0.6f);
        bjs.sn_env = 1;
    }
    if (s % 2 == 1) bjs.sn_env = bjs.sn_env < 0.3f ? 0.3f : bjs.sn_env;   /* tchic-tchic do trem */
    if (fiddle && s % 2 == 0) { bjs.fd_f = mtof(FIDDLE[b][s / 2]); bjs.fd_env = bjs.fd_env < 0.4f ? 0.4f : bjs.fd_env; }
    bjs.fd_tgt = fiddle ? 1.0f : 0.0f;
}

static void banjo_render(int16_t *out, int n)
{
    for (int i = 0; i < n; i++, bjs.t++) {
        if (bjs.t % BSTEP == 0) banjo_step((int)(bjs.t / BSTEP));
        float o = 0;
        for (int v = 0; v < 3; v++) o += ks_tick(&bjs.bj[v]) * 0.42f;
        o += ks_tick(&bjs.bass) * 0.75f;
        for (int v = 0; v < 3; v++) o += ks_tick(&bjs.gt[v]) * 0.16f;
        float nn = noise1(), hp = nn - sy.n_prev;
        sy.n_prev = nn;
        if (bjs.sn_env > 0.001f) { o += hp * bjs.sn_env * 0.09f; bjs.sn_env *= 0.9985f; }
        /* rabeca: duas serras com vibrato, ataque suave */
        bjs.fd_env += (bjs.fd_tgt - bjs.fd_env) * 0.0015f;
        if (bjs.fd_env > 0.002f && bjs.fd_f > 0) {
            bjs.fd_vib = wrap1(bjs.fd_vib + 5.5f * INV_SR);
            float f = bjs.fd_f * (1.0f + 0.006f * sinp(bjs.fd_vib));
            bjs.fd_ph1 = wrap1(bjs.fd_ph1 + f * INV_SR);
            bjs.fd_ph2 = wrap1(bjs.fd_ph2 + f * 1.004f * INV_SR);
            float x = (bjs.fd_ph1 + bjs.fd_ph2 - 1.0f);
            bjs.fd_lp += (x - bjs.fd_lp) * 0.3f;
            o += bjs.fd_lp * bjs.fd_env * 0.20f;
        }
        o = clampf(o * 1.8f, -3, 3);
        o = o * (27 + o * o) / (27 + 9 * o * o);
        out[i] = (int16_t)(clampf(o * 0.9f, -1, 1) * 26000.0f);
    }
}

/* ---- trilha da pista 3: punk rock / hardcore ---------------------------
   180 BPM, Mi menor. Guitarra distorcida em palhetada para baixo (power
   chords), baixo dobrando a tonica, bateria de skate punk na parte A e
   d-beat no refrao, virada de caixa no fim; no refrao entra uma guitarra
   solo. Parte A: E E C D (abafada); refrao: G D E C (aberta).
   ---------------------------------------------------------------------- */
#define PSTEP 1333                     /* semicolcheia a 180 BPM */
typedef struct {
    uint32_t t;
    float k_ph, k_env, k_pe, s_env, s_ph, h_env, c_env, n_prev;
    float g_ph[3], g_f, g_env, g_dec, g_lp, g_cut;
    float b_ph, b_f, b_env, b_lp;
    float l_ph, l_f, l_env, l_lp, l_vib;
} punk_t;
static punk_t pks;
static const uint8_t PK_A[4] = { 40, 40, 36, 38 }, PK_B[4] = { 43, 38, 40, 36 };
static const uint8_t PK_LEAD[4][4] = { {67, 71, 74, 71}, {66, 69, 74, 69}, {64, 67, 71, 67}, {64, 67, 72, 76} };

static void punk_step(int step)
{
    int s = step % 16, bar = step / 16;
    int b = bar < 2 ? -1 : (bar - 2) % 16;            /* -1 = introducao */
    bool chorus = b >= 8, intro = b < 0;
    int root = intro ? 40 : (chorus ? PK_B[b % 4] : PK_A[b % 4]);
    bool fill = b == 15 && s >= 8;
    if (s % 2 == 0 && !fill) {                         /* guitarra: colcheias para baixo */
        pks.g_f = mtof(root + 12);
        pks.g_env = 1;
        pks.g_dec = chorus || intro ? 0.99993f : 0.9993f;
        pks.g_cut = chorus || intro ? 0.38f : 0.14f;
        pks.b_f = mtof(root); pks.b_env = 1;
    }
    if (intro) { if (s == 0 && (bar & 1)) pks.c_env = 1; return; }
    if (fill) { pks.s_env = 0.5f + s * 0.03f; pks.s_ph = 0; return; }   /* virada */
    bool kick, snare;
    if (!chorus) { kick = s == 0 || s == 8 || s == 10; snare = s == 4 || s == 12; }      /* skate punk */
    else { kick = s % 2 == 0 && s != 4 && s != 12; snare = s == 4 || s == 12; }          /* d-beat */
    if (kick) { pks.k_env = 1; pks.k_pe = 1; pks.k_ph = 0; }
    if (snare) { pks.s_env = 1; pks.s_ph = 0; }
    if (s % 2 == 0) pks.h_env = chorus ? 0.5f : 0.8f;
    if (s == 0 && (chorus || b == 0)) pks.c_env = 1;
    if (chorus && s % 4 == 0) { pks.l_f = mtof(PK_LEAD[b % 4][s / 4]); pks.l_env = 1; }
}

static void punk_render(int16_t *out, int n)
{
    for (int i = 0; i < n; i++, pks.t++) {
        if (pks.t % PSTEP == 0) punk_step((int)(pks.t / PSTEP));
        float o = 0, nn = noise1(), hp = nn - pks.n_prev;
        pks.n_prev = nn;
        if (pks.k_env > 0.001f) {
            pks.k_ph = wrap1(pks.k_ph + (50.0f + 110.0f * pks.k_pe) * INV_SR);
            o += clampf(sinp(pks.k_ph) * pks.k_env * 1.8f, -1, 1) * 0.8f;
            pks.k_env *= 0.99935f; pks.k_pe *= 0.9975f;
        }
        if (pks.s_env > 0.001f) {
            pks.s_ph = wrap1(pks.s_ph + 200.0f * INV_SR);
            o += (hp * 0.55f + sinp(pks.s_ph) * 0.35f) * pks.s_env * 0.55f;
            pks.s_env *= 0.99915f;
        }
        if (pks.h_env > 0.001f) { o += hp * pks.h_env * 0.07f; pks.h_env *= 0.9975f; }
        if (pks.c_env > 0.001f) { o += hp * pks.c_env * 0.14f; pks.c_env *= 0.99985f; }
        if (pks.g_env > 0.001f) {
            pks.g_ph[0] = wrap1(pks.g_ph[0] + pks.g_f * INV_SR);
            pks.g_ph[1] = wrap1(pks.g_ph[1] + pks.g_f * 1.4983f * INV_SR);
            pks.g_ph[2] = wrap1(pks.g_ph[2] + pks.g_f * 2.006f * INV_SR);
            float x = (2 * pks.g_ph[0] - 1) + 0.9f * (2 * pks.g_ph[1] - 1) + 0.6f * (2 * pks.g_ph[2] - 1);
            x = clampf(x * pks.g_env * 4.5f, -1, 1);
            pks.g_lp += (x - pks.g_lp) * pks.g_cut;
            o += pks.g_lp * 0.30f;
            pks.g_env *= pks.g_dec;
        }
        if (pks.b_env > 0.001f) {
            pks.b_ph = wrap1(pks.b_ph + pks.b_f * INV_SR);
            pks.b_lp += ((2 * pks.b_ph - 1) - pks.b_lp) * 0.18f;
            o += pks.b_lp * pks.b_env * 0.40f;
            pks.b_env *= 0.99985f;
        }
        if (pks.l_env > 0.001f) {                         /* solo: distorcido, com vibrato */
            pks.l_vib = wrap1(pks.l_vib + 6.0f * INV_SR);
            pks.l_ph = wrap1(pks.l_ph + pks.l_f * (1.0f + 0.008f * sinp(pks.l_vib)) * INV_SR);
            float x = clampf((2 * pks.l_ph - 1) * 3.0f, -1, 1);
            pks.l_lp += (x - pks.l_lp) * 0.25f;
            o += pks.l_lp * pks.l_env * 0.12f;
            pks.l_env *= 0.99993f;
        }
        o = clampf(o * 0.85f, -3, 3);
        o = o * (27 + o * o) / (27 + 9 * o * o);
        out[i] = (int16_t)(clampf(o * 0.9f, -1, 1) * 26000.0f);
    }
}

/* ---- trilha da pista 4: surf rock -------------------------------------
   160 BPM, La menor, Am G F E (a descida andaluza, cara de surf music).
   Guitarra solo em palhetada tremolo (corda pinçada a cada semicolcheia,
   com eco de mola), guitarra base dando "chank" no contratempo, baixo em
   colcheias, bateria com prato de conducao e virada de tons; na parte B
   entra um orgao. Partes: intro 4 compassos, A 8, B 4; volta para a A.
   ---------------------------------------------------------------------- */
#define SSTEP 1500                     /* semicolcheia a 160 BPM */
#define ECHO_N 2300
typedef struct {
    uint32_t t;
    ks_t ld[2], rh[3];
    int ld_next;
    float echo[ECHO_N];
    int echo_pos;
    float k_ph, k_env, k_pe, s_env, s_ph, r_env, tom_env, tom_ph, tom_f, n_prev;
    float b_ph, b_f, b_env, b_lp;
    float o_ph[3], o_f[3], o_lvl, o_tgt;
} surf_t;
static surf_t sfs;
static const uint8_t SF_CH[4][3] = { {57, 60, 64}, {55, 59, 62}, {53, 57, 60}, {52, 56, 59} };   /* Am G F E */
static const uint8_t SF_BASS[4] = { 45, 43, 41, 40 };
static const uint8_t SF_A[4][4] = { {76, 76, 77, 76}, {74, 74, 71, 74}, {72, 72, 69, 72}, {71, 68, 71, 76} };
static const uint8_t SF_B[4][4] = { {81, 79, 77, 76}, {79, 77, 74, 71}, {77, 76, 72, 69}, {68, 71, 74, 76} };

static void surf_step(int step)
{
    int s = step % 16, bar = step / 16;
    int b = bar < 4 ? -1 : (bar - 4) % 12;            /* -1 introducao; 0-7 parte A; 8-11 parte B */
    int ch = (b < 0 ? bar : b) % 4;
    bool partB = b >= 8, intro = b < 0, fill = (b == 7 || b == 11) && s >= 12;
    /* bateria: bumbo no 1 e no 3, caixa no 2 e no 4, conducao em colcheias */
    if (!fill) {
        if (s == 0 || s == 8 || (s == 10 && !intro)) { sfs.k_env = 1; sfs.k_pe = 1; sfs.k_ph = 0; }
        if (s == 4 || s == 12) { sfs.s_env = 1; sfs.s_ph = 0; }
        if (s % 2 == 0) sfs.r_env = s % 4 == 0 ? 0.8f : 0.5f;
    } else {                                           /* virada de tons descendo */
        sfs.tom_env = 1; sfs.tom_ph = 0; sfs.tom_f = 190.0f - (s - 12) * 25.0f;
    }
    /* baixo: tonica e quinta em colcheias, oitava no fim do compasso */
    if (s % 2 == 0) {
        int n = SF_BASS[ch] + ((s / 2) % 4 == 2 ? 7 : 0) + (s == 14 ? 12 : 0);
        sfs.b_f = mtof(n); sfs.b_env = 1;
    }
    /* base: "chank" do acorde no contratempo */
    if (s == 2 || s == 6 || s == 10 || s == 14)
        for (int i = 0; i < 3; i++) ks_pluck(&sfs.rh[i], mtof(SF_CH[ch][i]), 0.55f, 0.985f, 0.7f);
    /* solo: uma nota por tempo, palhetada tremolo nas 4 semicolcheias */
    if (!intro) {
        int note = partB ? SF_B[ch][s / 4] : SF_A[ch][s / 4];
        ks_pluck(&sfs.ld[sfs.ld_next], mtof(note), (s % 4 == 0) ? 1.0f : 0.7f, 0.9975f, 0.9f);
        sfs.ld_next ^= 1;
    }
    if (s == 0) for (int i = 0; i < 3; i++) sfs.o_f[i] = mtof(SF_CH[ch][i] + 12);
    sfs.o_tgt = partB ? 1.0f : 0.0f;
}

static void surf_render(int16_t *out, int n)
{
    for (int i = 0; i < n; i++, sfs.t++) {
        if (sfs.t % SSTEP == 0) surf_step((int)(sfs.t / SSTEP));
        float o = 0, nn = noise1(), hp = nn - sfs.n_prev;
        sfs.n_prev = nn;
        if (sfs.k_env > 0.001f) {
            sfs.k_ph = wrap1(sfs.k_ph + (48.0f + 100.0f * sfs.k_pe) * INV_SR);
            o += sinp(sfs.k_ph) * sfs.k_env * 0.85f;
            sfs.k_env *= 0.99930f; sfs.k_pe *= 0.9978f;
        }
        if (sfs.s_env > 0.001f) {
            sfs.s_ph = wrap1(sfs.s_ph + 210.0f * INV_SR);
            o += (hp * 0.5f + sinp(sfs.s_ph) * 0.3f) * sfs.s_env * 0.45f;
            sfs.s_env *= 0.99925f;
        }
        if (sfs.r_env > 0.001f) { o += hp * sfs.r_env * 0.05f; sfs.r_env *= 0.99965f; }   /* prato de conducao */
        if (sfs.tom_env > 0.001f) {
            sfs.tom_ph = wrap1(sfs.tom_ph + sfs.tom_f * INV_SR);
            o += sinp(sfs.tom_ph) * sfs.tom_env * 0.5f;
            sfs.tom_env *= 0.99945f;
        }
        if (sfs.b_env > 0.001f) {
            sfs.b_ph = wrap1(sfs.b_ph + sfs.b_f * INV_SR);
            sfs.b_lp += ((2 * sfs.b_ph - 1) - sfs.b_lp) * 0.12f;
            o += sfs.b_lp * sfs.b_env * 0.45f;
            sfs.b_env *= 0.99965f;
        }
        for (int v = 0; v < 3; v++) o += ks_tick(&sfs.rh[v]) * 0.13f;
        /* solo com eco de mola (reverb de surf) */
        float ld = ks_tick(&sfs.ld[0]) + ks_tick(&sfs.ld[1]);
        float e = sfs.echo[sfs.echo_pos];
        sfs.echo[sfs.echo_pos] = ld + e * 0.42f;
        sfs.echo_pos = sfs.echo_pos + 1 == ECHO_N ? 0 : sfs.echo_pos + 1;
        o += (ld + e * 0.45f) * 0.36f;
        /* orgao na parte B */
        sfs.o_lvl += (sfs.o_tgt - sfs.o_lvl) * 0.0008f;
        if (sfs.o_lvl > 0.002f) {
            float org = 0;
            for (int v = 0; v < 3; v++) {
                sfs.o_ph[v] = wrap1(sfs.o_ph[v] + sfs.o_f[v] * INV_SR);
                org += sfs.o_ph[v] < 0.5f ? 1.0f : -1.0f;
            }
            o += org * sfs.o_lvl * 0.035f;
        }
        o = clampf(o * 1.3f, -3, 3);
        o = o * (27 + o * o) / (27 + 9 * o * o);
        out[i] = (int16_t)(clampf(o * 0.9f, -1, 1) * 26000.0f);
    }
}

typedef struct { int16_t buf[ABLK]; int off, cnt; } pump_t;
static pump_t pm_mus, pm_fx;
static bool sound_on = true, audio_ok = true;

static void pump(int channel, pump_t *p, void (*gen)(int16_t *, int))
{
    for (int guard = 0; guard < 8 && audio_ok; guard++) {
        if (p->cnt == 0) { gen(p->buf, ABLK); p->off = 0; p->cnt = ABLK; }
        int w = lap_mixer_write(channel, p->buf + p->off, p->cnt);
        if (w < 0) { audio_ok = false; break; }   /* sem alto-falante/mixer: segue sem som */
        if (w == 0) break;                       /* fila cheia: completa no proximo quadro */
        p->off += w; p->cnt -= w;
    }
}
static void audio_init(void)
{
    for (int i = 0; i < 256; i++) sintab[i] = sinf(i * (2.0f * PI_F / 256.0f));
    memset(&sy, 0, sizeof sy);
    memset(&bjs, 0, sizeof bjs);
    memset(&pks, 0, sizeof pks);
    memset(&sfs, 0, sizeof sfs);
    memset(&pm_mus, 0, sizeof pm_mus);
    memset(&pm_fx, 0, sizeof pm_fx);
    audio_ok = true;
    lap_mixer_config(LAP_MIX_MUSIC, 70, 0);
    lap_mixer_config(LAP_MIX_EFFECTS, 90, 0);
}
static void audio_frame(void)
{
    if (!sound_on) return;
    pump(LAP_MIX_MUSIC, &pm_mus, trk == 1 ? banjo_render : (trk == 2 ? punk_render : (trk == 3 ? surf_render : synth_render)));
    pump(LAP_MIX_EFFECTS, &pm_fx, sfx_render);
}
static void audio_toggle(void)
{
    sound_on = !sound_on;
    if (!sound_on) { lap_mixer_clear(LAP_MIX_MUSIC); lap_mixer_clear(LAP_MIX_EFFECTS); }
}

/* ======================================================================
   corredores
   ====================================================================== */
typedef struct {
    float z, x, v, drag, tx, hy, jv;
    rgb_t shirt, helm;
    uint32_t fin_ms;
    char num;
    uint8_t ch;                 /* qual piloto */
} racer_t;
#define NRIV 3
static racer_t pl, rv[NRIV];
enum { ST_COUNT, ST_RACE, ST_FINISH, ST_MENU, ST_CHAR, ST_PODIUM };
/* dificuldade, recordes, campeonato, turbo de vacuo, fantasma, bichos */
static int diff = 1;                               /* 0 facil, 1 normal, 2 dificil */
static const float DIFF_MUL[3] = { 1.07f, 1.0f, 0.94f };
static const char *const DIFF_TXT[3] = { "FACIL", "NORMAL", "DIFICIL" };
static uint32_t best_ms[4];
static bool new_rec;
static bool champ_on, champ_awarded;
static int champ_idx, champ_pts[NCHAR];
static const int CHAMP_PTS[4] = { 10, 6, 3, 1 };
static float vac, turbo_t;
typedef struct { float z; int16_t x, hy; } gsamp_t;
#define GMAX 1600
#define G_DT 0.1f
static gsamp_t g_rec[GMAX];
static struct { int32_t n; gsamp_t s[GMAX]; } g_best;
static int g_rec_n;
static float g_acc;
typedef struct { float z, x, dir, sp; uint8_t type, active, hit; } critter_t;
#define NCRIT 3
static critter_t crit[NCRIT];
static float crit_timer, g_now_s;
typedef struct { uint32_t magic; uint32_t best[4]; int32_t diff; } save_t;
#define SAVE_MAGIC 0x524F4C31u                      /* "ROL1" */
static void save_records(void)
{
    save_t sv = { SAVE_MAGIC, { best_ms[0], best_ms[1], best_ms[2], best_ms[3] }, diff };
    lap_save("recordes", &sv, sizeof sv);
}
static void load_records(void)
{
    save_t sv;
    if (lap_load("recordes", &sv, sizeof sv) == (int)sizeof sv && sv.magic == SAVE_MAGIC) {
        for (int i = 0; i < 4; i++) best_ms[i] = sv.best[i];
        diff = clampi(sv.diff, 0, 2);
    }
}
static void ghost_slot(char *b, int t) { b[0] = 'f'; b[1] = 'a'; b[2] = 'n'; b[3] = (char)('0' + t); b[4] = 0; }
static void load_ghost(int t)
{
    char nm[8];
    ghost_slot(nm, t);
    int r = lap_load(nm, &g_best, sizeof g_best);
    if (r < (int)sizeof(int32_t) || g_best.n < 0 || g_best.n > GMAX) g_best.n = 0;
}
static void save_ghost(int t)
{
    char nm[8];
    ghost_slot(nm, t);
    g_best.n = g_rec_n;
    memcpy(g_best.s, g_rec, sizeof(gsamp_t) * (size_t)g_rec_n);
    lap_save(nm, &g_best, sizeof(int32_t) + sizeof(gsamp_t) * (size_t)g_rec_n);
}
static int state;
static uint32_t t_state, t_race, t_last, fin_time;
static int fin_place, last_beep, last_bump_seg;
static float steer, hit_cd, crash_cd;
static int fin_fx;                 /* efeitos do final ja disparados */
static bool brake;

static void cars_init(void);
static void cars_update(float dt);
static void critters_update(float dt);
static void restart(void)
{
    memset(&pl, 0, sizeof pl);
    pl.z = 2000; pl.x = 0; pl.drag = 1.0f; pl.num = '7';
    crash_cd = 0;
    pl.shirt = (rgb_t){235, 55, 70}; pl.helm = (rgb_t){255, 215, 50};
    static const float zs[NRIV] = { 2000, 2000, 2400 }, xs0[NRIV] = { -0.6f, 0.6f, 0.0f }, xs3[NRIV] = { 0.15f, 0.85f, 0.5f };
    const float *xs = trk == 2 ? xs3 : xs0;
    if (trk == 2) { pl.x = 0.5f; cars_init(); }
    static const float dr[NRIV] = { 1.10f, 1.07f, 1.13f };
    static const rgb_t sh[NRIV] = { {30, 170, 90}, {40, 110, 240}, {150, 70, 200} };
    static const rgb_t he[NRIV] = { {255, 230, 60}, {250, 250, 250}, {255, 150, 40} };
    static const char nu[NRIV] = { '3', '5', '9' };
    for (int i = 0; i < NRIV; i++) {
        memset(&rv[i], 0, sizeof rv[i]);
        rv[i].z = zs[i]; rv[i].x = xs[i]; rv[i].tx = xs[i]; rv[i].drag = dr[i];
        rv[i].shirt = sh[i]; rv[i].helm = he[i]; rv[i].num = nu[i];
        rv[i].ch = (uint8_t)((my_char + 1 + i) % NCHAR);   /* os outros tres pilotos */
    }
    pl.ch = (uint8_t)my_char;
    vac = 0; turbo_t = 0; g_rec_n = 0; g_acc = 0; new_rec = false; champ_awarded = false;
    memset(crit, 0, sizeof crit);
    crit_timer = 4.0f;
    state = ST_COUNT;
    t_state = lap_millis();
    shout_on = false;
    last_shout = 0;
    last_beep = -1;
    last_bump_seg = -1;
    hit_cd = 0;
}

/* gravidade da ladeira, arrasto do ar e atrito dos rolamentos */
static void phys(racer_t *r, float drag, float extra, float dt)
{
    int i = seg_of(r->z);
    float grade = (yb[i] - yb[i + 1]) / SEG;
    float acc = GRAV * grade - drag * DRAG * r->v * r->v - ROLL - extra;
    r->v += acc * dt;
    if (r->v < 0) r->v = 0;
    float vmax = (r == &pl && turbo_t > 0) ? VMAX * 1.15f : VMAX;
    if (r->v > vmax) r->v = vmax;
    r->z += r->v * dt;
    float zmax = (nseg - 3) * SEG;
    if (r->z > zmax) { r->z = zmax; r->v = 0; }
    if (r->hy > 0 || r->jv > 0) {
        r->hy += r->jv * dt;
        r->jv -= GRAV * 1.6f * dt;
        if (r->hy <= 0) {                                /* bateu no chao depois da lombada */
            r->hy = 0; r->jv = 0;
            if (r == &pl) { lap_haptic(LAP_HAPTIC_CLICK); sfx_bump(); }
        }
    }
}

/* fator de arrasto com vacuo: alguem logo a frente, na mesma linha */
static float draft(const racer_t *me)
{
    const racer_t *all[NRIV + 1] = { &pl, &rv[0], &rv[1], &rv[2] };
    for (int i = 0; i < NRIV + 1; i++) {
        const racer_t *o = all[i];
        if (o == me) continue;
        float dz = o->z - me->z;
        if (dz > 150 && dz < 1500 && fabsf(o->x - me->x) < 800.0f / RW) return 0.85f;
    }
    return 1.0f;
}

static void collide(racer_t *a, racer_t *b)
{
    float dz = a->z - b->z, dx = a->x - b->x;
    if (fabsf(dz) < 220 && fabsf(dx) < 840.0f / RW) {
        racer_t *back = dz < 0 ? a : b, *front = dz < 0 ? b : a;
        if (back->v > front->v * 0.95f) back->v = front->v * 0.95f;
        float push = dx >= 0 ? 0.05f : -0.05f;
        a->x += push; b->x -= push;
        if ((a == &pl || b == &pl) && hit_cd <= 0) { lap_haptic(LAP_HAPTIC_CLICK); sfx_hit(); hit_cd = 0.4f; }
    }
}

static void update(float dt, uint32_t now)
{
    if (state == ST_MENU || state == ST_CHAR || state == ST_PODIUM) return;
    if (state == ST_COUNT) {
        int n = (int)((now - t_state) / 1000u);
        if (n != last_beep && n < 3) { last_beep = n; lap_tone(660, 120, 40, LAP_WAVE_SQUARE); }
        if (now - t_state >= 3000u) {
            state = ST_RACE; t_race = now;
            if (trk == 1) { shout_on = true; shout_t0 = now; last_shout = now; }
            lap_tone(1320, 300, 45, LAP_WAVE_SQUARE);
            pl.v = START_PUSH;
            for (int i = 0; i < NRIV; i++) rv[i].v = START_PUSH * (0.92f + 0.04f * i);
        }
        return;
    }
    if (hit_cd > 0) hit_cd -= dt;

    /* jogador */
    if (state == ST_RACE) {
        float sp = pl.v / VMAX;
        int si = seg_of(pl.z);
        pl.x += steer * dt * (0.8f + 1.6f * sp);
        pl.x -= dt * 2.0f * sp * sp * curv[si] * (trk == 1 ? 0.24f : (trk == 3 ? 0.20f : 0.18f));   /* forca centrifuga (terra escorrega mais) */
        float extra = brake ? 3000.0f : 0.0f;
        if (trk == 3 && fabsf(pl.x) > 1.02f) {                 /* guard rail: raspa e volta */
            pl.x = pl.x > 0 ? 0.95f : -0.95f;
            if (hit_cd <= 0) { pl.v *= 0.82f; sfx_hit(); hit_cd = 0.6f; }
        }
        if (trk != 3 && fabsf(pl.x) > 1.0f) extra += 1000.0f;  /* calcada */
        if (fabsf(pl.x) > 1.62f) {                            /* muro: bate e volta para a calcada */
            pl.x = pl.x > 0 ? 1.5f : -1.5f;
            if (hit_cd <= 0) { pl.v *= 0.8f; sfx_hit(); hit_cd = 1.0f; }   /* muro: so som, sem vibrar */
        }
        if (pl.v < 1500 && !brake) extra -= 900.0f;           /* devagar: empurra com o pe */
        if (sflag[si] == 1 && si != last_bump_seg) {           /* lombada */
            last_bump_seg = si;
            if (pl.v > 2500) { pl.jv = pl.v * 0.12f; pl.v *= 0.93f; }   /* vibra so na queda */
        }
        float dfac = draft(&pl);
        if (dfac < 1.0f && turbo_t <= 0 && pl.v > 3500) vac += dt / 3.0f;   /* no vacuo e embalado: enche em 3 s */
        else if (turbo_t <= 0) vac -= dt * 0.08f;
        vac = clampf(vac, 0, 1);
        if (vac >= 1.0f) {                                           /* barra cheia: TURBO! */
            turbo_t = 2.0f; vac = 0;
            lap_tone(523, 80, 40, LAP_WAVE_SAW); lap_tone(784, 80, 40, LAP_WAVE_SAW); lap_tone(1046, 160, 40, LAP_WAVE_SAW);
        }
        if (turbo_t > 0) { extra -= 2600.0f; turbo_t -= dt; }
        phys(&pl, pl.drag * dfac, extra, dt);
        g_acc += dt;                                                 /* grava o fantasma a cada 0,1 s */
        while (g_acc >= G_DT) {
            g_acc -= G_DT;
            if (g_rec_n < GMAX) {
                g_rec[g_rec_n].z = pl.z;
                g_rec[g_rec_n].x = (int16_t)clampf(pl.x * 1000.0f, -30000, 30000);
                g_rec[g_rec_n].hy = (int16_t)clampf(pl.hy, -30000, 30000);
                g_rec_n++;
            }
        }
        if (trk == 1 && pl.v > VMAX * 0.72f && now - last_shout > 12000u) {   /* embalou: TACA-LE PAU! */
            shout_on = true; shout_t0 = now; last_shout = now;
        }
        if (pl.z >= fin_z) {
            state = ST_FINISH; t_state = now; fin_time = now - t_race;
            fin_fx = 0;
            fin_place = 1;
            for (int i = 0; i < NRIV; i++) if (rv[i].fin_ms) fin_place++;
            turbo_t = 0;
            if (best_ms[trk] == 0 || fin_time < best_ms[trk]) {      /* NOVO RECORDE: guarda o tempo e o fantasma */
                best_ms[trk] = fin_time; new_rec = true;
                save_records();
                save_ghost(trk);
            }
            lap_tone(880, 150, 45, LAP_WAVE_SQUARE);
            lap_tone(1175, 150, 45, LAP_WAVE_SQUARE);
            lap_tone(1760, 300, 45, LAP_WAVE_SQUARE);
        }
    } else {
        phys(&pl, 1.0f, 4000.0f, dt);                          /* freia depois da chegada */
        float ft = (now - t_state) / 1000.0f;
        if (fin_place == 1) {                                   /* pulando: vibra a cada pouso */
            int hop = (int)(ft * 6.0f / PI_F);
            if (hop > fin_fx && hop < 12) { fin_fx = hop; lap_haptic(LAP_HAPTIC_TICK); }
        } else if (my_char == 3 && ft > 0.6f && fin_fx == 0) {  /* BUM! */
            fin_fx = 1;
            lap_vibrate(350, 100);
            sfx_bump(); sfx_hit();
        }
    }

    if (trk == 2) cars_update(dt);
    critters_update(dt);
    /* adversarios */
    for (int i = 0; i < NRIV; i++) {
        racer_t *r = &rv[i];
        int si = seg_of(r->z);
        
                float dz = pl.z - r->z;
        float desired_x;

        /* 1. Desvio do jogador (mesma lógica, mas mais suave) */
        if (dz > 0 && dz < 600 && fabsf(pl.x - r->x) < 0.5f) {
            desired_x = pl.x > 0 ? pl.x - 0.6f : pl.x + 0.6f;
        }
        /* 2. Apex da curva: fecha por dentro quando a curva é forte */
        else if (fabsf(curv[si]) > 1.5f) {
            desired_x = (curv[si] > 0) ? -0.45f : 0.45f;
        }
        /* 3. Personalidade: cada piloto tem uma faixa preferida diferente */
        else {
            desired_x = (i - 1) * 0.35f;                      /* -0.35, 0, +0.35 */
            desired_x += fsin((float)(now & 0x3FFF) * 0.001f + (float)i * 1.7f) * 0.15f;
        }

        /* Se algum adversário à frente estiver mais lento, abre para ultrapassar */
        for (int k = 0; k < NRIV; k++) {
            if (k == i) continue;
            float gz = rv[k].z - r->z;
            if (gz > 0 && gz < 400 && fabsf(rv[k].x - desired_x) < 0.35f && r->v > rv[k].v + 200.0f)
                desired_x = rv[k].x > 0 ? rv[k].x - 0.55f : rv[k].x + 0.55f;
        }

        r->tx = trk == 2
            ? clampf(desired_x * 0.5f + 0.5f, 0.12f, 0.88f)
            : clampf(desired_x, trk == 3 ? -0.75f : -0.85f, trk == 3 ? 0.75f : 0.85f);
        float d = r->tx - r->x;
        r->x += clampf(d, -0.9f * dt, 0.9f * dt);
        
                float drag = r->drag * draft(r);
        drag *= (trk == 3 ? 0.95f : 1.0f) * DIFF_MUL[diff];
        if (fabsf(curv[si]) > 2.5f) drag *= trk == 3 ? 1.04f : 1.2f;

        /* ---- RUBBER-BANDING: mantém a corrida disputada ---- */
        if (state == ST_RACE) {
            float gap = pl.z - r->z;                        /* + = jogador na frente */
            if (gap > 2000.0f)      drag *= 0.72f;          /* IA muito atrás: acelera */
            else if (gap > 1000.0f) drag *= 0.88f;          /* IA um pouco atrás: acelera de leve */
            else if (gap < -2500.0f) drag *= 1.28f;         /* IA muito à frente: freia */
            else if (gap < -1200.0f) drag *= 1.12f;         /* IA um pouco à frente: freia de leve */
        }
        
        
        
        
        
        
        
        
        
        
        
        
        if (sflag[si] == 1 && r->v > 2500 && r->hy <= 0 && r->jv <= 0) r->jv = r->v * 0.12f;
        float extra = r->z > fin_z ? 4000.0f : (r->v < 1500 ? -900.0f : 0.0f);
        phys(r, drag, extra, dt);
        if (r->z >= fin_z && !r->fin_ms) r->fin_ms = now - t_race + 1;
        collide(&pl, r);
        for (int j = i + 1; j < NRIV; j++) collide(r, &rv[j]);
    }
}

/* ======================================================================
   projecao
   ====================================================================== */
static float bx[DRAW_N + 2], by[DRAW_N + 2], bsc[DRAW_N + 2], bxo[DRAW_N + 2];
static uint8_t bok[DRAW_N + 2];
static int base;
static float camX, camY, camZ;

static void project(void)
{
    base = (int)(camZ / SEG);
    if (base < 0) base = 0;
    float frac = camZ / SEG - base;
    float x = 0, dx = -curv[clampi(base, 0, nseg - 1)] * frac;
    for (int k = 0; k <= DRAW_N; k++) {
        int idx = base + k;
        bxo[k] = x;
        if (idx > nseg) { bok[k] = 0; continue; }
        float dz = idx * SEG - camZ;
        if (dz < 10) bok[k] = 0;
        else {
            float s = CAM_DEPTH / dz;
            bsc[k] = s;
            bx[k] = HALFW + s * (x - camX) * HALFW;
            by[k] = HOR - s * (yb[idx] - camY) * HALFW;
            bok[k] = 1;
        }
        x += dx;
        if (idx < nseg) dx += curv[idx];
    }
}
static inline void P(int k, float u, float h, float *X, float *Y)
{
    float s = bsc[k] * HALFW;
    *X = bx[k] + s * u;
    *Y = by[k] - s * h;
}
static float fog_tab[DRAW_N + 2];             /* neblina por distancia: calculada uma vez so */
static void fog_init(void)
{
    for (int k = 0; k <= DRAW_N + 1; k++) fog_tab[k] = clampf((k - 18.0f) / (DRAW_N - 18.0f), 0, 1) * 0.85f;
}
static inline float fogk(int k) { return fog_tab[k < 0 ? 0 : (k > DRAW_N + 1 ? DRAW_N + 1 : k)]; }
/* face vertical: de (j0,u0) a (j1,u1), alturas h0..h1 */
static void face(int j0, int j1, float u0, float u1, float h0, float h1, uint16_t c)
{
    if (!bok[j0] || !bok[j1]) return;
    float xs[4], ys[4];
    P(j0, u0, h0, &xs[0], &ys[0]);
    P(j1, u1, h0, &xs[1], &ys[1]);
    P(j1, u1, h1, &xs[2], &ys[2]);
    P(j0, u0, h1, &xs[3], &ys[3]);
    poly(xs, ys, 4, c);
}
/* face horizontal (laje): entre as divisas j e j+1, de u0 a u1, na altura h */
static void top(int j, float u0, float u1, float h, uint16_t c)
{
    if (!bok[j] || !bok[j + 1]) return;
    float xs[4], ys[4];
    P(j, u0, h, &xs[0], &ys[0]);
    P(j, u1, h, &xs[1], &ys[1]);
    P(j + 1, u1, h, &xs[2], &ys[2]);
    P(j + 1, u0, h, &xs[3], &ys[3]);
    poly(xs, ys, 4, c);
}

/* ======================================================================
   desenho
   ====================================================================== */
static const rgb_t C_GROUND = {150, 95, 70}, C_WALK = {178, 150, 132}, C_CURB_A = {235, 230, 225},
                   C_CURB_B = {195, 60, 55}, C_ASPH = {108, 92, 102}, C_LANE = {255, 235, 190},
                   C_BRICK = {170, 92, 62}, C_LAJE = {160, 150, 140}, C_EDGE = {110, 105, 100},
                   C_WIN = {55, 45, 70}, C_WINON = {255, 190, 110}, C_DOOR = {95, 60, 40},
                   C_TANK = {55, 110, 195}, C_POLE = {105, 105, 110};

static void draw_house(const house_t *h, int kb)
{
    int ke = h->a + h->len - base;
    if (ke > DRAW_N) ke = DRAW_N;
    if (ke <= kb) return;
    bool front = (h->a - base) == kb;
    float ui = h->side > 0 ? h->u0 : h->u1;
    float kS = h->side > 0 ? 1.12f : 0.6f;     /* sol baixo vindo da esquerda */
    float hb = h->brick ? h->h * 0.45f : h->h;

    for (int j = kb; j < ke; j++) top(j, h->u0, h->u1, h->h, shade(C_LAJE, 1.0f, fogk(j)));
    for (int j = kb; j < ke; j++) {
        float f = fogk(j);
        face(j, j + 1, ui, ui, 0, hb, shade(h->c, kS, f));
        if (h->brick) face(j, j + 1, ui, ui, hb, h->h, shade(C_BRICK, kS, f));
    }
    /* janelas da lateral (longe demais, a neblina ja apaga) */
    for (int j = kb + 1; j + 2 <= ke - 1; j += 4) {
        if (j < h->a - base + 1) continue;
        if (fogk(j) > 0.6f || bsc[j] * HALFW * 520.0f < 1.2f) break;   /* janela menor que 1 pixel: nem desenha */
        for (int fl = 0; fl < h->floors; fl++) {
            float y = fl * 1400.0f + 550.0f;
            bool lit = trk == 2 ? (hxy(h->seed + j, fl) & 3) != 0 : (hxy(h->seed + j, fl) & 7) == 0;
            face(j, j + 2, ui, ui, y, y + 520.0f, lit ? shade(C_WINON, 1.0f, fogk(j) * 0.5f) : shade(C_WIN, 1.0f, fogk(j)));
        }
    }
    /* caixa d'agua na laje */
    if (h->tank) {
        int t0 = h->a - base + 2, t1 = t0 + 3;
        if (t0 >= kb && t1 <= ke) {
            float uc = (h->u0 + h->u1) * 0.5f, ut = h->side > 0 ? uc - 380 : uc + 380;
            for (int j = t0; j < t1; j++) top(j, uc - 380, uc + 380, h->h + 700, shade(C_TANK, 1.15f, fogk(j)));
            for (int j = t0; j < t1; j++) face(j, j + 1, ut, ut, h->h, h->h + 700, shade(C_TANK, kS * 0.95f, fogk(j)));
            face(t0, t0, uc - 380, uc + 380, h->h, h->h + 700, shade(C_TANK, 0.85f, fogk(t0)));
        }
    }
    if (!front) return;
    float f = fogk(kb);
    face(kb, kb, h->u0, h->u1, 0, hb, shade(h->c, 0.86f, f));
    if (h->brick) face(kb, kb, h->u0, h->u1, hb, h->h, shade(C_BRICK, 0.86f, f));
    face(kb, kb, h->u0, h->u1, h->h - 120, h->h, shade(C_EDGE, 0.9f, f));
    float w = h->u1 - h->u0;
    if (f < 0.6f && bsc[kb] * HALFW * 520.0f >= 1.2f)
    for (int fl = 0; fl < h->floors; fl++) {
        float y = fl * 1400.0f + 550.0f;
        for (int c = 0; c < 3; c++) {
            float ua = h->u0 + w * (0.08f + c * 0.31f), ub = ua + w * 0.22f;
            bool door = fl == 0 && c == (h->side > 0 ? 0 : 2);
            if (door) face(kb, kb, ua, ub, 0, 1000, shade(C_DOOR, 0.9f, f));
            else {
                bool lit = trk == 2 ? (hxy(h->seed + c, fl + 9) & 3) != 0 : (hxy(h->seed + c, fl + 9) & 7) == 0;
                face(kb, kb, ua, ub, y, y + 520, lit ? shade(C_WINON, 1.0f, f * 0.5f) : shade(C_WIN, 1.0f, f));
            }
        }
    }
}

/* ---- pilotos e carrinhos em 3D: caixas com sombreamento chapado ------- */
/* unidades do mundo, origem no chao entre as rodas; x lado, y cima, z frente */
typedef struct { int16_t cx, cy, cz, hx, hy, hz; rgb_t c; } mpart_t;
#define MPMAX 40
typedef struct { mpart_t p[MPMAX]; int n; } model_t;
static model_t models[NCHAR];
static const char *const CHAR_NAME[NCHAR] = { "MONALISA", "MC 171", "LUIZA", "BILU" };

static void mp(model_t *m, int cx, int cy, int cz, int hx, int hy, int hz, rgb_t c)
{
    if (m->n >= MPMAX) return;
    mpart_t *p = &m->p[m->n++];
    p->cx = (int16_t)cx; p->cy = (int16_t)cy; p->cz = (int16_t)cz;
    p->hx = (int16_t)hx; p->hy = (int16_t)hy; p->hz = (int16_t)hz; p->c = c;
}
/* tabua, eixos e os 4 rolamentos */
static void mp_cart(model_t *m, rgb_t plank)
{
    rgb_t dark = {75, 58, 46}, steel = {178, 184, 196};
    mp(m, 0, 55, 0, 180, 15, 350, plank);
    mp(m, 0, 32, 300, 215, 12, 22, dark);
    mp(m, 0, 32, -300, 215, 12, 22, dark);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) mp(m, sx * 235, 40, sz * 300, 28, 40, 40, steel);
}
/* olhos (para a vista de frente do menu) */
static void mp_eyes(model_t *m, int y, int z)
{
    rgb_t e = {25, 20, 25};
    mp(m, -28, y, z, 10, 7, 3, e);
    mp(m, 28, y, z, 10, 7, 3, e);
}

static void build_models(void)
{
    model_t *m;
    /* 0: executiva, mulher negra de uns 30 anos, terninho azul-marinho, coque */
    m = &models[0]; m->n = 0;
    {
        rgb_t skin = {112, 72, 50}, navy = {38, 48, 92}, navy2 = {32, 40, 78}, hair = {22, 18, 20};
        mp_cart(m, (rgb_t){120, 70, 45});                          /* tabua envernizada */
        mp(m, 0, 110, 130, 105, 38, 170, navy2);                    /* calca do terninho */
        mp(m, -70, 100, 320, 36, 40, 36, (rgb_t){20, 20, 24});      /* scarpin */
        mp(m, 70, 100, 320, 36, 40, 36, (rgb_t){20, 20, 24});
        mp(m, 0, 250, -170, 115, 140, 68, navy);                    /* blazer */
        mp(m, 0, 330, -101, 38, 50, 3, (rgb_t){245, 245, 240});    /* blusa branca na gola */
        mp(m, -135, 290, -40, 30, 30, 120, navy2);
        mp(m, 135, 290, -40, 30, 30, 120, navy2);
        mp(m, -135, 290, 100, 28, 28, 26, skin);
        mp(m, 135, 290, 100, 28, 28, 26, skin);
        mp(m, 0, 405, -170, 34, 22, 34, skin);
        mp(m, 0, 482, -170, 72, 80, 72, skin);                      /* cabeca */
        mp(m, 0, 556, -176, 76, 12, 78, hair);                      /* cabelo puxado */
        mp(m, 0, 505, -244, 74, 58, 8, hair);
        mp(m, 0, 565, -262, 40, 34, 28, hair);                      /* coque */
        mp(m, -76, 455, -160, 7, 14, 7, (rgb_t){235, 195, 60});     /* brincos dourados */
        mp(m, 76, 455, -160, 7, 14, 7, (rgb_t){235, 195, 60});
        mp(m, 0, 448, -97, 20, 5, 3, (rgb_t){190, 40, 70});        /* batom */
        mp_eyes(m, 492, -97);
    }
    /* 1: funkeiro, cara branco de uns 25, bone de aba reta pra tras, camisa de time, cordao */
    m = &models[1]; m->n = 0;
    {
        rgb_t skin = {238, 194, 165}, jersey = {250, 215, 40}, trim = {30, 140, 70};
        mp_cart(m, (rgb_t){60, 170, 80});                           /* tabua pintada de verde */
        mp(m, 0, 112, 70, 105, 40, 110, (rgb_t){70, 110, 175});     /* bermuda jeans */
        mp(m, -70, 96, 250, 30, 38, 48, (rgb_t){245, 245, 245});    /* meiao branco */
        mp(m, 70, 96, 250, 30, 38, 48, (rgb_t){245, 245, 245});
        mp(m, -70, 100, 330, 42, 44, 34, (rgb_t){230, 55, 50});     /* tenis vermelho */
        mp(m, 70, 100, 330, 42, 44, 34, (rgb_t){230, 55, 50});
        mp(m, 0, 250, -170, 125, 140, 72, jersey);                  /* camisa larga */
        mp(m, 0, 380, -170, 127, 12, 74, trim);                     /* gola */
        mp(m, 0, 210, -243, 40, 50, 3, trim);                       /* numero nas costas */
        mp(m, -142, 290, -40, 27, 27, 120, skin);                   /* bracos de fora */
        mp(m, 142, 290, -40, 27, 27, 120, skin);
        mp(m, -142, 290, 100, 28, 28, 26, skin);
        mp(m, 142, 290, 100, 28, 28, 26, skin);
        mp(m, -142, 300, 20, 30, 12, 14, (rgb_t){240, 200, 50});    /* relogio dourado */
        mp(m, 0, 405, -170, 36, 22, 36, skin);
        mp(m, 0, 482, -170, 72, 80, 72, skin);
        mp(m, 0, 505, -243, 73, 32, 6, (rgb_t){245, 225, 140});     /* cabelo descolorido */
        mp(m, 0, 562, -170, 78, 22, 78, (rgb_t){25, 25, 30});       /* bone */
        mp(m, 0, 548, -282, 70, 5, 46, (rgb_t){25, 25, 30});        /* aba reta virada pra tras */
        mp(m, 0, 570, -91, 30, 12, 3, (rgb_t){220, 40, 40});        /* logo */
        mp(m, 0, 328, -100, 46, 6, 3, (rgb_t){245, 205, 60});       /* cordao de ouro */
        mp(m, 0, 300, -100, 14, 18, 3, (rgb_t){245, 205, 60});      /* pingente */
        mp_eyes(m, 492, -97);
    }
    /* 2: descolada, 28 anos, cabelo rosa, jaqueta jeans com patches, fone no pescoco */
    m = &models[2]; m->n = 0;
    {
        rgb_t skin = {205, 152, 118}, pink = {235, 95, 195}, denim = {115, 155, 205};
        mp_cart(m, (rgb_t){235, 120, 190});                         /* tabua rosa cheia de adesivo */
        mp(m, 0, 30, -100, 40, 3, 40, (rgb_t){80, 220, 200});       /* adesivo */
        mp(m, 0, 110, 130, 105, 38, 170, (rgb_t){32, 32, 38});      /* calca preta rasgada */
        mp(m, -60, 150, 160, 22, 3, 22, skin);                      /* rasgos no joelho */
        mp(m, 60, 150, 160, 22, 3, 22, skin);
        mp(m, -70, 100, 320, 40, 42, 36, (rgb_t){80, 220, 200});    /* tenis turquesa */
        mp(m, 70, 100, 320, 40, 42, 36, (rgb_t){80, 220, 200});
        mp(m, 0, 250, -170, 118, 140, 70, denim);                   /* jaqueta jeans */
        mp(m, 0, 255, -101, 42, 110, 3, (rgb_t){30, 30, 36});       /* camiseta preta */
        mp(m, -45, 300, -241, 24, 24, 3, (rgb_t){240, 80, 150});    /* patches nas costas */
        mp(m, 45, 230, -241, 22, 22, 3, (rgb_t){250, 220, 50});
        mp(m, -30, 180, -241, 18, 18, 3, (rgb_t){80, 200, 120});
        mp(m, -135, 290, -40, 31, 31, 120, denim);
        mp(m, 135, 290, -40, 31, 31, 120, denim);
        mp(m, -135, 290, 100, 28, 28, 26, skin);
        mp(m, 135, 290, 100, 28, 28, 26, skin);
        mp(m, 0, 405, -170, 34, 22, 34, skin);
        mp(m, 0, 405, -212, 64, 9, 9, (rgb_t){25, 25, 30});         /* fone no pescoco */
        mp(m, -62, 410, -170, 16, 28, 28, (rgb_t){220, 40, 60});
        mp(m, 62, 410, -170, 16, 28, 28, (rgb_t){220, 40, 60});
        mp(m, 0, 482, -170, 70, 80, 72, skin);
        mp(m, 0, 558, -172, 76, 14, 78, pink);                      /* cabelo rosa */
        mp(m, 0, 480, -242, 74, 66, 9, pink);
        mp(m, -48, 592, -170, 28, 24, 28, pink);                    /* dois coquinhos */
        mp(m, 48, 592, -170, 28, 24, 28, pink);
        mp_eyes(m, 492, -97);
        mp(m, 0, 492, -96, 60, 3, 2, (rgb_t){30, 30, 35});          /* oculos */
    }
    /* 3: ET cinza... verde! cabecao, olhos pretos enormes */
    m = &models[3]; m->n = 0;
    {
        rgb_t g = {130, 200, 110}, g2 = {105, 170, 90}, eye = {10, 10, 15};
        mp_cart(m, (rgb_t){185, 190, 205});                         /* tabua de metal de disco voador */
        mp(m, 0, 30, 0, 150, 4, 300, (rgb_t){90, 230, 255});         /* luz por baixo */
        mp(m, 0, 105, 130, 78, 30, 170, g2);                         /* pernas finas */
        mp(m, -60, 92, 330, 30, 24, 48, g2);                         /* pes compridos */
        mp(m, 60, 92, 330, 30, 24, 48, g2);
        mp(m, 0, 235, -170, 82, 125, 55, g);                         /* tronco magro */
        mp(m, -108, 280, -40, 17, 17, 125, g);
        mp(m, 108, 280, -40, 17, 17, 125, g);
        mp(m, -108, 280, 115, 22, 10, 42, g2);                       /* dedos longos */
        mp(m, 108, 280, 115, 22, 10, 42, g2);
        mp(m, 0, 385, -170, 20, 30, 20, g);
        mp(m, 0, 440, -150, 55, 30, 55, g);                          /* queixo fino */
        mp(m, 0, 535, -170, 118, 95, 112, g);                        /* cabecao */
        mp(m, 0, 650, -175, 100, 28, 100, g);                        /* topo da cabeca */
        mp(m, -56, 528, -56, 40, 28, 4, eye);                        /* olhos pretos enormes */
        mp(m, 56, 528, -56, 40, 28, 4, eye);
        mp(m, -66, 540, -51, 9, 7, 2, (rgb_t){140, 160, 185});      /* brilho dos olhos */
        mp(m, 46, 540, -51, 9, 7, 2, (rgb_t){140, 160, 185});
        mp(m, 0, 436, -94, 14, 3, 3, (rgb_t){60, 100, 55});          /* boquinha */
    }
}

static const signed char FACE_N[6][3] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1} };
static const uint8_t FACE_V[6][4] = { {0,2,6,4}, {1,5,7,3}, {0,4,5,1}, {2,3,7,6}, {0,1,3,2}, {4,6,7,5} };
static const float LX = -0.55f, LY = 0.65f, LZ = 0.52f;   /* sol baixo, a esquerda e a frente */

/* ponto do mundo -> tela: u lateral (a partir do meio da rua), h acima da rua, z ao longo da pista */
static bool proj3(float u, float h, float z, float *X, float *Y)
{
    float dz = z - camZ;
    if (dz < 60) return false;
    float fk = z / SEG - base;
    int k = (int)fk;
    if (k < 0 || k >= DRAW_N) return false;
    float f = fk - k;
    float xo = bxo[k] + (bxo[k + 1] - bxo[k]) * f;
    float s = CAM_DEPTH / dz * HALFW;
    *X = HALFW + s * (xo + u - camX);
    *Y = HOR - s * (road_y(z) + h - camY);
    return true;
}

/* desenha um modelo: proj(lx,ly,lz) leva o ponto local para a tela; cam = camera no
   referencial do modelo (para descartar faces de costas e ordenar); luz no mesmo referencial */
typedef bool (*mproj_t)(float lx, float ly, float lz, float *X, float *Y);
static void draw_model(const model_t *m, mproj_t proj, float lcx, float lcy, float lcz,
                       float lx, float ly, float lz, float fog)
{
    int ord[MPMAX];
    float dist[MPMAX];
    for (int i = 0; i < m->n; i++) {
        float dx = m->p[i].cx - lcx, dy = m->p[i].cy - lcy, dz = m->p[i].cz - lcz;
        dist[i] = dx * dx + dy * dy + dz * dz;
        int j = i;
        while (j > 0 && dist[ord[j - 1]] < dist[i]) { ord[j] = ord[j - 1]; j--; }
        ord[j] = i;
    }
    for (int n = 0; n < m->n; n++) {
        const mpart_t *pt = &m->p[ord[n]];
        float X[8], Y[8];
        bool ok = true;
        for (int c = 0; c < 8 && ok; c++)
            ok = proj(pt->cx + ((c & 1) ? pt->hx : -pt->hx), pt->cy + ((c & 2) ? pt->hy : -pt->hy),
                      pt->cz + ((c & 4) ? pt->hz : -pt->hz), &X[c], &Y[c]);
        if (!ok) continue;
        for (int f = 0; f < 6; f++) {
            float nx = FACE_N[f][0], ny = FACE_N[f][1], nz = FACE_N[f][2];
            float fx = pt->cx + nx * pt->hx, fy = pt->cy + ny * pt->hy, fz = pt->cz + nz * pt->hz;
            if (nx * (lcx - fx) + ny * (lcy - fy) + nz * (lcz - fz) <= 0) continue;   /* de costas */
            float d = nx * lx + ny * ly + nz * lz;
            float b = 0.5f + 0.6f * (d > 0 ? d : 0);
            float xs[4], ys[4];
            for (int v = 0; v < 4; v++) { xs[v] = X[FACE_V[f][v]]; ys[v] = Y[FACE_V[f][v]]; }
            rgb_t pc = pt->c;
            if (g_blend) pc = (rgb_t){ (uint8_t)((pc.r + 200) / 2), (uint8_t)((pc.g + 235) / 2), (uint8_t)((pc.b + 255) / 2) };
            poly(xs, ys, 4, shade(pc, b, fog));
        }
    }
}

/* projecao na pista (carrinho em u0, z0, altura h0, girado por yaw) */
static float mp_u0, mp_z0, mp_h0, mp_sy, mp_cy;
static bool proj_track(float lx, float ly, float lz, float *X, float *Y)
{
    float wx = lx * mp_cy + lz * mp_sy, wz = -lx * mp_sy + lz * mp_cy;
    return proj3(mp_u0 + wx, mp_h0 + ly, mp_z0 + wz, X, Y);
}

static void draw_cart3d(const racer_t *r, float yaw, float shake, float fog, bool player)
{
    (void)player;
    float z0 = r->z, u0 = r->x * RW, h0 = r->hy + shake;
    float fk = z0 / SEG - base;
    int k = (int)fk;
    if (k < 0 || k >= DRAW_N) return;
    float xo = bxo[k] + (bxo[k + 1] - bxo[k]) * (fk - k);
    float sy = fsin(yaw), cy = fsin(yaw + PI_F * 0.5f);   /* cos sem chamar cosf no mesmo angulo */
    mp_u0 = u0; mp_z0 = z0; mp_h0 = h0; mp_sy = sy; mp_cy = cy;
    /* camera e luz no referencial do carrinho */
    float wcx = camX - xo - u0, wcy = camY - road_y(z0) - h0, wcz = camZ - z0;
    float lcx = wcx * cy - wcz * sy, lcz = wcx * sy + wcz * cy, lcy = wcy;
    float llx = LX * cy - LZ * sy, llz = LX * sy + LZ * cy;

    /* sombra no chao (o fantasma nao tem) */
    if (!g_blend) {
        float xs[4], ys[4];
        static const float SX[4] = { -260, 260, 260, -260 }, SZ[4] = { -390, -390, 390, 390 };
        bool ok = true;
        for (int i = 0; i < 4; i++) {
            float wx = SX[i] * cy + SZ[i] * sy, wz = -SX[i] * sy + SZ[i] * cy;
            ok &= proj3(u0 + wx, 0, z0 + wz, &xs[i], &ys[i]);
        }
        if (ok) poly_dark(xs, ys, 4, 0.55f);
    }
    draw_model(&models[r->ch], proj_track, lcx, lcy, lcz, llx, LY, llz, fog);
    if (g_blend) return;
    /* corda: das maos ate a ponta do eixo da frente */
    for (int sd = -1; sd <= 1; sd += 2) {
        float ax, ay, bx2, by2;
        if (proj_track(sd * 135.0f, 290, 128, &ax, &ay) && proj_track(sd * 200.0f, 45, 300, &bx2, &by2))
            line(ax, ay, bx2, by2, shade((rgb_t){150, 110, 60}, 1, fog));
    }
}

/* vitrine do menu: modelo girando, visto de frente e um pouco de cima */
static float sh_cx, sh_cy, sh_sc, sh_sy, sh_cyw;
static bool proj_show(float lx, float ly, float lz, float *X, float *Y)
{
    /* gira em torno do eixo vertical; o modelo olha para +z, a camera fica em +z olhando para -z */
    float x = lx * sh_cyw + lz * sh_sy, z = -lx * sh_sy + lz * sh_cyw, y = ly - 300;
    /* inclina 0,35 rad para ver de cima */
    float y2 = y * 0.9394f - z * 0.3429f, z2 = y * 0.3429f + z * 0.9394f;
    float d = 2600.0f - z2;
    if (d < 200) return false;
    *X = sh_cx + x * sh_sc / d;
    *Y = sh_cy - y2 * sh_sc / d;
    return true;
}
static void draw_showcase(int ch, float cx, float cy, float scale, float yaw)
{
    sh_cx = cx; sh_cy = cy; sh_sc = scale;
    sh_sy = fsin(yaw); sh_cyw = fsin(yaw + PI_F * 0.5f);
    /* camera (0, 300 + .., 2600) no referencial da vitrine -> referencial do modelo */
    float cz = 2600.0f * 0.9394f, cyv = 300.0f + 2600.0f * 0.3429f;
    float lcx = -cz * sh_sy, lcz = cz * sh_cyw;               /* desfaz o giro */
    float lx = -0.4f * sh_cyw - 0.75f * sh_sy, lz = -0.4f * sh_sy + 0.75f * sh_cyw;
    draw_model(&models[ch], proj_show, lcx, cyv, lcz, lx, 0.55f, lz, 0);
}

/* ---- fundo: ceu do por do sol, sol e morros com luzinhas -------------- */
static void build_bg(void)
{
    static const rgb_t st[4] = { {58, 42, 108}, {155, 74, 134}, {240, 122, 114}, {255, 189, 122} };
    for (int y = 0; y < FH; y++) {
        float t = clampf(y / (HOR + 8.0f), 0, 1) * 3.0f;
        int i = (int)t; if (i > 2) i = 2;
        float f = t - i;
        rgb_t a = st[i], b = st[i + 1];
        for (int x = 0; x < FW; x++) {
            float r = a.r + (b.r - a.r) * f, g = a.g + (b.g - a.g) * f, bb = a.b + (b.b - a.b) * f;
            float dx = x - FW * 0.22f, dy = y - (HOR - 3.0f), d = sqrtf(dx * dx + dy * dy);
            if (d < 36) {
                float q = (1 - d / 36) * (1 - d / 36) * 0.9f;
                r += (255 - r) * q; g += (225 - g) * q; bb += (160 - bb) * q;
            }
            if (d < 9) { r = 255; g = 246; bb = 205; }
            uint16_t c = pk((int)r, (int)g, (int)bb);
            float yh = HOR - 5 - 7 * sinf(x * 0.028f) - 4 * sinf(x * 0.09f + 1);
            float yh2 = HOR + 7 - 5 * sinf(x * 0.05f + 2) - 2 * sinf(x * 0.13f);
            uint32_t hh = hxy((uint32_t)x, (uint32_t)y);
            if (y > yh2) {
                c = pk(85 - (int)((y - yh2) * 0.3f), 48, 90);
                if ((hh & 31) == 0) c = pk(255, 205, 110);
                else if ((hh & 63) == 1) c = pk(150, 200, 255);
            } else if (y > yh) {
                c = pk(107, 63, 108);
                if ((hh & 63) == 0) c = pk(255, 210, 122);
                else if ((hh & 127) == 1) c = pk(141, 90, 134);
            }
            bgimg[y * FW + x] = c;
        }
    }
}

static void draw_kites(float t)
{
    static const float K[4][2] = { {215, 30}, {248, 44}, {190, 46}, {150, 24} };
    static const rgb_t KC[4] = { {255, 45, 85}, {47, 180, 255}, {255, 225, 74}, {51, 221, 102} };
    for (int i = 0; i < 4; i++) {
        float x = K[i][0] * FW / 320.0f + 3 * fsin(t * 1.1f + i * 1.7f), y = K[i][1] * HOR / 58.0f + 1.5f * fsin(t * 1.7f + i);
        float xs[4] = { x, x + 4, x, x - 4 }, ys[4] = { y - 5, y, y + 6, y };
        poly(xs, ys, 4, pk(KC[i].r, KC[i].g, KC[i].b));
        for (int s = 0; s < 14; s++)
            pset((int)(x + 1.5f * fsin(s * 0.9f + t * 4 + i)), (int)(y + 6 + s), pk(240, 240, 240));
    }
}

/* postes (lista p/ os fios) */
/* ======================================================================
   pista 2 (morro da vo): cerca, eucaliptos, araucarias, vacas, casa da vo
   ====================================================================== */
enum { PR_POST, PR_EUC, PR_ARA, PR_COW, PR_FARM, PR_PINE, PR_HAY, PR_CONE, PR_STAND, PR_ARCH };
typedef struct { int16_t a; uint8_t type; int8_t side; float u, sz; } prop_t;
#define PMAX 1600
static prop_t pr[PMAX];
static int np;

static int cmp_prop(const void *a, const void *b) { return ((const prop_t *)a)->a - ((const prop_t *)b)->a; }
static void add_prop(int a, int type, int side, float u, float sz)
{
    if (np >= PMAX || a < 2 || a >= nseg - 8) return;
    pr[np].a = (int16_t)a; pr[np].type = (uint8_t)type; pr[np].side = (int8_t)side;
    pr[np].u = u; pr[np].sz = sz; np++;
}

static void build_props(void)
{
    np = 0;
    rng = 0x13579BDFu;
    add_prop(14, PR_FARM, 1, RW * 1.9f, 1);                  /* casa da vo, no alto */
    for (int side = -1; side <= 1; side += 2) {
        for (int s = 4; s < nseg - 10; s += 6)                  /* cerca de madeira */
            if (rnd() % 19) add_prop(s, PR_POST, side, side * RW * 1.4f, 1);
        int s = 3;
        while (s < nseg - 10) {                                 /* arvores */
            s += 5 + (int)(rnd() % 12);
            float u = side * RW * (1.9f + (float)(rnd() % 100) / 100.0f * 3.5f);
            add_prop(s, rnd() % 5 < 2 ? PR_ARA : PR_EUC, side, u, 0.8f + (float)(rnd() % 45) / 100.0f);
        }
        for (s = 40 + (int)(rnd() % 60); s < nseg - 20; s += 90 + (int)(rnd() % 160))   /* vacas no pasto */
            add_prop(s, PR_COW, side, side * RW * (2.0f + (float)(rnd() % 150) / 100.0f), 1);
    }
}

/* caixa 3D seguindo a rua: laje, lado virado para a rua, frente */
static void box3(int k0, int k1, float u0, float u1, float h0, float h1, rgb_t c, bool front)
{
    if (k1 > DRAW_N) k1 = DRAW_N;
    if (k0 < 1) { k0 = 1; front = false; }
    if (k1 <= k0) return;
    for (int j = k0; j < k1; j++) top(j, u0, u1, h1, shade(c, 1.05f, fogk(j)));
    float ui = u0 > 0 ? u0 : (u1 < 0 ? u1 : 0);
    if (ui != 0) {
        float kS = ui > 0 ? 1.12f : 0.6f;
        for (int j = k0; j < k1; j++) face(j, j + 1, ui, ui, h0, h1, shade(c, kS, fogk(j)));
    }
    if (front) face(k0, k0, u0, u1, h0, h1, shade(c, 0.86f, fogk(k0)));
}

static void draw_prop(const prop_t *p, int k)
{
    float f = fogk(k), u = p->u, z = p->sz;
    switch (p->type) {
    case PR_POST: {
        rgb_t wood = {120, 82, 50};
        box3(k, k + 1, u - 55, u + 55, 0, 760, wood, true);
        int k2 = k + 6 > DRAW_N ? DRAW_N : k + 6;
        if (k2 > k + 1) {
            face(k, k2, u, u, 380, 450, shade(wood, 1.0f, f));
            face(k, k2, u, u, 600, 670, shade(wood, 1.0f, f));
        }
        break;
    }
    case PR_EUC:
        box3(k, k + 1, u - 70 * z, u + 70 * z, 0, 3000 * z, (rgb_t){205, 195, 175}, true);
        box3(k - 1, k + 3, u - 650 * z, u + 650 * z, 2300 * z, 4300 * z, (rgb_t){70, 120, 65}, true);
        box3(k, k + 2, u - 380 * z, u + 380 * z, 4300 * z, 4900 * z, (rgb_t){85, 135, 70}, true);
        break;
    case PR_ARA: {
        rgb_t g = {38, 88, 52};
        box3(k, k + 1, u - 80 * z, u + 80 * z, 0, 4600 * z, (rgb_t){115, 78, 52}, true);
        box3(k - 2, k + 3, u - 1150 * z, u + 1150 * z, 3300 * z, 3520 * z, g, true);
        box3(k - 1, k + 2, u - 850 * z, u + 850 * z, 3850 * z, 4050 * z, g, true);
        box3(k, k + 2, u - 520 * z, u + 520 * z, 4350 * z, 4560 * z, g, true);
        break;
    }
    case PR_COW: {
        rgb_t w = {240, 238, 230}, b = {35, 30, 30};
        for (int l = 0; l < 2; l++)                               /* pernas */
            for (int sd = -1; sd <= 1; sd += 2)
                box3(k + l * 4, k + l * 4 + 1, u + sd * 120 - 35, u + sd * 120 + 35, 0, 380, w, true);
        box3(k, k + 5, u - 180, u + 180, 380, 900, w, true);    /* corpo */
        box3(k + 1, k + 3, u - 182, u + 182, 560, 860, b, false);/* mancha preta */
        box3(k - 1, k + 1, u - 120, u + 120, 650, 1020, b, true);/* cabeca */
        break;
    }
    case PR_FARM: {
        rgb_t wall = {240, 236, 222}, roof = {190, 62, 45}, win = {60, 110, 170};
        float u0 = u, u1 = u + 3200, uc = (u0 + u1) * 0.5f, h = 1700, ridge = h + 1100;
        int k1 = k + 22 > DRAW_N ? DRAW_N : k + 22;
        box3(k, k1, u0, u1, 0, h, wall, k >= 1);
        for (int j = (k < 1 ? 1 : k); j < k1; j++) {          /* telhado de duas aguas: lado da rua */
            if (!bok[j] || !bok[j + 1]) continue;
            float xs[4], ys[4];
            P(j, u0 - 150, h, &xs[0], &ys[0]);
            P(j, uc, ridge, &xs[1], &ys[1]);
            P(j + 1, uc, ridge, &xs[2], &ys[2]);
            P(j + 1, u0 - 150, h, &xs[3], &ys[3]);
            poly(xs, ys, 4, shade(roof, 1.0f, fogk(j)));
        }
        if (k >= 1 && bok[k]) {
            float xs[3], ys[3];                                  /* oitao da frente */
            P(k, u0, h, &xs[0], &ys[0]);
            P(k, u1, h, &xs[1], &ys[1]);
            P(k, uc, ridge, &xs[2], &ys[2]);
            poly(xs, ys, 3, shade(wall, 0.9f, f));
            face(k, k, u0 + 500, u0 + 1100, 600, 1250, shade(win, 1, f));
            face(k, k, u1 - 1100, u1 - 500, 600, 1250, shade(win, 1, f));
            face(k, k, uc - 300, uc + 300, 0, 1300, shade((rgb_t){110, 70, 45}, 1, f));
        }
        for (int j = k + 3; j + 2 < k1; j += 6)                 /* janelas da lateral */
            if (j >= 1) face(j, j + 2, u0, u0, 600, 1250, shade(win, 1, fogk(j)));
        break;
    }
    }
}

/* fundo de dia: ceu, nuvens, morros verdes com araucarias na crista */
static void build_bg_farm(void)
{
    for (int y = 0; y < FH; y++) {
        float t = clampf(y / (HOR + 8.0f), 0, 1);
        int r = (int)(80 + 125 * t), g = (int)(150 + 80 * t), b = (int)(232 + 15 * t);
        for (int x = 0; x < FW; x++) {
            int rr = r, gg = g, bbb = b;
            float dx = x - FW * 0.78f, dy = y - FH * 0.08f, d = sqrtf(dx * dx + dy * dy);
            if (d < 26) { float q = (1 - d / 26) * (1 - d / 26) * 0.7f; rr += (int)((255 - rr) * q); gg += (int)((250 - gg) * q); bbb += (int)((220 - bbb) * q); }
            if (d < 6) { rr = 255; gg = 252; bbb = 225; }
            /* nuvens: elipses achatadas */
            static const float CL[4][3] = { {0.15f, 0.10f, 26}, {0.30f, 0.07f, 18}, {0.55f, 0.15f, 30}, {0.92f, 0.20f, 20} };
            for (int c = 0; c < 4; c++) {
                float ex = (x - CL[c][0] * FW) / CL[c][2], ey = (y - CL[c][1] * FH) / (CL[c][2] * 0.32f);
                if (ex * ex + ey * ey < 1) { rr = 250; gg = 252; bbb = 255; if (ey > 0.3f) { rr = 225; gg = 232; bbb = 245; } }
            }
            uint16_t col = pk(rr, gg, bbb);
            float yh = HOR - 6 - 8 * sinf(x * 0.021f + 1) - 4 * sinf(x * 0.07f);
            float yh2 = HOR + 4 - 6 * sinf(x * 0.035f + 3) - 2 * sinf(x * 0.11f);
            if (y > yh2) col = pk(78, 128, 72);
            else if (y > yh) col = pk(120, 165, 125);
            /* araucarias na crista do morro do fundo */
            int ax = x % 23;
            if (y <= yh && y > yh - 7 && ax == 11) col = pk(70, 95, 80);
            if (y <= yh - 5 && y > yh - 8 && ax >= 8 && ax <= 14) col = pk(70, 100, 82);
            bgimg[y * FW + x] = col;
        }
    }
}

/* ---- "TACA-LE PAU!" grande, com estouro de quadrinho ------------------ */

static void glyph_top(int x, int y, char ch, int sc, uint16_t c)  /* so as 2 primeiras linhas: brilho */
{
    glyph_rows(x, y, ch, sc, c, 2);
}
static void shout_line(const char *s, int cx, int y, int sc, float t, int base_i)
{
    int w = text_w(s, sc), x = cx - w / 2;
    uint16_t blk = pk(20, 10, 10), red = pk(200, 30, 40), yel = pk(255, 214, 40), hi = pk(255, 250, 190);
    for (int i = 0; s[i]; i++) {
        int gx = x + i * 6 * sc;
        int gy = y + (((i + base_i) & 1) ? sc : -sc / 2) + (int)(fsin(t * 16.0f + (i + base_i) * 1.1f) * sc * 0.5f);
        glyph(gx + sc + 1, gy + sc + 1, s[i], sc, red);                     /* sombra vermelha */
        for (int dy = -2; dy <= 2; dy++)                                   /* contorno grosso */
            for (int dx = -2; dx <= 2; dx++)
                if (dx || dy) glyph(gx + dx, gy + dy, s[i], sc, blk);
        glyph(gx, gy, s[i], sc, yel);
        glyph_top(gx, gy, s[i], sc, hi);
    }
}
static void draw_shout(float t)
{
    float pop = t < 0.14f ? t / 0.14f * 1.25f : (t < 0.28f ? 1.25f - (t - 0.14f) / 0.14f * 0.25f : 1.0f);
    if (t > 1.35f) pop = clampf(1.0f - (t - 1.35f) / 0.25f, 0.2f, 1.0f);
    int sc = (int)(FH / 34.0f * pop + 0.5f);
    if (sc < 1) sc = 1;
    int cx = FW / 2, cy = (int)(FH * 0.40f);
    /* estouro: miolo + pontas, girando devagar */
    float R = (text_w("TACA-LE", sc) * 0.5f + 3 * sc) * 1.05f, Ri = R * 0.68f, ang = t * 0.9f;
    float ix[18], iy[18];
    for (int i = 0; i < 18; i++) {
        float a = ang + i * (2 * PI_F / 18);
        ix[i] = cx + Ri * fsin(a + PI_F * 0.5f); iy[i] = cy + Ri * 0.62f * fsin(a);
    }
    for (int i = 0; i < 18; i++) {
        int j = (i + 1) % 18;
        float a = ang + (i + 0.5f) * (2 * PI_F / 18), rr = (i & 1) ? R : R * 0.86f;
        float xs[3] = { ix[i], cx + rr * fsin(a + PI_F * 0.5f), ix[j] };
        float ys[3] = { iy[i], cy + rr * 0.62f * fsin(a), iy[j] };
        poly(xs, ys, 3, (i & 1) ? pk(255, 120, 30) : pk(255, 170, 40));
    }
    {
        /* miolo inteiro: preenche por faixas (o poly aceita convexo) */
        float xs[18], ys[18];
        for (int i = 0; i < 18; i++) { xs[i] = ix[i]; ys[i] = iy[i]; }
        poly(xs, ys, 18, pk(255, 150, 35));
    }
    shout_line("TACA-LE", cx, cy - 7 * sc - sc, sc, t, 0);
    shout_line("PAU!", cx, cy + sc, sc + (sc > 2 ? 1 : 0), t, 7);
}

/* ======================================================================
   pista 3: descendo a Consolacao no fim da tarde. Predios dos dois lados,
   postes acesos, carros vindo na mao contraria (lado esquerdo).
   ====================================================================== */
static const rgb_t BCOL[8] = {
    {150, 140, 130}, {120, 125, 135}, {175, 160, 140}, {95, 110, 140},
    {160, 120, 100}, {135, 135, 120}, {110, 120, 125}, {185, 175, 160},
};
static void build_buildings(void)
{
    nh = 0;
    rng = 0x5EED1234u;
    for (int side = -1; side <= 1; side += 2) {
        int s = 2;
        while (s < nseg - 15 && nh < HMAX) {
            if (rnd() % 11 == 0) { s += 6 + (int)(rnd() % 6); continue; }   /* rua transversal */
            house_t *h = &hs[nh++];
            h->a = (int16_t)s;
            h->len = (int16_t)(14 + rnd() % 14);
            h->side = (int8_t)side;
            uint32_t r = rnd() % 10;
            h->floors = (uint8_t)(r < 3 ? 4 + rnd() % 4 : (r < 8 ? 8 + rnd() % 8 : 16 + rnd() % 8));
            h->h = h->floors * 1400.0f + 600.0f;
            float depth = 4000.0f + (float)(rnd() % 3000);
            float inner = RW * 1.75f;
            if (side > 0) { h->u0 = inner; h->u1 = inner + depth; }
            else { h->u0 = -inner - depth; h->u1 = -inner; }
            h->c = BCOL[rnd() % 8];
            h->brick = 0;
            h->tank = (rnd() % 10) < 3;
            h->seed = rnd();
            s += h->len + 1;
        }
    }
}

/* fundo do entardecer: ceu avermelhado e a silhueta da cidade */
static void build_bg_city(void)
{
    for (int y = 0; y < FH; y++) {
        float t = clampf(y / (HOR + 10.0f), 0, 1);
        float r, g, b;
        if (t < 0.5f) { float q = t / 0.5f; r = 60 + 150 * q; g = 40 + 40 * q; b = 95 - 10 * q; }
        else { float q = (t - 0.5f) / 0.5f; r = 210 + 45 * q; g = 80 + 80 * q; b = 85 + 10 * q; }
        for (int x = 0; x < FW; x++) {
            float dx = x - FW * 0.42f, dy = y - (HOR + 6.0f), d = sqrtf(dx * dx + dy * dy * 4);
            float rr = r, gg = g, bb = b;
            if (d < 60) { float q = (1 - d / 60) * (1 - d / 60) * 0.8f; rr += (255 - rr) * q; gg += (190 - gg) * q; bb += (120 - bb) * q; }
            uint16_t c = pk((int)rr, (int)gg, (int)bb);
            /* skyline: blocos de largura e altura variadas */
            int col = x / 7;
            uint32_t hh = hxy((uint32_t)col, 77);
            float top = HOR + 4 - (float)(hh % 26) - ((hh >> 8) % 3 == 0 ? 10 : 0);
            if (y > top) {
                c = pk(55, 38, 70);
                uint32_t w = hxy((uint32_t)x, (uint32_t)y);
                if ((x % 7) != 0 && (w & 15) == 0) c = pk(255, 200, 120);
                if ((int)top == y - 1 && (hh & 31) == 3 && x % 7 == 3) c = pk(255, 40, 40);   /* luz de antena */
            }
            bgimg[y * FW + x] = c;
        }
    }
}

/* brilho aditivo (lampadas, farois) */
static void add_glow(float cx, float cy, float r, int cr, int cg, int cb)
{
    if (r < 1) r = 1;
    if (r > 34) r = 34;                     /* perto da camera o brilho nao cresce mais */
    int ya = (int)(cy - r), yb2 = (int)(cy + r), xa = (int)(cx - r), xb = (int)(cx + r);
    /* tudo em 1/16 de pixel: d2 e r2 inteiros, q em 0..256 sem divisao por pixel */
    int32_t cx16 = (int32_t)(cx * 16.0f), cy16 = (int32_t)(cy * 16.0f), r16 = (int32_t)(r * 16.0f);
    int32_t r2 = r16 * r16, inv = (int32_t)((1u << 24) / (uint32_t)(r2 > 0 ? r2 : 1));
    if (ya < 0) ya = 0;
    if (yb2 > FH - 1) yb2 = FH - 1;
    if (xa < 0) xa = 0;
    if (xb > FW - 1) xb = FW - 1;
    for (int y = ya; y <= yb2; y++) {
        int32_t dy = y * 16 - cy16, dy2 = dy * dy;
        if (dy2 >= r2) continue;
        uint16_t *p = fb + y * FW;
        for (int x = xa; x <= xb; x++) {
            int32_t dx = x * 16 - cx16, d2 = dx * dx + dy2;
            if (d2 >= r2) continue;
            int32_t q = 256 - ((d2 * inv) >> 16);
            q = (q * q) >> 8;
            uint16_t v = p[x];
            int R = ((v >> 11) & 31) << 3, G = ((v >> 5) & 63) << 2, B = (v & 31) << 3;
            p[x] = pk(R + ((cr * q) >> 8), G + ((cg * q) >> 8), B + ((cb * q) >> 8));
        }
    }
}
#define GLOW_MAX 48
static float gl_x[GLOW_MAX], gl_y[GLOW_MAX], gl_r[GLOW_MAX];
static uint8_t gl_kind[GLOW_MAX];      /* 0 poste (laranja), 1 farol (branco) */
static int gl_n;
static void glow_add(float x, float y, float r, int kind)
{
    if (gl_n < GLOW_MAX) { gl_x[gl_n] = x; gl_y[gl_n] = y; gl_r[gl_n] = r; gl_kind[gl_n] = (uint8_t)kind; gl_n++; }
}

/* carros na mao contraria */
typedef struct { float z, x, v; rgb_t c; uint8_t bus; } car_t;
#define NCARS 12
static car_t cars[NCARS];
static const rgb_t CARC[6] = { {200, 30, 40}, {230, 230, 230}, {30, 30, 35}, {40, 90, 180}, {170, 170, 175}, {230, 190, 40} };
static void car_spawn(car_t *c, float zmin)
{
    c->z = zmin + (float)(rnd() % 30000);
    c->x = (rnd() & 1) ? -0.3f : -0.72f;
    c->v = 7000.0f + (float)(rnd() % 4000);
    c->c = CARC[rnd() % 6];
    c->bus = (rnd() % 5) == 0;                       /* de vez em quando, um onibus */
    if (c->bus) { c->c = (rnd() & 1) ? (rgb_t){240, 190, 30} : (rgb_t){40, 120, 200}; c->x = -0.72f; c->v *= 0.8f; }
}
static void cars_init(void)
{
    rng = 0xC0FFEE11u;
    for (int i = 0; i < NCARS; i++) { car_spawn(&cars[i], 6000.0f + i * 5000.0f); }
}
static void draw_bus(const car_t *c, int k)
{
    if (k < 1 || k + 24 > DRAW_N) return;
    float u = c->x * RW, f = fogk(k);
    rgb_t blk = {25, 25, 28}, glass = {40, 50, 70};
    box3(k + 2, k + 4, u - 520, u - 400, 0, 330, blk, true);
    box3(k + 2, k + 4, u + 400, u + 520, 0, 330, blk, true);
    box3(k + 19, k + 21, u - 520, u - 400, 0, 330, blk, true);
    box3(k + 19, k + 21, u + 400, u + 520, 0, 330, blk, true);
    box3(k, k + 24, u - 510, u + 510, 220, 1600, c->c, true);       /* caixa do onibus */
    for (int j = k + 2; j < k + 23; j += 3)                         /* janelas na lateral */
        face(j, j + 2, u + 512, u + 512, 950, 1380, shade(glass, 1.0f, fogk(j)));
    face(k, k, u - 460, u + 460, 820, 1400, shade((rgb_t){80, 100, 140}, 1.1f, f));   /* para-brisa */
    face(k, k, u - 320, u + 320, 1430, 1540, pk(255, 170, 40));    /* letreiro */
    face(k, k, u - 470, u - 330, 420, 540, pk(255, 250, 220));      /* farois */
    face(k, k, u + 330, u + 470, 420, 540, pk(255, 250, 220));
    if (bok[k]) {
        float x1, y1, x2, y2;
        P(k, u - 400, 480, &x1, &y1);
        P(k, u + 400, 480, &x2, &y2);
        float r = bsc[k] * HALFW * 600;
        glow_add(x1, y1, r, 1);
        glow_add(x2, y2, r, 1);
    }
}
static void draw_car(const car_t *c, int k)
{
    if (c->bus) { draw_bus(c, k); return; }
    if (k < 1 || k + 10 > DRAW_N) return;
    float u = c->x * RW, f = fogk(k);
    rgb_t blk = {25, 25, 28}, glass = {45, 55, 80};
    box3(k + 1, k + 2, u - 450, u - 330, 0, 230, blk, true);       /* rodas */
    box3(k + 1, k + 2, u + 330, u + 450, 0, 230, blk, true);
    box3(k + 7, k + 8, u - 450, u - 330, 0, 230, blk, true);
    box3(k + 7, k + 8, u + 330, u + 450, 0, 230, blk, true);
    box3(k, k + 10, u - 440, u + 440, 150, 620, c->c, true);       /* carroceria */
    box3(k + 3, k + 7, u - 380, u + 380, 620, 1000, glass, true);  /* cabine */
    box3(k + 3, k + 7, u - 360, u + 360, 1000, 1040, c->c, false); /* teto */
    face(k + 3, k + 3, u - 370, u + 370, 640, 980, shade((rgb_t){90, 110, 150}, 1.1f, f));   /* para-brisa */
    face(k, k, u - 400, u - 240, 400, 520, pk(255, 250, 220));      /* farois */
    face(k, k, u + 240, u + 400, 400, 520, pk(255, 250, 220));
    face(k, k, u - 150, u + 150, 300, 420, shade((rgb_t){60, 60, 65}, 1, f));   /* grade */
    if (bok[k]) {
        float x1, y1, x2, y2;
        P(k, u - 320, 460, &x1, &y1);
        P(k, u + 320, 460, &x2, &y2);
        float r = bsc[k] * HALFW * 520;
        glow_add(x1, y1, r, 1);
        glow_add(x2, y2, r, 1);
    }
}

static void cars_update(float dt)
{
    {
        if (crash_cd > 0) crash_cd -= dt;
        for (int i = 0; i < NCARS; i++) {
            car_t *c = &cars[i];
            c->z -= c->v * dt;
            if (c->z < pl.z - PLAYER_DZ - 600) car_spawn(c, pl.z + 40000.0f);
            float dz = c->z - pl.z;
            if (dz > -200 && dz < (c->bus ? 4900 : 2100) && fabsf(c->x - pl.x) < (c->bus ? 820.0f : 750.0f) / RW && crash_cd <= 0 && state == ST_RACE) {
                pl.v *= 0.35f;                                 /* batida feia */
                pl.x += c->x < pl.x ? 0.25f : -0.25f;
                lap_haptic(LAP_HAPTIC_CLICK); sfx_hit(); sfx_hit();
                crash_cd = 1.0f;
            }
        }
    }
}

/* ======================================================================
   pista 4: pista profissional de descida, na serra. Guard rail dos dois
   lados, faixas pintadas, zebras e cones nas chicanes, fardos de feno,
   arquibancadas com torcida, portais com faixa por cima da pista.
   ====================================================================== */
static const char *const ARCH_TXT[4] = { "COPA ROLIMA", "SEM FREIO", "DESCIDA LIVRE", "TACA-LE PAU" };

static void build_props_pro(void)
{
    np = 0;
    rng = 0x0DEC1DEu;
    add_prop(20, PR_STAND, 1, RW * 1.7f, 1);                    /* arquibancadas na largada */
    add_prop(20, PR_STAND, -1, -RW * 1.7f, 1);
    add_prop(fin_seg - 70, PR_STAND, 1, RW * 1.7f, 1);          /* e na chegada */
    add_prop(fin_seg - 70, PR_STAND, -1, -RW * 1.7f, 1);
    for (int s = 300, i = 0; s < fin_seg - 200; s += 420, i++)  /* portais com faixa */
        add_prop(s, PR_ARCH, 0, 0, (float)(i % 4));
    for (int side = -1; side <= 1; side += 2) {
        int s = 3;
        while (s < nseg - 10) {                                 /* pinheiros fora do guard rail */
            s += 4 + (int)(rnd() % 9);
            if ((s > 10 && s < 60) || (s > fin_seg - 80 && s < fin_seg - 30)) continue;   /* lugar das arquibancadas */
            add_prop(s, PR_PINE, side, side * RW * (1.6f + (float)(rnd() % 100) / 100.0f * 3.0f), 0.8f + (float)(rnd() % 50) / 100.0f);
        }
    }
    for (int s = 30; s < fin_seg; s++) {
        float c = curv[s];
        if (fabsf(c) > 3.0f && s % 7 == 0)                       /* feno do lado de fora da curva */
            add_prop(s, PR_HAY, c > 0 ? -1 : 1, (c > 0 ? -1.0f : 1.0f) * RW * 1.32f, 1);
        if (fabsf(c) > 5.0f && s % 3 == 0) {                     /* cones na chicane */
            add_prop(s, PR_CONE, -1, -RW * 0.97f, 1);
            add_prop(s, PR_CONE, 1, RW * 0.97f, 1);
        }
    }
}

static void draw_prop_pro(const prop_t *p, int k)
{
    float f = fogk(k), u = p->u, z = p->sz;
    switch (p->type) {
    case PR_PINE: {
        rgb_t g = {35, 95, 55}, g2 = {45, 115, 65};
        box3(k, k + 1, u - 70 * z, u + 70 * z, 0, 1200 * z, (rgb_t){100, 70, 45}, true);
        box3(k - 2, k + 3, u - 900 * z, u + 900 * z, 900 * z, 2000 * z, g, true);
        box3(k - 1, k + 2, u - 650 * z, u + 650 * z, 2000 * z, 3000 * z, g2, true);
        box3(k, k + 2, u - 380 * z, u + 380 * z, 3000 * z, 3900 * z, g, true);
        box3(k, k + 1, u - 140 * z, u + 140 * z, 3900 * z, 4500 * z, g2, true);
        break;
    }
    case PR_HAY: {
        rgb_t hay = {225, 190, 95};
        box3(k, k + 3, u - 380, u + 380, 0, 420, hay, true);
        box3(k + 1, k + 4, u - 320, u + 320, 420, 820, hay, true);
        face(k, k, u - 380, u + 380, 190, 230, shade((rgb_t){150, 110, 60}, 1, f));   /* amarracao */
        break;
    }
    case PR_CONE:
        box3(k, k + 1, u - 70, u + 70, 0, 120, (rgb_t){250, 110, 20}, true);
        box3(k, k + 1, u - 45, u + 45, 120, 190, (rgb_t){250, 250, 250}, true);
        box3(k, k + 1, u - 30, u + 30, 190, 300, (rgb_t){250, 110, 20}, true);
        break;
    case PR_STAND: {
        float sg = p->side > 0 ? 1.0f : -1.0f, u0 = u;
        int k1 = k + 40 > DRAW_N ? DRAW_N : k + 40;
        for (int t = 0; t < 4; t++) {                            /* degraus subindo para fora */
            float a = u0 + sg * t * 650, b = u0 + sg * (t * 650 + 2600 - t * 650);
            float lo = sg > 0 ? a : b, hi = sg > 0 ? b : a;
            box3(k, k1, lo, hi, 0, 500.0f * (t + 1), (rgb_t){150, 150, 160}, k >= 1);
            float ui = a;                                        /* torcida na frente do degrau */
            for (int j = (k < 1 ? 1 : k); j < k1; j += 2) {
                uint32_t h = hxy((uint32_t)(p->a + j), (uint32_t)t);
                static const uint8_t TC[6][3] = { {230, 50, 50}, {250, 210, 40}, {40, 120, 220}, {250, 250, 250}, {60, 170, 80}, {240, 130, 40} };
                const uint8_t *c = TC[h % 6];
                float hop = 70.0f * fabsf(fsin(g_now_s * 9.0f + (float)(h & 15)));   /* torcida pulando */
                float h0 = 500.0f * (t + 1) + hop;
                face(j, j + 1, ui, ui, h0, h0 + 260, shade((rgb_t){c[0], c[1], c[2]}, 1, fogk(j)));
                face(j, j + 1, ui, ui, h0 + 260, h0 + 380, shade((rgb_t){230, 180, 140}, 1, fogk(j)));
            }
        }
        box3(k, k1, sg > 0 ? u0 + 600 : u0 - 2900, sg > 0 ? u0 + 2900 : u0 - 600, 3300, 3450, (rgb_t){200, 40, 40}, k >= 1);   /* cobertura */
        break;
    }
    case PR_ARCH: {
        if (k < 1 || !bok[k]) break;
        float w = RW * 1.3f;
        box3(k, k + 1, -w - 70, -w + 70, 0, 3500, (rgb_t){60, 60, 70}, true);
        box3(k, k + 1, w - 70, w + 70, 0, 3500, (rgb_t){60, 60, 70}, true);
        face(k, k, -w, w, 2700, 3400, shade((rgb_t){30, 90, 200}, 1, f));
        face(k, k, -w, w, 2650, 2700, shade((rgb_t){250, 210, 40}, 1, f));
        const char *tx = ARCH_TXT[(int)p->sz & 3];
        float xl, yt, xr, yb3;
        P(k, -w, 3400, &xl, &yt);
        P(k, w, 2700, &xr, &yb3);
        int sc = (int)((yb3 - yt) / 9.0f);
        if (sc >= 1) {
            if (sc > 4) sc = 4;
            int tw = text_w(tx, sc);
            if (tw < xr - xl)
                text((int)((xl + xr) * 0.5f) - tw / 2, (int)(yt + (yb3 - yt - 7 * sc) * 0.5f), tx, sc, pk(255, 255, 255), false);
        }
        break;
    }
    default: break;
    }
}

/* fundo: dia claro na serra, montanhas azuladas e morro com pinheiros */
static void build_bg_mountain(void)
{
    for (int y = 0; y < FH; y++) {
        float t = clampf(y / (HOR + 8.0f), 0, 1);
        int r = (int)(70 + 120 * t), g = (int)(140 + 80 * t), b = (int)(230 + 15 * t);
        for (int x = 0; x < FW; x++) {
            int rr = r, gg = g, bb = b;
            float dx = x - FW * 0.2f, dy = y - FH * 0.07f, d = sqrtf(dx * dx + dy * dy);
            if (d < 24) { float q = (1 - d / 24) * (1 - d / 24) * 0.7f; rr += (int)((255 - rr) * q); gg += (int)((250 - gg) * q); bb += (int)((225 - bb) * q); }
            if (d < 5) { rr = 255; gg = 253; bb = 230; }
            uint16_t col = pk(rr, gg, bb);
            /* picos ao longe (dentes de serra) e morro verde mais perto */
            float px = fmodf(x * 1.0f, 70.0f) / 70.0f;
            float peak = HOR - 4 - 22 * (px < 0.5f ? px * 2 : (1 - px) * 2) * (0.6f + 0.4f * sinf(x * 0.013f + 1)) ;
            float hill = HOR + 6 - 5 * sinf(x * 0.03f + 2) - 3 * sinf(x * 0.09f);
            if (y > hill) col = pk(60, 120, 70);
            else if (y > peak) {
                col = pk(110, 130, 165);
                if (y < peak + 3 && peak < HOR - 14) col = pk(240, 245, 250);      /* neve no topo */
            }
            int ax = x % 11;                                     /* pinheirinhos no morro */
            if (y <= hill && y > hill - 6 && ax >= 4 && ax <= 6 && (y > hill - 3 || ax == 5)) col = pk(40, 90, 55);
            bgimg[y * FW + x] = col;
        }
    }
}

/* ---- bichos atravessando: cachorro no morro de Osasco, galinha no sitio ---- */
enum { CR_DOG = 1, CR_HEN = 2 };
static void critters_update(float dt)
{
    if (trk > 1) return;
    if (state == ST_RACE) {
        crit_timer -= dt;
        if (crit_timer <= 0) {
            for (int i = 0; i < NCRIT; i++)
                if (!crit[i].active) {
                    critter_t *c = &crit[i];
                    c->active = 1; c->hit = 0;
                    c->type = trk == 0 ? CR_DOG : CR_HEN;
                    c->dir = (rnd() & 1) ? 1.0f : -1.0f;
                    c->x = -c->dir * 1.55f;
                    c->z = pl.z + (70.0f + (float)(rnd() % 30)) * SEG;
                    c->sp = c->type == CR_DOG ? 0.55f + (float)(rnd() % 30) / 100.0f : 0.35f + (float)(rnd() % 20) / 100.0f;
                    break;
                }
            crit_timer = 7.0f + (float)(rnd() % 7);
        }
    }
    for (int i = 0; i < NCRIT; i++) {
        critter_t *c = &crit[i];
        if (!c->active) continue;
        if (c->type == CR_HEN && (rnd() % 90) == 0) c->dir = -c->dir;   /* galinha nao sabe para onde vai */
        c->x += c->dir * c->sp * dt;
        if (c->x > 1.8f || c->x < -1.8f || c->z < pl.z - PLAYER_DZ) { c->active = 0; continue; }
        if (!c->hit && state == ST_RACE && fabsf(c->z - pl.z) < 300 && fabsf(c->x - pl.x) * RW < 450) {
            c->hit = 1;                                       /* trombou: perde um pouco, o bicho foge */
            pl.v *= 0.88f;
            c->sp *= 2.5f; c->dir = c->x > pl.x ? 1.0f : -1.0f;
            sfx_hit();
        }
    }
}
/* caixa com as pontas em qualquer ordem */
static void bx3(int k0, int k1, float a, float b, float h0, float h1, rgb_t c)
{
    box3(k0, k1, a < b ? a : b, a < b ? b : a, h0, h1, c, true);
}
static void draw_critter(const critter_t *c, int k)
{
    if (k < 1 || k + 4 > DRAW_N) return;
    float u = c->x * RW, d = c->dir, w = fsin(g_now_s * (c->type == CR_DOG ? 22.0f : 30.0f)) * 40.0f;
    if (c->type == CR_DOG) {
        rgb_t br = {155, 100, 55}, dk = {95, 60, 32};
        bx3(k, k + 1, u - 210 + w, u - 150 + w, 0, 210, dk);        /* patas */
        bx3(k, k + 1, u + 150 - w, u + 210 - w, 0, 210, dk);
        bx3(k + 2, k + 3, u - 210 - w, u - 150 - w, 0, 210, dk);
        bx3(k + 2, k + 3, u + 150 + w, u + 210 + w, 0, 210, dk);
        bx3(k, k + 3, u - 270, u + 270, 190, 430, br);               /* corpo */
        float hx = u + d * 330;
        bx3(k, k + 2, hx - 110, hx + 110, 320, 570, br);              /* cabeca */
        bx3(k + 1, k + 2, hx - 45, hx + 45, 570, 640, dk);            /* orelha */
        bx3(k, k + 1, hx + d * 110 - 35, hx + d * 110 + 35, 380, 450, (rgb_t){30, 25, 25});   /* focinho */
        float tx = u - d * 310;
        bx3(k + 1, k + 2, tx - 40, tx + 40, 400, 580 + w, dk);        /* rabo abanando */
    } else {
        rgb_t wh = {245, 244, 236}, re = {215, 35, 35}, ye = {240, 190, 40};
        bx3(k, k + 1, u - 60, u - 40 + w * 0.5f, 0, 150, ye);         /* pernas */
        bx3(k, k + 1, u + 40 - w * 0.5f, u + 60, 0, 150, ye);
        bx3(k, k + 2, u - 150, u + 150, 140, 390, wh);                 /* corpo */
        bx3(k, k + 2, u - d * 170 - 50, u - d * 170 + 50, 300, 470, wh);   /* rabo */
        float hx = u + d * 160;
        bx3(k, k + 1, hx - 70, hx + 70, 350, 500, wh);                 /* cabeca */
        bx3(k, k + 1, hx - 35, hx + 35, 500, 570, re);                 /* crista */
        bx3(k, k + 1, hx + d * 70 - 25 + d * 25, hx + d * 70 + 25 + d * 25, 410, 450, ye);   /* bico */
        bx3(k, k + 1, hx + d * 30 - 20, hx + d * 30 + 20, 360, 400, re);   /* barbela */
    }
}

#define POLE_EVERY 28
static float wire_x[2][8], wire_y[2][8];
static int wire_n[2];

static void draw_buttons(void);
/* tela de escolha da pista: dois cartoes grandes, toque em um deles */
static void draw_menu(uint32_t now)
{
    for (int y = 0; y < FH; y++) {
        uint16_t c = pk(30 + y / 4, 20 + y / 6, 60 + y / 3);
        hspan(y, 0, FW - 1, c);
    }
    shout_line("ROLIMA", FW / 2, (int)(FH * 0.08f), FH / 34 + 1, now / 1000.0f, 0);
    {
        char t[40];
        snprintf(t, sizeof t, "ESCOLHA A PISTA - PILOTO: %s", CHAR_NAME[my_char]);
        text_c((int)(FH * 0.35f), t, 1, pk(255, 230, 180));
    }
    int cw = (FW - 30) / 4, chh = (int)(FH * 0.40f), y0 = (int)(FH * 0.40f);
    for (int t = 0; t < 4; t++) {
        int x0 = 6 + t * (cw + 6);
        /* miniatura do cenario: ceu, morro, estrada */
        for (int y = 0; y < chh; y++) {
            float v = (float)y / chh;
            uint16_t c;
            if (t == 0) c = v < 0.55f ? pk(90 + (int)(160 * v / 0.55f), 50 + (int)(80 * v / 0.55f), 130 - (int)(20 * v)) : pk(150, 95, 70);
            else if (t == 1) c = v < 0.55f ? pk(90 + (int)(110 * v / 0.55f), 155 + (int)(70 * v / 0.55f), 235) : pk(92, 150, 62);
            else if (t == 2) c = v < 0.55f ? pk(60 + (int)(190 * v / 0.55f), 40 + (int)(90 * v / 0.55f), 95) : pk(120, 115, 115);
            else c = v < 0.55f ? pk(70 + (int)(120 * v / 0.55f), 140 + (int)(80 * v / 0.55f), 230) : pk(95, 160, 70);
            hspan(y0 + y, x0, x0 + cw - 1, c);
            if (v >= 0.55f) {                                 /* estrada em perspectiva */
                float w = (v - 0.55f) / 0.45f * cw * 0.45f;
                hspan(y0 + y, x0 + cw / 2 - (int)w, x0 + cw / 2 + (int)w, t == 0 ? pk(108, 92, 102) : (t == 1 ? pk(172, 126, 84) : (t == 2 ? pk(70, 68, 80) : pk(82, 84, 92))));
                if (t == 3) {                                  /* guard rail e faixas */
                    pset(x0 + cw / 2 - (int)w - 1, y0 + y, pk(200, 205, 215));
                    pset(x0 + cw / 2 + (int)w + 1, y0 + y, pk(200, 205, 215));
                    pset(x0 + cw / 2 - (int)(w * 0.9f), y0 + y, pk(245, 245, 245));
                    pset(x0 + cw / 2 + (int)(w * 0.9f), y0 + y, pk(245, 245, 245));
                    if ((y / 3) & 1) pset(x0 + cw / 2, y0 + y, pk(250, 210, 40));
                }
            }
        }
        if (t == 0) {                                          /* predinhos coloridos */
            for (int i = 0; i < 5; i++) {
                int bx0 = x0 + 4 + i * (cw - 8) / 5, bh = chh / 4 + (i * 7) % (chh / 5);
                rect(bx0, y0 + chh * 55 / 100 - bh, bx0 + (cw - 8) / 5 - 2, y0 + chh * 55 / 100, pk(HCOL[i].r, HCOL[i].g, HCOL[i].b));
            }
        } else if (t == 3) {                                   /* montanhas e pinheiros */
            int by1 = y0 + chh * 55 / 100;
            for (int i = 0; i < 3; i++) {
                int mx = x0 + 6 + i * (cw - 12) / 2;
                for (int yy = 0; yy < chh / 4; yy++)
                    hspan(by1 - chh / 4 + yy, mx - yy < x0 ? x0 : mx - yy, mx + yy > x0 + cw - 1 ? x0 + cw - 1 : mx + yy, pk(110, 130, 165));
            }
            for (int i = 0; i < 5; i++) {
                int ax = x0 + 4 + i * (cw - 8) / 4;
                rect(ax - 3, by1 - 8, ax + 3, by1, pk(35, 95, 55));
                rect(ax - 1, by1 - 12, ax + 1, by1 - 8, pk(35, 95, 55));
            }
        } else if (t == 2) {                                   /* predios altos com janelas acesas */
            for (int i = 0; i < 4; i++) {
                int bw = (cw - 8) / 4, bx0 = x0 + 4 + i * bw, bh = chh / 3 + (i * 11) % (chh / 4);
                int by1 = y0 + chh * 55 / 100;
                rect(bx0, by1 - bh, bx0 + bw - 2, by1, pk(BCOL[i * 2].r * 2 / 3, BCOL[i * 2].g * 2 / 3, BCOL[i * 2].b * 2 / 3));
                for (int wy = by1 - bh + 3; wy < by1 - 2; wy += 4)
                    for (int wx = bx0 + 2; wx < bx0 + bw - 3; wx += 3)
                        if ((hxy((uint32_t)wx, (uint32_t)wy) & 3) != 0) pset(wx, wy, pk(255, 200, 120));
            }
        } else {                                               /* araucarias */
            for (int i = 0; i < 4; i++) {
                int ax = x0 + 10 + i * (cw - 20) / 3, ay = y0 + chh * 55 / 100;
                rect(ax - 1, ay - chh / 3, ax + 1, ay, pk(115, 78, 52));
                rect(ax - 9, ay - chh / 3, ax + 9, ay - chh / 3 + 3, pk(38, 88, 52));
                rect(ax - 6, ay - chh / 3 - 5, ax + 6, ay - chh / 3 - 2, pk(38, 88, 52));
            }
        }
        rect(x0, y0, x0 + cw - 1, y0 + 1, pk(255, 255, 255));   /* moldura */
        rect(x0, y0 + chh - 2, x0 + cw - 1, y0 + chh - 1, pk(255, 255, 255));
        rect(x0, y0, x0 + 1, y0 + chh - 1, pk(255, 255, 255));
        rect(x0 + cw - 2, y0, x0 + cw - 1, y0 + chh - 1, pk(255, 255, 255));
        static const char *L1[4] = { "MORRO AO", "MORRO", "RUA DA", "PISTA" }, *L2[4] = { "POR DO SOL", "DA VO", "CONSOLACAO", "PRO" };
        const char *l1 = L1[t], *l2 = L2[t];
        int sc = cw >= text_w("CONSOLACAO", 2) + 6 ? 2 : 1;
        text(x0 + (cw - text_w(l1, sc)) / 2, y0 + chh - 16 * sc - 6, l1, sc, pk(255, 255, 255), true);
        text(x0 + (cw - text_w(l2, sc)) / 2, y0 + chh - 8 * sc - 4, l2, sc, pk(255, 220, 80), true);
        if (best_ms[t]) {                                     /* recorde da pista */
            char rb[16];
            snprintf(rb, sizeof rb, "%u:%02u.%u", (unsigned)(best_ms[t] / 60000u), (unsigned)((best_ms[t] / 1000u) % 60u), (unsigned)((best_ms[t] / 100u) % 10u));
            rect(x0 + 2, y0 + 2, x0 + cw - 3, y0 + 11, pk(0, 0, 0));
            text(x0 + (cw - text_w(rb, 1)) / 2, y0 + 3, rb, 1, pk(255, 214, 40), false);
        }
    }
    /* botoes de baixo: campeonato e dificuldade */
    {
        int by0 = (int)(FH * 0.84f), by1 = FH - 4;
        rect(6, by0, FW / 2 - 4, by1, pk(200, 40, 50));
        rect(FW / 2 + 3, by0, FW - 7, by1, pk(40, 110, 200));
        int bsc2 = text_w("CAMPEONATO", 2) < FW / 2 - 16 ? 2 : 1;
        text((6 + FW / 2 - 4) / 2 - text_w("CAMPEONATO", bsc2) / 2, (by0 + by1) / 2 - 7 * bsc2 / 2, "CAMPEONATO", bsc2, pk(255, 255, 255), true);
        char db[24];
        snprintf(db, sizeof db, "%s", DIFF_TXT[diff]);
        text((FW / 2 + 3 + FW - 7) / 2 - text_w(db, bsc2) / 2, (by0 + by1) / 2 - 7 * bsc2 / 2, db, bsc2, pk(255, 255, 255), true);
    }
    draw_buttons();
}

static void draw_buttons(void)
{
    rect(4, 4, 27, 25, pk(200, 40, 50));
    glyph(10, 8, 'X', 2, pk(255, 255, 255));
    rect(31, 4, 54, 25, sound_on ? pk(40, 110, 230) : pk(70, 70, 80));
    {                                                     /* alto-falante */
        uint16_t w = pk(255, 255, 255);
        rect(35, 11, 38, 18, w);
        float xs[4] = { 38, 44, 44, 38 }, ys[4] = { 11, 7, 22, 18 };
        poly(xs, ys, 4, w);
        if (sound_on) { line(47, 11, 47, 18, w); line(50, 9, 50, 20, w); }
        else { line(46, 9, 52, 20, pk(255, 80, 80)); line(52, 9, 46, 20, pk(255, 80, 80)); }
    }
}

/* ---- final da corrida: comemoracao, cabeca baixa ou... o Bilu explode ---- */
static void disc(float cx, float cy, float r, uint16_t c)
{
    int ya = (int)floorf(cy - r), yb2 = (int)ceilf(cy + r);
    for (int y = ya; y <= yb2; y++) {
        float dy = y + 0.5f - cy, q = r * r - dy * dy;
        if (q < 0) continue;
        float w = sqrtf(q);
        hspan(y, (int)floorf(cx - w + 0.5f), (int)floorf(cx + w - 0.5f), c);
    }
}
static model_t anim_m;
static int part_group(const mpart_t *p)          /* 0 corpo/carrinho, 1 cabeca, 2 bracos */
{
    if (p->cy <= 60) return 0;
    if (p->cy >= 380) return 1;
    if ((p->cx >= 100 || p->cx <= -100) && p->cy >= 260 && p->cy <= 320) return 2;
    return 0;
}
/* mode 0: campeao pulando com os bracos para cima; 1: cabeca baixa dizendo "nao";
   2: explosao (pecas voando), t em segundos desde a explosao */
static const model_t *animate(int ch, int mode, float t)
{
    const model_t *src = &models[ch];
    anim_m = *src;
    float jump = mode == 0 ? fabsf(fsin(t * 6.0f)) * 200.0f : 0.0f;
    for (int i = 0; i < anim_m.n; i++) {
        mpart_t *p = &anim_m.p[i];
        int g = part_group(p);
        float x = p->cx, y = p->cy, z = p->cz;
        if (mode == 0 && g == 2) {                     /* bracos esticados para cima, acenando */
            float sgn = p->cx > 0 ? 1.0f : -1.0f;
            float d = z + 160.0f;
            x = p->cx * 1.05f + sgn * fsin(t * 14.0f) * 22.0f;
            y = 330.0f + d;
            z = -150.0f;
            int16_t tmp = p->hy; p->hy = p->hz; p->hz = tmp;
        }
        if (mode == 1 && g == 1) {                     /* cabeca baixa balancando: "nao" */
            x += fsin(t * 7.0f) * 30.0f;
            y -= 70.0f;
            z += 45.0f;
        }
        if (mode == 2) {                               /* cada peca voa para um lado */
            uint32_t h = hxy((uint32_t)i, 99);
            float vx = ((float)(h & 255) / 128.0f - 1.0f) * 1600.0f;
            float vy = 900.0f + (float)((h >> 8) & 255) * 6.0f;
            float vz = 600.0f + (float)((h >> 16) & 255) * 9.0f;       /* muitas vem para a tela */
            if (g == 0 && p->cy <= 60) { vx *= 0.15f; vy *= 0.2f; vz *= 0.1f; }   /* o carrinho fica */
            x += vx * t; y += vy * t - 2400.0f * t * t; z += vz * t;
        }
        p->cx = (int16_t)clampf(x, -30000, 30000);
        p->cy = (int16_t)clampf(y + jump, -30000, 30000);
        p->cz = (int16_t)clampf(z, -30000, 30000);
    }
    return &anim_m;
}

/* meleca verde grudando na tela */
static void goo(float t)
{
    for (int i = 0; i < 9; i++) {
        float ti = 0.08f * i;
        if (t < ti) continue;
        uint32_t h = hxy((uint32_t)i, 4242);
        float cx = FW * (0.08f + 0.84f * (float)(h & 255) / 255.0f);
        float cy = FH * (0.10f + 0.70f * (float)((h >> 8) & 255) / 255.0f);
        float r = FH * (0.06f + 0.10f * (float)((h >> 16) & 255) / 255.0f);
        float pop = clampf((t - ti) / 0.12f, 0, 1);
        r *= pop < 1 ? 0.4f + 0.75f * pop : 1.0f;
        uint16_t dark = pk(55, 140, 35), mid = pk(105, 215, 55), hi = pk(200, 255, 150);
        float drip = clampf((t - ti - 0.2f) * 18.0f, 0, r * 3.0f);     /* escorrendo */
        rect(cx - r * 0.22f - 1, cy, cx + r * 0.22f + 1, cy + drip, dark);
        rect(cx - r * 0.22f, cy, cx + r * 0.22f, cy + drip, mid);
        disc(cx - r * 0.1f, cy + drip, r * 0.3f, mid);
        for (int b = 0; b < 3; b++) {                  /* mancha irregular: tres bolhas */
            uint32_t q = hxy((uint32_t)(i * 3 + b), 31);
            float ox = ((float)(q & 255) / 128.0f - 1.0f) * r * 0.55f, oy = ((float)((q >> 8) & 255) / 128.0f - 1.0f) * r * 0.4f;
            float rb = r * (0.55f + 0.3f * (float)((q >> 16) & 255) / 255.0f);
            disc(cx + ox, cy + oy, rb + 1, dark);
        }
        for (int b = 0; b < 3; b++) {
            uint32_t q = hxy((uint32_t)(i * 3 + b), 31);
            float ox = ((float)(q & 255) / 128.0f - 1.0f) * r * 0.55f, oy = ((float)((q >> 8) & 255) / 128.0f - 1.0f) * r * 0.4f;
            float rb = r * (0.55f + 0.3f * (float)((q >> 16) & 255) / 255.0f);
            disc(cx + ox, cy + oy, rb, mid);
        }
        for (int k = 0; k < 5; k++) {                  /* respingos em volta */
            uint32_t q = hxy((uint32_t)(i * 7 + k), 77);
            float a = (float)(q & 1023) / 1023.0f * 2 * PI_F;
            float dd = r * (1.2f + (float)((q >> 10) & 255) / 255.0f);
            float rr = r * (0.12f + 0.15f * (float)((q >> 18) & 255) / 255.0f);
            disc(cx + dd * fsin(a + PI_F * 0.5f), cy + dd * fsin(a), rr, mid);
        }
        disc(cx - r * 0.35f, cy - r * 0.35f, r * 0.18f, hi);
    }
}

/* ordem de chegada dos 4 pilotos (quem nao chegou ainda vai pela posicao na pista) */
static void race_rank(uint8_t out[4])
{
    uint32_t key[4];
    uint8_t ch[4];
    ch[0] = (uint8_t)my_char; key[0] = fin_time;
    for (int i = 0; i < NRIV; i++) {
        ch[i + 1] = rv[i].ch;
        key[i + 1] = rv[i].fin_ms ? rv[i].fin_ms : 10000000u - (uint32_t)(rv[i].z / 10.0f);
    }
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)i;
    for (int i = 1; i < 4; i++) {
        uint8_t v = out[i];
        int j = i;
        while (j > 0 && key[out[j - 1]] > key[v]) { out[j] = out[j - 1]; j--; }
        out[j] = v;
    }
    for (int i = 0; i < 4; i++) out[i] = ch[out[i]];
}
/* piloto de frente, animado (mode como em animate; 3 = parado) */
static void show_anim(int ch, int mode, float t, float cx, float cy, float sc, float yaw)
{
    sh_cx = cx; sh_cy = cy; sh_sc = sc;
    sh_sy = fsin(yaw); sh_cyw = fsin(yaw + PI_F * 0.5f);
    float cz = 2600.0f * 0.9394f, cyv = 300.0f + 2600.0f * 0.3429f;
    draw_model(mode == 3 ? &models[ch] : animate(ch, mode, t), proj_show, -cz * sh_sy, cyv, cz * sh_cyw,
               -0.4f * sh_cyw - 0.75f * sh_sy, 0.55f, -0.4f * sh_sy + 0.75f * sh_cyw, 0);
}
static void confetti(float t)
{
    for (int i = 0; i < 40; i++) {
        uint32_t h = hxy((uint32_t)i, 555);
        float x = (float)(h % (uint32_t)FW), y = fmodf((float)((h >> 9) % 200) + t * (30.0f + (h >> 20) % 30), (float)FH);
        static const uint8_t CC[5][3] = { {255, 214, 40}, {255, 80, 120}, {80, 200, 255}, {120, 230, 90}, {255, 255, 255} };
        const uint8_t *c = CC[i % 5];
        rect(x, y, x + 1, y + 2, pk(c[0], c[1], c[2]));
    }
}
/* classificacao do campeonato (pontos de agora + os desta corrida, se ainda nao somados) */
static void champ_table(int *pts, uint8_t ord[NCHAR])
{
    for (int c = 0; c < NCHAR; c++) pts[c] = champ_pts[c];
    if (state == ST_FINISH && !champ_awarded) {
        uint8_t rk[4];
        race_rank(rk);
        for (int i = 0; i < 4; i++) pts[rk[i]] += CHAMP_PTS[i];
    }
    for (int i = 0; i < NCHAR; i++) ord[i] = (uint8_t)i;
    for (int i = 1; i < NCHAR; i++) {
        uint8_t v = ord[i];
        int j = i;
        while (j > 0 && pts[ord[j - 1]] < pts[v]) { ord[j] = ord[j - 1]; j--; }
        ord[j] = v;
    }
}
/* soma os pontos da corrida e segue o campeonato */
static void start_track(int t);
static void champ_next(void)
{
    if (!champ_awarded) {
        uint8_t rk[4];
        race_rank(rk);
        for (int i = 0; i < 4; i++) champ_pts[rk[i]] += CHAMP_PTS[i];
        champ_awarded = true;
    }
    champ_idx++;
    if (champ_idx < 4) start_track(champ_idx);
    else { state = ST_PODIUM; t_state = lap_millis(); fin_fx = 0; }
}

/* podio do campeonato */
static void draw_podium(uint32_t now)
{
    float t = (now - t_state) / 1000.0f;
    g_now_s = now / 1000.0f;
    for (int y = 0; y < FH; y++) hspan(y, 0, FW - 1, pk(25 + y / 3, 20 + y / 5, 55 + y / 3));
    int pts[NCHAR];
    uint8_t ord[NCHAR];
    champ_table(pts, ord);
    bool fem = ord[0] == 0 || ord[0] == 2;
    char buf[40];
    snprintf(buf, sizeof buf, "%s: %s", fem ? "CAMPEA" : "CAMPEAO", CHAR_NAME[ord[0]]);
    text_c(8, buf, 2, pk(255, 214, 40));
    /* degraus: 2o a esquerda, 1o no meio, 3o a direita; o 4o fica de canto */
    static const float PX[3] = { 0.36f, 0.56f, 0.76f }, PH[3] = { 0.20f, 0.13f, 0.26f };
    static const int PI3[3] = { 1, 0, 2 };
    for (int s = 0; s < 3; s++) {
        int place = PI3[s];
        float cx = FW * PX[s], top = FH * (0.70f + PH[s] - 0.13f);
        show_anim(ord[place], place == 0 ? 0 : 3, t, cx, top - FH * 0.13f, FH * 1.2f, fsin(t * 1.5f + s) * 0.3f);
        rect(cx - FW * 0.09f, top, cx + FW * 0.09f, FH - 1, place == 0 ? pk(230, 190, 50) : (place == 1 ? pk(190, 195, 205) : pk(190, 120, 60)));
        snprintf(buf, sizeof buf, "%d  %d PTS", place + 1, pts[ord[place]]);
        text((int)cx - text_w(buf, 1) / 2, (int)top + 4, buf, 1, pk(40, 30, 30), false);
        text((int)cx - text_w(CHAR_NAME[ord[place]], 1) / 2, (int)top + 13, CHAR_NAME[ord[place]], 1, pk(40, 30, 30), false);
    }
    /* o 4o lugar, no canto: cabeca baixa... ou explode, se for o Bilu */
    {
        int c4 = ord[3];
        float cx = FW * 0.11f, cy = FH - 14 - FH * 0.11f;
        if (c4 == 3 && t > 1.0f) {
            float tb = t - 1.0f;
            show_anim(3, 2, tb, cx, cy, FH * 1.0f, 0);
            if (tb < 0.15f) disc(cx, cy - FH * 0.15f, FH * (0.12f + tb * 2.0f), pk(240, 255, 220));
            if (tb > 0.1f) goo(tb - 0.1f);
            if (fin_fx == 0) { fin_fx = 1; lap_vibrate(350, 100); sfx_bump(); sfx_hit(); }
        } else show_anim(c4, 1, t, cx, cy, FH * 1.0f, 0);
        snprintf(buf, sizeof buf, "4 %s %d PTS", CHAR_NAME[c4], pts[c4]);
        text(4, FH - 10, buf, 1, pk(220, 220, 230), true);
    }
    confetti(t);
    draw_buttons();
    if (t > 2.0f) text_c(28, "TOQUE PARA VOLTAR", 1, pk(255, 200, 120));
}

static void draw_finale(uint32_t now)
{
    float t = (now - t_state) / 1000.0f;
    for (int y = 0; y < FH; y++) {                     /* escurece a pista */
        uint16_t *p = fb + y * FW;
        for (int x = 0; x < FW; x++) p[x] = (uint16_t)((p[x] >> 1) & 0x7BEF);
    }
    bool won = fin_place == 1;
    bool fem = my_char == 0 || my_char == 2;
    bool boom = !won && my_char == 3;                  /* Bilu perdeu: explode */
    float cx = FW * 0.73f, cy = FH * 0.66f, sc = FH * 2.7f;
    char buf[40];
    /* textos a esquerda */
    int lx = 8, ty = 34;
    const char *title = won ? (fem ? "CAMPEA!" : "CAMPEAO!") : "CHEGADA";
    int tsc = text_w(title, 3) < FW / 2 ? 3 : 2;
    text(lx, ty, title, tsc, won ? pk(255, 214, 40) : pk(230, 230, 240), true);
    if (!champ_on) {
        snprintf(buf, sizeof buf, "POSICAO %d/4", fin_place);
        text(lx, ty + 8 * tsc + 6, buf, 2, pk(255, 255, 255), true);
        snprintf(buf, sizeof buf, "TEMPO %u:%02u.%u", (unsigned)(fin_time / 60000u), (unsigned)((fin_time / 1000u) % 60u), (unsigned)((fin_time / 100u) % 10u));
        text(lx, ty + 8 * tsc + 24, buf, 2, pk(255, 255, 255), true);
        text(lx, ty + 8 * tsc + 42, CHAR_NAME[my_char], 2, pk(255, 200, 120), true);
    } else {                                           /* campeonato: tabela de pontos */
        int pts[NCHAR];
        uint8_t ord[NCHAR], rk[4];
        champ_table(pts, ord);
        race_rank(rk);
        snprintf(buf, sizeof buf, "CORRIDA %d/4  TEMPO %u:%02u.%u", champ_idx + 1, (unsigned)(fin_time / 60000u), (unsigned)((fin_time / 1000u) % 60u), (unsigned)((fin_time / 100u) % 10u));
        text(lx, ty + 8 * tsc + 5, buf, 1, pk(255, 255, 255), true);
        for (int i = 0; i < NCHAR; i++) {
            int c = ord[i], got = 0;
            for (int q = 0; q < 4; q++) if (rk[q] == c) got = CHAMP_PTS[q];
            snprintf(buf, sizeof buf, "%d %s %d PTS", i + 1, CHAR_NAME[c], pts[c]);
            if (got) {                                  /* o que ganhou agora, em verde */
                char gb[8];
                snprintf(gb, sizeof gb, "%d", got);
                int gx = lx + text_w(buf, 1) + 6;
                rect(gx, ty + 8 * tsc + 20 + i * 10, gx + 2, ty + 8 * tsc + 20 + i * 10, pk(120, 255, 140));
                rect(gx + 1, ty + 8 * tsc + 19 + i * 10, gx + 1, ty + 8 * tsc + 21 + i * 10, pk(120, 255, 140));
                text(gx + 4, ty + 8 * tsc + 17 + i * 10, gb, 1, pk(120, 255, 140), true);
            }
            text(lx, ty + 8 * tsc + 17 + i * 10, buf, 1, c == my_char ? pk(255, 214, 40) : pk(230, 230, 240), true);
        }
    }
    if (new_rec && ((int)(t * 4) & 1)) text(lx, FH - 24, "NOVO RECORDE!", 2, pk(120, 255, 140), true);
    /* o piloto, de frente */
    if (!boom) {
        float yaw = won ? fsin(t * 2.0f) * 0.35f : fsin(t * 0.8f) * 0.15f;
        sh_cx = cx; sh_cy = cy; sh_sc = sc;
        sh_sy = fsin(yaw); sh_cyw = fsin(yaw + PI_F * 0.5f);
        float cz = 2600.0f * 0.9394f, cyv = 300.0f + 2600.0f * 0.3429f;
        draw_model(animate(my_char, won ? 0 : 1, t), proj_show, -cz * sh_sy, cyv, cz * sh_cyw,
                   -0.4f * sh_cyw - 0.75f * sh_sy, 0.55f, -0.4f * sh_sy + 0.75f * sh_cyw, 0);
        if (won)                                       /* confete */
            for (int i = 0; i < 40; i++) {
                uint32_t h = hxy((uint32_t)i, 555);
                float x = (float)(h % (uint32_t)FW), y = fmodf((float)((h >> 9) % 200) + t * (30.0f + (h >> 20) % 30), (float)FH);
                static const uint8_t CC[5][3] = { {255, 214, 40}, {255, 80, 120}, {80, 200, 255}, {120, 230, 90}, {255, 255, 255} };
                const uint8_t *c = CC[i % 5];
                rect(x, y, x + 1, y + 2, pk(c[0], c[1], c[2]));
            }
    } else {
        float tb = t - 0.6f;                           /* treme, pisca, explode */
        sh_cx = cx; sh_cy = cy; sh_sc = sc;
        float yaw = tb < 0 ? fsin(t * 40.0f) * 0.12f : 0.0f;
        sh_sy = fsin(yaw); sh_cyw = fsin(yaw + PI_F * 0.5f);
        float cz = 2600.0f * 0.9394f, cyv = 300.0f + 2600.0f * 0.3429f;
        draw_model(animate(3, tb < 0 ? 1 : 2, tb < 0 ? t : tb), proj_show, -cz * sh_sy, cyv, cz * sh_cyw,
                   -0.4f * sh_cyw - 0.75f * sh_sy, 0.55f, -0.4f * sh_sy + 0.75f * sh_cyw, 0);
        if (tb >= 0 && tb < 0.18f) disc(cx, cy - FH * 0.25f, FH * (0.2f + tb * 3.0f), pk(240, 255, 220));   /* clarao */
        if (tb > 0.1f) goo(tb - 0.1f);
    }
    draw_buttons();
    if (t > 1.6f) text_c(FH - 10, champ_on ? (champ_idx < 3 ? "TOQUE PARA A PROXIMA PISTA" : "TOQUE PARA VER O PODIO") : "TOQUE PARA ESCOLHER A PISTA", 1, pk(255, 200, 120));
}

/* tela de escolha do piloto: os 4 girando, toque em um */
static void draw_char_menu(uint32_t now)
{
    for (int y = 0; y < FH; y++) hspan(y, 0, FW - 1, pk(30 + y / 4, 20 + y / 6, 60 + y / 3));
    shout_line("ROLIMA", FW / 2, (int)(FH * 0.08f), FH / 34 + 1, now / 1000.0f, 0);
    text_c((int)(FH * 0.35f), "ESCOLHA O PILOTO", 1, pk(255, 230, 180));
    int cw = (FW - 30) / 4, y0 = (int)(FH * 0.42f), chh = (int)(FH * 0.55f);
    for (int c = 0; c < NCHAR; c++) {
        int x0 = 6 + c * (cw + 6);
        for (int y = y0; y < y0 + chh && y < FH; y++)
            hspan(y, x0, x0 + cw - 1, c == my_char ? pk(70, 55, 110) : pk(48, 38, 80));
        /* chaozinho */
        for (int y = y0 + chh * 70 / 100; y < y0 + chh && y < FH; y++)
            hspan(y, x0, x0 + cw - 1, c == my_char ? pk(95, 80, 140) : pk(62, 50, 100));
        float yaw = now / 1000.0f * 0.9f + c * 1.3f;
        draw_showcase(c, x0 + cw * 0.5f, y0 + chh * 0.50f, cw * 3.3f, yaw);
        if (c == my_char) {
            rect(x0, y0, x0 + cw - 1, y0 + 1, pk(255, 214, 40));
            rect(x0, y0 + chh - 2, x0 + cw - 1, y0 + chh - 1, pk(255, 214, 40));
            rect(x0, y0, x0 + 1, y0 + chh - 1, pk(255, 214, 40));
            rect(x0 + cw - 2, y0, x0 + cw - 1, y0 + chh - 1, pk(255, 214, 40));
        }
        const char *nm = CHAR_NAME[c];
        int sc = 2;                                           /* mesmo tamanho para todos os nomes */
        for (int q = 0; q < NCHAR; q++) if (text_w(CHAR_NAME[q], 2) > cw - 4) sc = 1;
        text(x0 + (cw - text_w(nm, sc)) / 2, y0 + chh - 8 * sc - 3, nm, sc, pk(255, 255, 255), true);
    }
    draw_buttons();
}

static void render(uint32_t now)
{
    if (state == ST_CHAR) { draw_char_menu(now); return; }
    if (state == ST_MENU) { draw_menu(now); return; }
    if (state == ST_PODIUM) { draw_podium(now); return; }
    g_now_s = now / 1000.0f;
    memcpy(fb, bgimg, sizeof(uint16_t) * FW * FH);
    if (trk == 0) draw_kites(now / 1000.0f);

    camZ = pl.z - PLAYER_DZ;
    camX = pl.x * RW;
    camY = road_y(pl.z) + CAM_H;
    project();
    /* fantasma: a melhor descida gravada, no mesmo tempo de corrida */
    racer_t gh;
    bool gh_on = false;
    if (state == ST_RACE && g_best.n > 1) {
        float ft = (now - t_race) / 1000.0f / G_DT;
        int gi = (int)ft;
        if (gi < g_best.n - 1) {
            float fr = ft - gi;
            const gsamp_t *a = &g_best.s[gi], *b = &g_best.s[gi + 1];
            memset(&gh, 0, sizeof gh);
            gh.z = a->z + (b->z - a->z) * fr;
            gh.x = (a->x + (b->x - a->x) * fr) / 1000.0f;
            gh.hy = a->hy + (b->hy - a->hy) * fr;
            gh.ch = (uint8_t)my_char;
            gh_on = gh.z > camZ + 300;
        }
    }

    /* casas que comecam (ou ja estao) em cada segmento */
    static int16_t head[DRAW_N + 2], nxt[HMAX];
    for (int k = 0; k <= DRAW_N; k++) head[k] = -1;
    for (int i = lower_house(base - 32); i < nh && hs[i].a - base <= DRAW_N - 1; i++) {   /* so a janela visivel */
        int k0 = hs[i].a - base;
        if (hs[i].a + hs[i].len - base < 2) continue;
        if (k0 < 1) k0 = 1;
        nxt[i] = head[k0];
        head[k0] = (int16_t)i;
    }
    wire_n[0] = wire_n[1] = 0;
    gl_n = 0;
    /* objetos do sitio (pista 2) */
    static int16_t phead[DRAW_N + 2], pnxt[PMAX];
    for (int k = 0; k <= DRAW_N; k++) phead[k] = -1;
    int p0 = 0, p1 = np;                                      /* busca binaria: objetos a partir de base-42 */
    while (p0 < p1) { int m = (p0 + p1) / 2; if (pr[m].a < base - 42) p0 = m + 1; else p1 = m; }
    for (int i = p0; i < np && pr[i].a - base <= DRAW_N - 1; i++) {
        int k0 = pr[i].a - base;
        int reach = pr[i].type == PR_FARM ? 22 : (pr[i].type == PR_STAND ? 40 : 6);
        if (k0 > DRAW_N - 1 || k0 + reach < 1) continue;
        if (k0 < 1) { if (pr[i].type != PR_FARM && pr[i].type != PR_POST && pr[i].type != PR_STAND) continue; k0 = 1; }
        pnxt[i] = phead[k0];
        phead[k0] = (int16_t)i;
    }

    for (int k = DRAW_N - 1; k >= 1; k--) {
        int idx = base + k;
        if (idx >= nseg) continue;
        if (bok[k] && bok[k + 1] && by[k + 1] < by[k]) {
            float x1 = bx[k], y1 = by[k], s1 = bsc[k] * HALFW * RW;
            float x2 = bx[k + 1], y2 = by[k + 1], s2 = bsc[k + 1] * HALFW * RW;
            float f = fogk(k);
            int alt = (idx / 3) & 1;
            float ka = alt ? 1.0f : 0.94f;
            int ys = (int)floorf(y2) + 1, ye = (int)floorf(y1);
            if (trk == 3) {                                   /* pista profissional */
                uint16_t cg = shade((rgb_t){95, 160, 70}, ka, f);
                for (int y = ys < 0 ? 0 : ys; y <= ye && y < FH; y++) hspan(y, 0, FW - 1, cg);
                trap(x1, y1, s1 * 1.25f, x2, y2, s2 * 1.25f, shade((rgb_t){120, 120, 115}, 1.0f, f));   /* acostamento */
                bool hot = fabsf(curv[idx]) > 3.0f;
                if (hot)                                        /* zebra nas curvas e chicanes */
                    trap(x1, y1, s1 * 1.07f, x2, y2, s2 * 1.07f, shade(alt ? (rgb_t){220, 30, 30} : (rgb_t){245, 245, 245}, 1, f));
                if (sflag[idx] == 3)
                    trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){245, 245, 245}, 1, f), shade((rgb_t){20, 20, 20}, 1, f), 16, idx & 1);
                else {
                    trap(x1, y1, s1, x2, y2, s2, shade((rgb_t){82, 84, 92}, alt ? 1.0f : 0.97f, f));
                    if (sflag[idx] == 2)
                        trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){245, 245, 245}, 1, f), shade((rgb_t){20, 20, 20}, 1, f), 16, idx & 1);
                    for (int sd = -1; sd <= 1; sd += 2)            /* faixas brancas de borda */
                        trap(x1 + sd * s1 * 0.93f, y1, s1 * 0.022f, x2 + sd * s2 * 0.93f, y2, s2 * 0.022f, shade((rgb_t){240, 240, 240}, 1, f));
                    if ((idx / 4) & 1)                          /* faixa do meio tracejada */
                        trap(x1, y1, s1 * 0.018f, x2, y2, s2 * 0.018f, shade((rgb_t){250, 210, 40}, 1, f));
                }
            } else
            if (trk == 2) {                                   /* avenida: calcada, asfalto escuro, faixas */
                uint16_t cg = shade((rgb_t){120, 115, 115}, ka, f);
                for (int y = ys < 0 ? 0 : ys; y <= ye && y < FH; y++) hspan(y, 0, FW - 1, cg);
                trap(x1, y1, s1 * 1.75f, x2, y2, s2 * 1.75f, shade((rgb_t){165, 155, 150}, alt ? 1.0f : 0.95f, f));
                trap(x1, y1, s1 * 1.05f, x2, y2, s2 * 1.05f, shade((rgb_t){185, 180, 175}, 1.0f, f));
                int lm = idx % 20;                             /* poca de luz dos postes */
                float pool = (lm <= 2 || lm >= 18) ? 1.0f : 0.0f;
                rgb_t asph = { (uint8_t)(70 + 45 * pool), (uint8_t)(68 + 30 * pool), (uint8_t)(80 + 10 * pool) };
                if (sflag[idx] == 4)
                    trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){235, 235, 230}, 1, f), shade(asph, 1, f), 22, 0);
                else if (sflag[idx] == 3)
                    trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){245, 245, 245}, 1, f), shade((rgb_t){20, 20, 20}, 1, f), 16, idx & 1);
                else {
                    trap(x1, y1, s1, x2, y2, s2, shade(asph, alt ? 1.0f : 0.97f, f));
                    if (sflag[idx] == 2) trap(x1, y1, s1 * 0.98f, x2, y2, s2 * 0.98f, shade((rgb_t){240, 240, 240}, 1, f));
                    for (int sd = -1; sd <= 1; sd += 2)            /* faixa amarela dupla no meio */
                        trap(x1 + sd * s1 * 0.025f, y1, s1 * 0.008f, x2 + sd * s2 * 0.025f, y2, s2 * 0.008f, shade((rgb_t){250, 200, 40}, 1, f));
                    if (alt)                                    /* faixas brancas tracejadas */
                        for (int sd = -1; sd <= 1; sd += 2)
                            trap(x1 + sd * s1 * 0.5f, y1, s1 * 0.012f, x2 + sd * s2 * 0.5f, y2, s2 * 0.012f, shade((rgb_t){235, 235, 235}, 1, f));
                }
            } else
            if (trk == 1) {                                   /* estrada de terra no pasto */
                uint16_t cg = shade((rgb_t){92, 150, 62}, ka, f);
                for (int y = ys < 0 ? 0 : ys; y <= ye && y < FH; y++) hspan(y, 0, FW - 1, cg);
                trap(x1, y1, s1 * 1.5f, x2, y2, s2 * 1.5f, shade((rgb_t){118, 160, 72}, alt ? 1.0f : 0.95f, f));
                trap(x1, y1, s1 * 1.12f, x2, y2, s2 * 1.12f, shade((rgb_t){140, 104, 66}, 1.0f, f));
                uint32_t hh = hxy((uint32_t)idx, 7);
                trap(x1, y1, s1, x2, y2, s2, shade((rgb_t){172, 126, 84}, 0.93f + (hh & 15) / 160.0f, f));
                for (int sd = -1; sd <= 1; sd += 2)            /* trilhas de pneu */
                    trap(x1 + sd * s1 * 0.45f, y1, s1 * 0.11f, x2 + sd * s2 * 0.45f, y2, s2 * 0.11f, shade((rgb_t){150, 106, 70}, alt ? 1.0f : 0.95f, f));
                if (sflag[idx] == 1) {                         /* buraco com pedras */
                    trap(x1 + s1 * 0.1f, y1, s1 * 0.42f, x2 + s2 * 0.1f, y2, s2 * 0.42f, shade((rgb_t){95, 66, 42}, 1, f));
                    trap(x1 - s1 * 0.5f, y1, s1 * 0.12f, x2 - s2 * 0.5f, y2, s2 * 0.12f, shade((rgb_t){150, 145, 140}, 1, f));
                } else if (sflag[idx] == 3)
                    trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){245, 245, 245}, 1, f), shade((rgb_t){20, 20, 20}, 1, f), 16, idx & 1);
                else if (sflag[idx] == 2)
                    trap(x1, y1, s1 * 0.98f, x2, y2, s2 * 0.98f, shade((rgb_t){240, 240, 240}, 1, f));
            } else {
            uint16_t cg = shade(C_GROUND, ka, f);
            for (int y = ys < 0 ? 0 : ys; y <= ye && y < FH; y++) hspan(y, 0, FW - 1, cg);
            trap(x1, y1, s1 * 1.7f, x2, y2, s2 * 1.7f, shade(C_WALK, alt ? 1.0f : 0.95f, f));
            trap(x1, y1, s1 * 1.12f, x2, y2, s2 * 1.12f, shade(alt ? C_CURB_A : C_CURB_B, 1.0f, f));
            if (sflag[idx] == 1)
                trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){250, 205, 40}, 1, f), shade((rgb_t){30, 30, 30}, 1, f), 12, 0);
            else if (sflag[idx] == 3)
                trap_stripes(x1, y1, s1, x2, y2, s2, shade((rgb_t){245, 245, 245}, 1, f), shade((rgb_t){20, 20, 20}, 1, f), 16, idx & 1);
            else {
                trap(x1, y1, s1, x2, y2, s2, shade(C_ASPH, alt ? 1.0f : 0.96f, f));
                if (sflag[idx] == 2) trap(x1, y1, s1 * 0.98f, x2, y2, s2 * 0.98f, shade((rgb_t){240, 240, 240}, 1, f));
                else if (alt) trap(x1, y1, s1 * 0.03f, x2, y2, s2 * 0.03f, shade(C_LANE, 1.0f, f));
            }
            }
        }
        /* sitio */
        for (int i = phead[k]; i >= 0; i = pnxt[i]) { if (trk == 3) draw_prop_pro(&pr[i], k); else draw_prop(&pr[i], k); }
        /* casas */
        for (int i = head[k]; i >= 0; i = nxt[i]) draw_house(&hs[i], k);
        /* postes */
        if (trk == 0 && idx % POLE_EVERY == 0 && bok[k] && bok[k + 1]) {
            int side = (idx / POLE_EVERY) & 1;
            float u = side ? RW * 1.25f : -RW * 1.25f;
            float f = fogk(k);
            face(k, k, u - 45, u + 45, 0, 4500, shade(C_POLE, 0.9f, f));
            float tx, ty, ax, ay;
            P(k, u, 4400, &tx, &ty);
            P(k, u * 0.78f, 4500, &ax, &ay);
            line(tx, ty, ax, ay, shade(C_POLE, 0.7f, f));
            if (wire_n[side] < 8) { wire_x[side][wire_n[side]] = tx; wire_y[side][wire_n[side]] = ty; wire_n[side]++; }
        }
        /* pista profissional: guard rail dos dois lados (lamina e postes) */
        if (trk == 3 && bok[k] && bok[k + 1]) {
            float f = fogk(k);
            for (int sd = -1; sd <= 1; sd += 2) {
                float u = sd * RW * 1.12f;
                if (idx % 4 == 0) box3(k, k + 1, u - 50 * sd - 40, u - 50 * sd + 40, 0, 600, (rgb_t){90, 90, 98}, true);
                face(k, k + 1, u, u, 330, 640, shade((rgb_t){185, 190, 198}, sd < 0 ? 1.1f : 0.85f, f));
                face(k, k + 1, u, u, 460, 510, shade((rgb_t){120, 125, 135}, 1, f));
            }
        }
        /* Consolacao: postes acesos dos dois lados e carros vindo */
        if (trk == 2 && idx % 20 == 0 && bok[k] && bok[k + 1]) {
            float f = fogk(k);
            for (int sd = -1; sd <= 1; sd += 2) {
                float u = sd * RW * 1.2f;
                face(k, k, u - 55, u + 55, 0, 4800, shade((rgb_t){70, 70, 78}, 0.9f, f));
                face(k, k, u * 0.8f, u, 4700, 4800, shade((rgb_t){70, 70, 78}, 0.9f, f));
                face(k, k, u * 0.8f - 120, u * 0.8f + 120, 4550, 4700, pk(255, 220, 150));
                float gx, gy;
                P(k, u * 0.8f, 4600, &gx, &gy);
                glow_add(gx, gy, bsc[k] * HALFW * 1500, 0);
            }
        }
        if (trk == 2)
            for (int i = 0; i < NCARS; i++)
                if ((int)(cars[i].z / SEG) - base == k) draw_car(&cars[i], k);
        /* faixa de chegada */
        if (idx == fin_seg && bok[k]) {
            float f = fogk(k);
            face(k, k, -RW * 1.3f - 60, -RW * 1.3f + 60, 0, 3400, shade(C_POLE, 0.9f, f));
            face(k, k, RW * 1.3f - 60, RW * 1.3f + 60, 0, 3400, shade(C_POLE, 0.9f, f));
            face(k, k, -RW * 1.3f, RW * 1.3f, 2700, 3400, shade((rgb_t){210, 40, 50}, 1.0f, f));
            float bxl, byt, bxr, byb;
            P(k, -RW * 1.3f, 3400, &bxl, &byt);
            P(k, RW * 1.3f, 2700, &bxr, &byb);
            int sc = (int)((byb - byt) / 9.0f);
            if (sc >= 1) {
                if (sc > 4) sc = 4;
                int tw = text_w("CHEGADA", sc);
                text((int)((bxl + bxr) * 0.5f) - tw / 2, (int)(byt + (byb - byt - 7 * sc) * 0.5f), "CHEGADA", sc, pk(255, 255, 255), false);
            }
        }
        /* bichos atravessando */
        for (int i = 0; i < NCRIT; i++)
            if (crit[i].active && (int)(crit[i].z / SEG) - base == k) draw_critter(&crit[i], k);
        /* fantasma a frente */
        if (gh_on && gh.z > pl.z && (int)(gh.z / SEG) - base == k) {
            g_blend = true; draw_cart3d(&gh, 0, 0, 0, false); g_blend = false;
        }
        /* adversarios a frente do jogador */
        for (int i = 0; i < NRIV; i++) {
            racer_t *r = &rv[i];
            if (r->z <= pl.z) continue;
            int ri = (int)(r->z / SEG);
            if (ri - base != k || !bok[k] || !bok[k + 1]) continue;
            draw_cart3d(r, clampf(r->tx - r->x, -0.3f, 0.3f), 0, fogk(k), false);
        }
    }
    /* fios entre os postes */
    for (int sd = 0; sd < 2; sd++)
        for (int i = 0; i + 1 < wire_n[sd]; i++) {
            float x0 = wire_x[sd][i], y0 = wire_y[sd][i], x1 = wire_x[sd][i + 1], y1 = wire_y[sd][i + 1];
            float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f + 2 + fabsf(x1 - x0) * 0.08f;
            line(x0, y0, mx, my, pk(40, 30, 40));
            line(mx, my, x1, y1, pk(40, 30, 40));
        }

    for (int i = 0; i < gl_n; i++)
        add_glow(gl_x[i], gl_y[i], gl_r[i], gl_kind[i] ? 150 : 170, gl_kind[i] ? 140 : 100, gl_kind[i] ? 110 : 40);
    /* o jogador */
    float shake = (fabsf(pl.x) > 1.0f && pl.v > 500) ? (float)(lap_random() % 30) : 0.0f;   /* trepida na calcada */
    draw_cart3d(&pl, steer * 0.22f, shake, 0, true);
    if (steer != 0 && pl.v > VMAX * 0.5f) {                                           /* faiscas dos rolamentos */
        float wx, wy;
        if (proj3(pl.x * RW - steer * 235.0f, 10, pl.z - 300, &wx, &wy))
            for (int i = 0; i < 10; i++)
                pset((int)wx + (int)(lap_random() % 9) - 4 - (int)steer * (int)(lap_random() % 6),
                     (int)wy - (int)(lap_random() % 5), (i & 1) ? pk(255, 240, 120) : pk(255, 150, 40));
    }
    if (gh_on && gh.z <= pl.z) { g_blend = true; draw_cart3d(&gh, 0, 0, 0, false); g_blend = false; }
    /* turbo: riscos de velocidade nas bordas */
    if (turbo_t > 0) {
        for (int i = 0; i < 18; i++) {
            uint32_t h = lap_random();
            float a = (float)(h & 1023) / 1023.0f * 2 * PI_F;
            float r0 = FW * (0.35f + (float)((h >> 10) & 63) / 400.0f), r1 = r0 + FW * 0.12f;
            float cx = HALFW, cy = HOR + 10;
            float ca = fsin(a + PI_F * 0.5f), sa = fsin(a) * 0.6f;
            line(cx + ca * r0, cy + sa * r0, cx + ca * r1, cy + sa * r1, pk(255, 255, 255));
        }
    }
    /* adversarios entre a camera e o jogador: do mais longe ao mais perto */
    {
        racer_t *near[NRIV];
        int nn = 0;
        for (int i = 0; i < NRIV; i++)
            if (rv[i].z <= pl.z && rv[i].z > camZ + 300) {
                int j = nn++;
                while (j > 0 && near[j - 1]->z < rv[i].z) { near[j] = near[j - 1]; j--; }
                near[j] = &rv[i];
            }
        for (int i = 0; i < nn; i++) draw_cart3d(near[i], 0, 0, 0, false);
    }

    if (shout_on) {
        float st = (now - shout_t0) / 1000.0f;
        if (st < 1.6f) draw_shout(st); else shout_on = false;
    }
    char buf[32];
    if (state == ST_FINISH) { draw_finale(now); return; }   /* tela do final cobre o placar */
    draw_buttons();
    int place = 1;
    for (int i = 0; i < NRIV; i++) if (rv[i].z > pl.z) place++;
    if (state == ST_FINISH) place = fin_place;
    snprintf(buf, sizeof buf, "%d/4", place);
    text(62, 6, buf, 3, pk(255, 210, 60), true);
    uint32_t ms = state == ST_COUNT ? 0 : (state == ST_RACE ? now - t_race : fin_time);
    snprintf(buf, sizeof buf, "%u:%02u.%u", (unsigned)(ms / 60000u), (unsigned)((ms / 1000u) % 60u), (unsigned)((ms / 100u) % 10u));
    text(FW - 6 - text_w(buf, 2), 7, buf, 2, pk(255, 255, 255), true);
    /* quanto falta: barrinha embaixo do tempo */
    float prog = clampf((pl.z - 2000.0f) / (fin_z - 2000.0f), 0, 1);
    rect(FW - 6 - text_w(buf, 2), 24, FW - 7, 25, pk(40, 30, 50));
    rect(FW - 6 - text_w(buf, 2), 24, FW - 6 - text_w(buf, 2) + (text_w(buf, 2) - 1) * prog, 25, pk(255, 210, 60));
    if (best_ms[trk]) {                                       /* recorde da pista */
        snprintf(buf, sizeof buf, "REC %u:%02u.%u", (unsigned)(best_ms[trk] / 60000u), (unsigned)((best_ms[trk] / 1000u) % 60u), (unsigned)((best_ms[trk] / 100u) % 10u));
        text(FW - 6 - text_w(buf, 1), 28, buf, 1, pk(255, 214, 40), true);
    }
    /* barra do vacuo (enche atras de alguem; cheia = TURBO) */
    text(FW - 90, FH - 39, turbo_t > 0 ? "TURBO" : "VACUO", 1, turbo_t > 0 ? pk(255, 120, 60) : pk(140, 230, 255), true);
    rect(FW - 58, FH - 37, FW - 7, FH - 34, pk(40, 30, 50));
    rect(FW - 58, FH - 37, FW - 58 + 51 * (turbo_t > 0 ? turbo_t / 2.0f : vac), FH - 34, turbo_t > 0 ? pk(255, 120, 60) : pk(140, 230, 255));
    if (turbo_t > 1.3f) text_c(FH / 5, "TURBO!", 3, pk(255, 140, 60));
    int kmh = (int)(pl.v * 0.0072f);
    snprintf(buf, sizeof buf, "%d KM/H", kmh);
    text(FW - 6 - text_w(buf, 2), FH - 18, buf, 2, pk(255, 255, 255), true);
    float fr = clampf(pl.v / VMAX, 0, 1);
    rect(FW - 90, FH - 25, FW - 7, FH - 22, pk(40, 30, 50));
    rect(FW - 90, FH - 25, FW - 90 + 83 * fr, FH - 22, fr > 0.75f ? pk(255, 80, 60) : pk(80, 195, 255));

    /* mensagens */
    if (state == ST_COUNT) {
        int n = 3 - (int)((now - t_state) / 1000u);
        if (n < 1) n = 1;
        buf[0] = (char)('0' + n); buf[1] = 0;
        text_c(FH / 2 - 30, buf, 7, pk(255, 230, 80));
    } else if (state == ST_RACE && now - t_race < 900u && trk == 0) {
        text_c(FH / 2 - 26, "VAI!", 6, pk(120, 255, 120));
    }
}

/* ======================================================================
   app
   ====================================================================== */
static bool orient_locked, immersive_on, screen_cleared;
static int scr_w = 1, scr_h = 1;          /* tela em pixels */

/* tamanho da imagem: a tela dividida pelo menor inteiro que caiba */
static void pick_size(void)
{
    int cw = 1, chh = 1;
    lap_cell_px(&cw, &chh);
    scr_w = lap_cols() * cw; scr_h = lap_rows() * chh;
    if (scr_w < 1) scr_w = 1;
    if (scr_h < 1) scr_h = 1;
    int best = 0;
    for (int s = 1; s <= 12 && !best; s++)                /* primeiro tenta divisao exata */
        if (scr_w % s == 0 && scr_h % s == 0 && (scr_w / s) * (scr_h / s) <= 72000 && scr_w / s <= FW_MAX && scr_h / s <= FH_MAX)
            best = s;
    for (int s = 1; s <= 12 && !best; s++)
        if ((scr_w / s) * (scr_h / s) <= 72000 && scr_w / s <= FW_MAX && scr_h / s <= FH_MAX) best = s;
    if (!best) best = 12;
    int nw = scr_w / best, nh = scr_h / best;
    if (nw < 160) nw = 160;
    if (nh < 90) nh = 90;
    if (nw > FW_MAX) nw = FW_MAX;
    if (nh > FH_MAX) nh = FH_MAX;
    if (nw != FW || nh != FH || !bg_ready) {
        FW = nw; FH = nh;
        HALFW = FW * 0.5f;
        HOR = FH * 0.30f;
        if (trk == 1) build_bg_farm(); else if (trk == 2) build_bg_city(); else if (trk == 3) build_bg_mountain(); else build_bg();
        bg_ready = true;
    }
}
/* ponto da tela (pixels) -> pixel da imagem */
static void scr_to_img(int sx, int sy, int *ix, int *iy) { *ix = sx * FW / scr_w; *iy = sy * FH / scr_h; }
static bool in_buttons(int ix, int iy) { return iy < 32 && ix < 60; }

static bool orientation_ok(void)
{
    int cw = 1, chh = 1;
    lap_cell_px(&cw, &chh);
    bool landscape = lap_cols() * cw > lap_rows() * chh;
    if (landscape != (bool)WANT_LANDSCAPE) {
        if (orient_locked) { lap_hold_rotation(false); orient_locked = false; }
        lap_clear(LAP_ATTR(LAP_WHITE, LAP_BLACK));
        screen_cleared = false;
        const char *t = WANT_LANDSCAPE ? "Gire para PAISAGEM" : "Gire para RETRATO";
        lap_put_str((lap_cols() - (int)strlen(t)) / 2, lap_rows() / 2, t, LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLACK));
        return false;
    }
    if (!orient_locked) { lap_hold_rotation(true); orient_locked = true; }
    return true;
}

/* escolhe a pista: monta a estrada, o cenario e o fundo */
static void start_track(int t)
{
    if (t != trk) {                                       /* troca a musica na hora */
        memset(&sy, 0, sizeof sy);
        memset(&bjs, 0, sizeof bjs);
        memset(&pks, 0, sizeof pks);
        memset(&sfs, 0, sizeof sfs);
        memset(&pm_mus, 0, sizeof pm_mus);
        if (sound_on) lap_mixer_clear(LAP_MIX_MUSIC);
    }
    trk = t;
    build_track();
    nh = 0; np = 0;
    RW = trk == 2 ? 3400.0f : 2000.0f;
    if (trk == 1) { build_props(); FOGC = (rgb_t){205, 225, 235}; }
    else if (trk == 2) { build_buildings(); FOGC = (rgb_t){185, 95, 105}; }
    else if (trk == 3) { build_props_pro(); FOGC = (rgb_t){190, 215, 235}; }
    else { build_houses(); FOGC = (rgb_t){240, 150, 120}; }
    qsort(hs, (size_t)nh, sizeof hs[0], cmp_house);            /* ordenado por segmento, uma vez */
    qsort(pr, (size_t)np, sizeof pr[0], cmp_prop);
    bg_ready = false;
    load_ghost(trk);
    restart();
}

static void enter(void)
{
    fb = (uint16_t *)malloc(sizeof(uint16_t) * FW_MAX * FH_MAX);
    bgimg = (uint16_t *)malloc(sizeof(uint16_t) * FW_MAX * FH_MAX);
    mem_fail = !fb || !bgimg;
    if (mem_fail) return;
    audio_init();
    sin_init();
    glyph_init();
    fog_init();
    load_records();
    build_models();
    start_track(0);
    state = ST_CHAR;
    t_last = lap_millis();
}

static void leave(void)
{
    if (orient_locked) { lap_hold_rotation(false); orient_locked = false; }
    if (immersive_on) { lap_immersive(false); immersive_on = false; }
    lap_keep_awake(false);
    lap_mixer_clear(LAP_MIX_MUSIC);
    lap_mixer_clear(LAP_MIX_EFFECTS);
    free(fb); fb = NULL;
    free(bgimg); bgimg = NULL;
}

/* medidor de desempenho: toque no relogio (canto de cima a direita) liga e desliga */
#define PHYS_DT (1.0f / 60.0f)
static float phys_acc;
static bool show_perf;
static uint32_t perf_t0, perf_frames, perf_draw_ms, perf_fps, perf_ms;
static void perf_tick(uint32_t now)
{
    perf_frames++;
    if (now - perf_t0 >= 1000u) {
        perf_fps = perf_frames * 1000u / (now - perf_t0);
        perf_ms = perf_frames ? perf_draw_ms / perf_frames : 0;
        perf_frames = 0; perf_draw_ms = 0; perf_t0 = now;
    }
}
static void draw_perf(void)
{
    char b[24];
    snprintf(b, sizeof b, "%u QPS %u MS", (unsigned)perf_fps, (unsigned)perf_ms);
    rect(FW - 6 - text_w(b, 1) - 2, 39, FW - 5, 48, pk(0, 0, 0));
    text(FW - 6 - text_w(b, 1), 40, b, 1, pk(120, 255, 120), false);
}

static void draw(void)
{
    if (mem_fail) {
        lap_clear(LAP_ATTR(LAP_WHITE, LAP_BLACK));
        lap_put_str(1, 1, "Sem memoria para o ROLIMA.", LAP_ATTR(LAP_RED | LAP_BRIGHT, LAP_BLACK));
        return;
    }
    if (!orientation_ok()) return;
    if (!immersive_on) { lap_immersive(true); lap_keep_awake(true); immersive_on = true; }

    uint32_t now = lap_millis();
    float dt = (now - t_last) / 1000.0f;
    t_last = now;
    if (dt > 0.25f) dt = 0.25f;                       /* travou (cartao, aviso): nao tenta recuperar tudo */
    perf_tick(now);

    /* entrada */
    int cw = 1, chh = 1;
    lap_cell_px(&cw, &chh);
    steer = 0; brake = false;
    lap_touch_t t;
    pick_size();
    if (lap_touch(&t) && t.down) {
        int ix, iy;
        scr_to_img(t.x, t.y, &ix, &iy);
        if (!in_buttons(ix, iy)) {
            if (t.x < scr_w * 0.38f) steer = -1;
            else if (t.x > scr_w * 0.62f) steer = 1;
            else brake = true;
        }
    }
    if (lap_key_down(LAP_KEY_LEFT)) steer = -1;
    if (lap_key_down(LAP_KEY_RIGHT)) steer = 1;
    if (lap_key_down(LAP_KEY_DOWN) || lap_key_down(LAP_KEY_CHAR(' '))) brake = true;

    /* fisica em passo fixo de 1/60 s, independente de quantos quadros a tela consegue */
    phys_acc += dt;
    int steps = 0;
    while (phys_acc >= PHYS_DT && steps < 15) { update(PHYS_DT, now); phys_acc -= PHYS_DT; steps++; }
    if (steps == 15) phys_acc = 0;
    /* ronco dos rolamentos: mais forte com a velocidade, aspero na calcada, mudo no ar */
    fx_roll_tgt = (pl.hy > 0 || pl.v < 200) ? 0.0f : 0.15f + 0.45f * pl.v / VMAX;
    fx_rough = fabsf(pl.x) > 1.0f ? 1.0f : 0.0f;
    audio_frame();
    uint32_t r0 = lap_millis();
    render(now);
    if (show_perf) draw_perf();
    perf_draw_ms += lap_millis() - r0;

    /* a imagem ocupa a tela inteira; as celulas embaixo so precisam ser limpas uma vez */
    if (!screen_cleared) { lap_clear(LAP_ATTR(LAP_WHITE, LAP_BLACK)); screen_cleared = true; }
    lap_image(0, 0, lap_cols(), lap_rows(), fb, FW, FH, ++serial);
}

static bool touch(int col, int row)
{
    int cw = 1, chh = 1, ix, iy;
    lap_cell_px(&cw, &chh);
    scr_to_img(col * cw + cw / 2, row * chh + chh / 2, &ix, &iy);
    if ((state == ST_RACE || state == ST_COUNT) && ix > FW - 90 && iy < 34) { show_perf = !show_perf; return true; }
    if (in_buttons(ix, iy)) {                             /* areas de toque maiores que o desenho */
        if (ix < 29) lap_exit();
        else audio_toggle();
        return true;
    }
    if (state == ST_CHAR) {
        if (iy > FH * 0.40f) { my_char = clampi(ix * NCHAR / FW, 0, NCHAR - 1); state = ST_MENU; }
        return true;
    }
    if (state == ST_MENU) {
        if (iy < FH * 0.40f) { state = ST_CHAR; return true; }   /* toque no titulo: trocar o piloto */
        if (iy > FH * 0.82f) {                                    /* botoes de baixo */
            if (ix < FW / 2) { champ_on = true; champ_idx = 0; memset(champ_pts, 0, sizeof champ_pts); start_track(0); }
            else { diff = (diff + 1) % 3; save_records(); }
            return true;
        }
        champ_on = false;
        start_track(clampi(ix * 4 / FW, 0, 3));
        return true;
    }
    if (state == ST_FINISH && lap_millis() - t_state > 1200u) {
        if (champ_on) champ_next(); else state = ST_MENU;
    }
    if (state == ST_PODIUM && lap_millis() - t_state > 2000u) { champ_on = false; state = ST_MENU; }
    return true;
}

static bool key(lap_key_t k, char ch)
{
    (void)ch;
    if (k == LAP_KEY_BACK) return false;
    if (state == ST_CHAR) {
        if (k == LAP_KEY_LEFT) my_char = (my_char + NCHAR - 1) % NCHAR;
        else if (k == LAP_KEY_RIGHT) my_char = (my_char + 1) % NCHAR;
        else if (k == LAP_KEY_ENTER) state = ST_MENU;
        return true;
    }
    if (state == ST_MENU && k == LAP_KEY_UP) { state = ST_CHAR; return true; }
    if (state == ST_MENU && k == LAP_KEY_CHAR && ch >= '1' && ch <= '4') { champ_on = false; start_track(ch - '1'); return true; }
    if (state == ST_MENU && k == LAP_KEY_CHAR && (ch == 'c' || ch == 'C')) { champ_on = true; champ_idx = 0; memset(champ_pts, 0, sizeof champ_pts); start_track(0); return true; }
    if (state == ST_MENU && k == LAP_KEY_CHAR && (ch == 'd' || ch == 'D')) { diff = (diff + 1) % 3; save_records(); return true; }
    if (state == ST_MENU && (k == LAP_KEY_LEFT || k == LAP_KEY_DOWN || k == LAP_KEY_RIGHT))
        champ_on = false, start_track(k == LAP_KEY_LEFT ? 0 : (k == LAP_KEY_DOWN ? 1 : 2));
    else if (k == LAP_KEY_ENTER && state == ST_FINISH) { if (champ_on) champ_next(); else state = ST_MENU; }
    else if (k == LAP_KEY_ENTER && state == ST_PODIUM) { champ_on = false; state = ST_MENU; }
    return true;
}

LAP_ICON_PX("....................",
            "........9999........",
            ".......999999.......",
            ".......99ff99.......",
            ".......999999.......",
            "........9999........",
            ".........33.........",
            "......11111111......",
            ".....1111111111.....",
            ".....1111ff1111.....",
            ".....1111111111.....",
            "....bbbbbbbbbbbb....",
            "...33333333333333...",
            "...11111111111111...",
            "..f88f........f88f..",
            "..8ff8........8ff8..",
            "..f88f........f88f..",
            "....................");

LAP_APP(.id = "rolima", .name = "ROLIMA", .sub = "ladeira abaixo",
        .version = "0.16", .author = "wmo586 & Claude AI",
        .icon = LAP_ICON_STAR, .color = LAP_YELLOW,
        .enter = enter, .leave = leave, .draw = draw, .key = key, .touch = touch);
