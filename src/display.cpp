#include "display.h"
#include "config.h"

namespace {
// ASCII face per mood — used only by the no-LCD fallback path.
const char* faceFor(BuddyMood m) {
  switch (m) {
    case BuddyMood::Focus:     return "( o_o )";
    case BuddyMood::Happy:     return "( ^_^ )";
    case BuddyMood::Sad:       return "( ;_; )";
    case BuddyMood::Surprised: return "( O_O )";
    case BuddyMood::Sleepy:    return "( -_- )";
    case BuddyMood::Confused:  return "( ?_? )";
    default:                   return "( . _ . )";
  }
}
}  // namespace

#if BUDDY_HAS_DISPLAY
// =====================================================================
//  ANIMATED LCD — ST7789V 240x320 via LovyanGFX, PSRAM sprite renderer
// =====================================================================
#include <LovyanGFX.hpp>
#include <math.h>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
 public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = BUDDY_TFT_SCK; c.pin_mosi = BUDDY_TFT_MOSI;
      c.pin_miso = -1; c.pin_dc = BUDDY_TFT_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = BUDDY_TFT_CS; c.pin_rst = BUDDY_TFT_RST; c.pin_busy = -1;
      c.panel_width = BUDDY_TFT_W; c.panel_height = BUDDY_TFT_H;
      c.readable = false; c.invert = true; c.rgb_order = false; c.bus_shared = false;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = BUDDY_TFT_BL; c.invert = false; c.freq = 12000; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

namespace {
LGFX gfx;
LGFX_Sprite canvas(&gfx);
bool useSprite = false;

// pet state mirrored in from main (Pet owns the simulation + NVS)
PetStats  g_pet{};
BuddyMood g_petMood = BuddyMood::Neutral;

// daily Claude usage (pushed from the PC via {"cmd":"usage",...})
bool     g_usageValid = false;
int      g_usagePct = 0;
int      g_blockPct = -1;     // 5-hour block usage vs block budget
char     g_usageStr[24]  = {0};   // "$12.3 today"
char     g_usageStr2[24] = {0};   // "17.0M tokens"

// multi-session world roster (pushed from PC via {"cmd":"world",...})
WorldRoom g_rooms[6];
int       g_numRooms = 0;
uint32_t  g_worldMs = 0;
int       g_resetPct = -1;     // 0-100 elapsed of the 5h reset window (sky day->night)
int       g_resetMin = -1;     // minutes until reset

uint32_t lerpC(uint32_t a, uint32_t b, float t) {
  int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255;
  int br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
  int r=ar+(int)((br-ar)*t), g=ag+(int)((bg-ag)*t), bl=ab+(int)((bb-ab)*t);
  return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)bl;
}

const int W = BUDDY_TFT_W, H = BUDDY_TFT_H, CX = W / 2;

// --- dino palette (sampled from the reference art) ---
const uint32_t DINO    = 0xA8D8A6;   // body green
const uint32_t DINO_DK = 0x689A6A;   // back spikes
const uint32_t BELLY   = 0xD7E9C0;   // pale belly
const uint32_t CHEEK   = 0xFD9F9E;   // pink cheeks
const uint32_t OUTLN   = 0x14210F;   // near-black outline
const uint32_t MOUTH_R = 0xF8696B;   // mouth red
const uint32_t MOUTH_D = 0x833033;   // mouth dark
int g_ox = 0;                        // horizontal motion offset (shake)

struct Theme { uint32_t bg, head, ink, accent; };
Theme themeFor(BuddyMood m) {
  switch (m) {
    case BuddyMood::Happy:     return { 0x0E2A18, 0xEAF6EC, 0x16331F, 0x4CD964 };
    case BuddyMood::Sad:       return { 0x101C2E, 0xE8EEF8, 0x1A2B40, 0x4C8BFF };
    case BuddyMood::Surprised: return { 0x2E260E, 0xFBF3DF, 0x3A2E10, 0xFFC83D };
    case BuddyMood::Sleepy:    return { 0x12121E, 0xDCDCEC, 0x24243A, 0x8A8AB0 };
    case BuddyMood::Confused:  return { 0x241830, 0xF1E7FB, 0x2C1E3C, 0xB36CFF };
    case BuddyMood::Focus:     return { 0x0E2230, 0xE6F4FB, 0x123040, 0x2FB8E6 };
    default:                   return { 0x12161A, 0xEAF2F6, 0x1A2228, 0x49C6E5 };
  }
}

// ---- animation state ----
uint32_t lastFrame = 0;
float    breathe = 0;          // 0..2pi
uint32_t nextBlinkAt = 0; uint32_t blinkUntil = 0;
int      lookX = 0; uint32_t nextLookAt = 0;
uint8_t  chatterIdx = 0; uint32_t nextChatterAt = 0;
uint8_t  spin = 0; uint32_t nextSpin = 0;
uint32_t effectUntil = 0;      // confetti / sweat / hearts transient

const char* CHATTER[] = {
  "ready when you are", "still here!", "coffee?", "ping me anytime",
  "all good :)", "just chillin'", "boop", "let's build"
};
const int CHATTER_N = 8;

struct Star { int16_t x, y; uint8_t sp, sz, ph; };
const int NSTAR = 22; Star stars[NSTAR];
struct Conf { float x, y, vx, vy; uint16_t col; }; const int NCONF = 28; Conf conf[NCONF];

template <typename T> T clampv(T v, T lo, T hi){ return v<lo?lo:(v>hi?hi:v); }

void seedStars() {
  for (int i = 0; i < NSTAR; ++i) {
    stars[i] = { (int16_t)random(0, W), (int16_t)random(0, H),
                 (uint8_t)random(1, 4), (uint8_t)random(1, 3), (uint8_t)random(0, 255) };
  }
}
void spawnConfetti(const Theme& t) {
  uint16_t cols[4] = { (uint16_t)gfx.color888(255,90,90), (uint16_t)gfx.color888(90,200,255),
                       (uint16_t)gfx.color888(255,210,80), (uint16_t)gfx.color888(120,230,140) };
  for (int i = 0; i < NCONF; ++i)
    conf[i] = { (float)random(20, W-20), (float)random(-40, 0),
                (float)random(-12,12)/10.0f, (float)random(15,38)/10.0f, cols[random(0,4)] };
}

// ---- drawing (target = canvas if sprite, else gfx) ----
lgfx::LovyanGFX* T = &gfx;

void bg(const Theme& t) { T->fillScreen(t.bg); }

void drawStars(const Theme& t, uint32_t now) {
  for (int i = 0; i < NSTAR; ++i) {
    Star& s = stars[i];
    s.y += s.sp; if (s.y > H) { s.y = 0; s.x = random(0, W); }
    uint8_t tw = 130 + (uint8_t)(120 * sinf((now/300.0f) + s.ph));
    uint32_t c = gfx.color888((tw*((t.accent>>16)&0xFF))/255,
                              (tw*((t.accent>>8)&0xFF))/255,
                              (tw*(t.accent&0xFF))/255);
    if (s.sz <= 1) T->drawPixel(s.x, s.y, c);
    else T->fillCircle(s.x, s.y, 1, c);
  }
}

void drawCodeRain(const Theme& t, uint32_t now) {
  // light "data stream" columns behind the pet while busy
  for (int col = 0; col < 8; ++col) {
    int x = 14 + col * 28;
    int headY = (int)((now/40 + col*37) % (H+40)) - 20;
    for (int k = 0; k < 5; ++k) {
      int y = headY - k*16;
      if (y < 0 || y > H) continue;
      uint8_t a = 220 - k*40;
      uint32_t c = gfx.color888((a*((t.accent>>16)&0xFF))/255,
                                (a*((t.accent>>8)&0xFF))/255,
                                (a*(t.accent&0xFF))/255);
      char ch = (random(0,2)) ? '1' : '0';
      T->setTextColor(c); T->setTextSize(1);
      T->setCursor(x, y); T->print(ch);
    }
  }
}

// the green dino body: back spikes + rounded body + pale belly
void drawHead(const Theme& t, int dy) {
  int cx = CX + g_ox;
  const int top = 72 + dy, L = cx - 92, w = 184, h = 196, r = 86;
  const int sx[3] = { cx - 4, cx + 44, cx + 80 };
  const int sy[3] = { top - 2, top + 10, top + 30 };
  const int sr[3] = { 26, 21, 16 };
  for (int i = 0; i < 3; ++i) {
    T->fillCircle(sx[i], sy[i], sr[i] + 3, OUTLN);
    T->fillCircle(sx[i], sy[i], sr[i], DINO_DK);
  }
  T->fillRoundRect(L - 3, top - 3, w + 6, h + 6, r + 3, OUTLN);
  T->fillRoundRect(L, top, w, h, r, DINO);
  T->fillEllipse(cx, top + 152, 52, 44, BELLY);
}

void drawEyes(BuddyMood m, const Theme& t, int dy, bool blink) {
  int cx = CX + g_ox;
  int ey = 150 + dy; int lx = cx-40+lookX, rx = cx+40+lookX;
  const uint32_t wht = 0xFFFFFF;
  if (blink || m == BuddyMood::Sleepy) {
    T->fillRoundRect(lx-13, ey-2, 26, 5, 3, OUTLN);
    T->fillRoundRect(rx-13, ey-2, 26, 5, 3, OUTLN);
  } else switch (m) {
    case BuddyMood::Surprised:
      T->fillCircle(lx, ey, 17, OUTLN); T->fillCircle(rx, ey, 17, OUTLN);
      T->fillCircle(lx-4, ey-6, 4, wht); T->fillCircle(rx-4, ey-6, 4, wht); break;
    case BuddyMood::Focus:
      T->fillRoundRect(lx-14, ey-7, 28, 15, 7, OUTLN);
      T->fillRoundRect(rx-14, ey-7, 28, 15, 7, OUTLN); break;
    case BuddyMood::Sad:
      T->fillEllipse(lx, ey+3, 13, 18, OUTLN); T->fillEllipse(rx, ey+3, 13, 18, OUTLN);
      T->fillCircle(lx+11, ey+15, 4, 0x6FB7FF); break;          // tear
    case BuddyMood::Confused:
      T->fillEllipse(lx, ey, 14, 19, OUTLN);
      T->fillRoundRect(rx-14, ey-2, 28, 6, 3, OUTLN); break;
    default:  // neutral / happy -> reference solid round eyes
      T->fillEllipse(lx, ey, 14, 20, OUTLN); T->fillEllipse(rx, ey, 14, 20, OUTLN);
      T->fillCircle(lx-4, ey-7, 4, wht); T->fillCircle(rx-4, ey-7, 4, wht); break;
  }
  // nostrils
  T->fillCircle(cx-9, ey+30, 2, OUTLN); T->fillCircle(cx+9, ey+30, 2, OUTLN);
}

void drawCheeksMouth(BuddyMood m, const Theme& t, int dy) {
  int cx = CX + g_ox;
  int my = 192 + dy;
  T->fillEllipse(cx-64, my-14, 16, 9, CHEEK);
  T->fillEllipse(cx+64, my-14, 16, 9, CHEEK);
  switch (m) {
    case BuddyMood::Happy:                                   // big open grin (reference)
      T->fillArc(cx, my, 27, 0,  8, 172, MOUTH_D);
      T->fillArc(cx, my, 22, 0, 16, 164, MOUTH_R);
      T->fillArc(cx, my+9, 12, 0, 10, 170, 0xE34A4C); break; // tongue
    case BuddyMood::Surprised:
      T->fillCircle(cx, my+2, 12, MOUTH_D); T->fillCircle(cx, my+2, 8, MOUTH_R); break;
    case BuddyMood::Sad:       T->fillArc(cx, my+18, 22, 17, 200, 340, MOUTH_D); break;
    case BuddyMood::Sleepy:    T->fillRoundRect(cx-7, my, 14, 4, 2, MOUTH_D); break;
    case BuddyMood::Focus:     T->fillRoundRect(cx-13, my, 26, 5, 2, MOUTH_D); break;
    case BuddyMood::Confused:  T->fillArc(cx-7, my, 13, 9, 20, 160, MOUTH_D);
                               T->fillArc(cx+8, my+5, 12, 8, 200, 320, MOUTH_D); break;
    default:                   T->fillArc(cx, my-2, 20, 15, 20, 160, MOUTH_D); break;  // gentle smile
  }
}

void drawZzz(const Theme& t, uint32_t now) {
  T->setTextColor(t.accent);
  int base = (int)((now/500) % 3);
  const int sz[3] = {1,2,3};
  for (int i=0;i<3;i++){ T->setTextSize(sz[(i+base)%3]); T->setCursor(CX+50+i*14, 70-i*16); T->print('z'); }
}

// the two little raised arms with a crease (drawn over the body)
void drawBody(const Theme& t, int dy) {
  int cx = CX + g_ox; int ay = 218 + dy;
  T->fillEllipse(cx-54, ay, 14, 24, DINO);  T->drawEllipse(cx-54, ay, 14, 24, OUTLN);
  T->fillEllipse(cx+54, ay, 14, 24, DINO);  T->drawEllipse(cx+54, ay, 14, 24, OUTLN);
  T->drawLine(cx-60, ay-10, cx-50, ay-18, OUTLN);
  T->drawLine(cx+60, ay-10, cx+50, ay-18, OUTLN);
}

void drawSpinner(const Theme& t, uint32_t now) {
  int cyc = (now/120) % 8;
  for (int i = 0; i < 8; ++i) {
    float a = i * 0.785f;
    int x = CX + (int)(16*cosf(a)), y = 250 + (int)(16*sinf(a));
    uint8_t b = 60 + ((i - cyc + 8) % 8) * 24;
    uint32_t c = gfx.color888((b*((t.accent>>16)&0xFF))/255,(b*((t.accent>>8)&0xFF))/255,(b*(t.accent&0xFF))/255);
    T->fillCircle(x, y, 3, c);
  }
}

void drawConfetti() {
  for (int i = 0; i < NCONF; ++i) {
    conf[i].x += conf[i].vx; conf[i].y += conf[i].vy; conf[i].vy += 0.06f;
    if (conf[i].y < H) T->fillRect((int)conf[i].x, (int)conf[i].y, 4, 4, conf[i].col);
  }
}

void drawSweat(const Theme& t, uint32_t now) {
  int y = 110 + (int)((now/8) % 60);
  T->fillCircle(CX+58, y, 5, gfx.color888(120,200,255));
}

void drawStar(int x, int y, int r, uint32_t c) {
  T->fillTriangle(x, y-r, x-r, y+r, x+r, y+r, c);
  T->fillTriangle(x, y+r, x-r, y-r, x+r, y-r, c);
}

void drawBar(int x, int y, int w, int h, uint8_t pct, uint32_t fg, uint32_t bgc) {
  T->fillRoundRect(x, y, w, h, h/2, bgc);
  int fw = (w-2) * (pct > 100 ? 100 : pct) / 100;
  if (fw > 0) T->fillRoundRect(x+1, y+1, fw, h-2, (h-2)/2, fg);
}

void drawHUD(const Theme& t) {
  T->setTextColor(t.head); T->setTextSize(2); T->setTextDatum(lgfx::top_left);
  T->setCursor(8, 4); T->print((String("Lv.")+g_pet.level).c_str());
  drawBar(72, 8, 80, 9, g_pet.xp % 100, t.accent, t.ink);   // xp to next level
  T->setTextDatum(lgfx::top_right); T->setTextColor(t.accent); T->setTextSize(1);
  T->drawString((String(g_pet.ageMin/60)+"h").c_str(), W-8, 8);
  T->setTextDatum(lgfx::top_left);
}

void drawStatBars(const Theme& t) {
  struct Row { const char* lb; uint8_t v; uint32_t c; };
  Row rows[3] = {
    {"H", g_pet.hunger, gfx.color888(255,170,70)},
    {"J", g_pet.happy,  gfx.color888(255,110,150)},
    {"E", g_pet.energy, gfx.color888(120,210,255)},
  };
  int x = 10, y = 24;
  for (int i = 0; i < 3; ++i) {
    T->setTextColor(t.head); T->setTextSize(1); T->setTextDatum(lgfx::top_left);
    T->setCursor(x, y); T->print(rows[i].lb);
    drawBar(x + 9, y - 1, 52, 7, rows[i].v, rows[i].c, t.ink);
    x += 70;
  }
}

void drawFaint(const Theme& t, uint32_t now) {
  Theme dim = themeFor(BuddyMood::Sleepy);
  bg(dim);
  g_ox = 0;
  drawHead(dim, 0);
  drawBody(dim, 0);
  int ey = 150;
  for (int ex : {CX-40, CX+40}) {          // X_X eyes
    T->drawLine(ex-11, ey-12, ex+11, ey+12, OUTLN); T->drawLine(ex+11, ey-12, ex-11, ey+12, OUTLN);
    T->drawLine(ex-10, ey-12, ex+12, ey+12, OUTLN); T->drawLine(ex+10, ey-12, ex-12, ey+12, OUTLN);
  }
  T->fillCircle(CX-9, ey+30, 2, OUTLN); T->fillCircle(CX+9, ey+30, 2, OUTLN);
  T->fillRoundRect(CX-10, 192, 20, 4, 2, MOUTH_D);
  T->setTextColor(0xD0D0D0); T->setTextDatum(lgfx::middle_center); T->setTextSize(2);
  T->drawString("ran away...", CX, 292);
  T->setTextSize(1); T->setTextColor(t.accent);
  if ((now/600) % 2) T->drawString("press Hatch to start over", CX, 312);
  T->setTextDatum(lgfx::top_left);
}

void drawBubble(const Theme& t, const char* txt) {
  if (!txt || !*txt) return;
  const int by = 280, bh = 34, bx = 10, bw = W-20;
  T->fillRoundRect(bx, by, bw, bh, 10, t.ink);
  T->fillTriangle(CX-10, by, CX+10, by, CX, by-10, t.ink);
  T->setTextColor(t.head); T->setTextDatum(lgfx::middle_center); T->setTextSize(2);
  T->drawString(txt, CX, by + bh/2);
  T->setTextDatum(lgfx::top_left);
}

// top-edge "fuel gauge" for daily usage (green -> amber -> red)
uint32_t gaugeCol(int p) { return p < 60 ? 0x4CD964 : (p < 85 ? 0xFFC83D : 0xFF5A5A); }

// two thin top gauges: 5-hour block usage (top) + today usage (below)
void drawUsageBar() {
  if (!g_usageValid) return;
  if (g_blockPct >= 0) {
    int b = g_blockPct > 100 ? 100 : g_blockPct;
    T->fillRect(0, 0, W, 3, 0x0A1014);
    T->fillRect(0, 0, W * b / 100, 3, gaugeCol(b));
  }
  int yo = (g_blockPct >= 0) ? 3 : 0;
  int p = g_usagePct < 0 ? 0 : (g_usagePct > 100 ? 100 : g_usagePct);
  T->fillRect(0, yo, W, 3, 0x0A1014);
  T->fillRect(0, yo, W * p / 100, 3, gaugeCol(p));
}

// progressively tired / scruffy face as usage climbs ("worked hard with you")
void drawTiredFace(int level, const Theme& t, int dy) {
  int cx = CX + g_ox; int ey = 150 + dy; int lx = cx-40, rx = cx+40, my = 192 + dy;
  // half-lidded eyes
  T->fillEllipse(lx, ey+4, 13, 10, OUTLN); T->fillEllipse(rx, ey+4, 13, 10, OUTLN);
  T->fillRoundRect(lx-15, ey-6, 30, 6, 3, OUTLN); T->fillRoundRect(rx-15, ey-6, 30, 6, 3, OUTLN);
  T->fillCircle(cx-9, ey+30, 2, OUTLN); T->fillCircle(cx+9, ey+30, 2, OUTLN);
  // cheeks + panting mouth
  T->fillEllipse(cx-64, my-14, 16, 9, CHEEK); T->fillEllipse(cx+64, my-14, 16, 9, CHEEK);
  T->fillArc(cx, my, 15, 0, 10, 170, MOUTH_D); T->fillArc(cx, my, 11, 0, 18, 162, MOUTH_R);
  // sweat drops (more with fatigue), animated downward
  int drops = level >= 85 ? 3 : (level >= 70 ? 2 : 1);
  for (int i = 0; i < drops; ++i) {
    int sx = cx + 50 - i*18;
    int sy = 118 + dy + (int)((millis()/9 + i*120) % 46);
    T->fillCircle(sx, sy, 4, 0x9ED8FF);
  }
  // scruffy smudges at high fatigue
  if (level >= 82) {
    uint32_t sm = 0x6E8C68;
    T->drawLine(cx-60, my-8, cx-50, my-3, sm); T->drawLine(cx-58, my-6, cx-48, my-1, sm);
    T->drawLine(cx+50, my+6, cx+60, my+11, sm);
  }
}

// ---- multi-session "world" view: a grid of rooms, one mini dino each ----
void drawMiniDino(int cx, int gy, uint8_t st, uint32_t now, int idx) {
  int bob = (st == 1) ? (int)(2.0f * sinf(now/120.0f + idx))     // working = lively
                      : (int)(1.5f * sinf(now/500.0f + idx));
  int by = gy - 22 + bob;
  // body + spikes + belly
  T->fillCircle(cx, by, 19, OUTLN); T->fillCircle(cx, by, 17, DINO);
  T->fillCircle(cx-3, by-16, 6, DINO_DK); T->fillCircle(cx+11, by-12, 5, DINO_DK);
  T->fillEllipse(cx, by+8, 9, 7, BELLY);
  int ey = by - 2;
  if (st == 2) {                                   // waiting
    T->fillCircle(cx-7, ey, 4, OUTLN); T->fillCircle(cx+7, ey, 4, OUTLN);
    T->fillCircle(cx, by+5, 3, MOUTH_D);
    if ((now/350 + idx) % 2) { T->setTextColor(0xFFC83D); T->setTextSize(2);
      T->setCursor(cx+15, by-30); T->print('!'); T->setTextSize(1); }
  } else if (st == 0) {                             // idle / resting
    T->fillRoundRect(cx-10, ey-1, 7, 3, 1, OUTLN); T->fillRoundRect(cx+3, ey-1, 7, 3, 1, OUTLN);
    T->fillRoundRect(cx-4, by+5, 8, 2, 1, MOUTH_D);
    if ((now/600 + idx) % 2) { T->setTextColor(0x88A0B0); T->setCursor(cx+13, by-20); T->print('z'); }
  } else if (st == 3) {                             // done
    T->fillCircle(cx-7, ey, 4, OUTLN); T->fillCircle(cx+7, ey, 4, OUTLN);
    T->fillArc(cx, by+3, 9, 6, 20, 160, MOUTH_D);
  } else {                                          // working
    T->fillRoundRect(cx-10, ey-2, 7, 5, 2, OUTLN); T->fillRoundRect(cx+3, ey-2, 7, 5, 2, OUTLN);
    T->fillRoundRect(cx-5, by+5, 10, 2, 1, MOUTH_D);
    T->fillRect(cx-15, gy-4, 30, 3, 0x5A4632);      // tiny desk
    int d = (now/250 + idx) % 4;                    // typing dots
    for (int k = 0; k < 3; ++k)
      T->fillCircle(cx-6 + k*6, gy-9, 1, k <= d ? 0x4CD964 : 0x33454F);
  }
}

// global sky behind the whole grid: day -> night as the 5h window elapses
void drawSky(uint32_t now) {
  float e = (g_resetPct < 0) ? 0.25f : g_resetPct / 100.0f;   // elapsed 0..1
  uint32_t topC = lerpC(0x4FB0F0, 0x080C1E, e);
  uint32_t botC = lerpC(0xBFE6FF, 0x05060F, e);
  const int bands = 12;
  for (int b = 0; b < bands; ++b)
    T->fillRect(0, b * H / bands, W, H / bands + 1, lerpC(topC, botC, b / (float)(bands - 1)));
  // sun (high->low) becomes a moon at night
  int sx = 14 + (int)(e * (W - 28));
  int sy = 16 + (int)(e * 78);
  if (e < 0.72f) T->fillCircle(sx, sy, 9, lerpC(0xFFE070, 0xFF9A50, e));
  else { T->fillCircle(sx, sy, 8, 0xE8ECF5);
         // a few stars
         for (int s = 0; s < 14; ++s) {
           int rx = (s * 53 + 11) % W, ry = (s * 29 + 7) % (H/2);
           if ((now/500 + s) % 3) T->drawPixel(rx, ry, 0xC8D2E8);
         } }
}

void drawRoom(int x, int y, int w, int h, const WorldRoom& rm, uint32_t now, int idx) {
  int iL = x+5, iR = x+w-5, iT = y+18, iB = y+h-6, floorY = iB - 2;
  // water level (vs daily budget): bottom-up fill
  int span = floorY - iT;
  int wtop = floorY - (int)(span * (rm.fill > 100 ? 100 : rm.fill) / 100.0f);
  uint32_t wc = rm.fill < 40 ? 0x1FA8D8 : rm.fill < 70 ? 0x36C06A
              : rm.fill < 88 ? 0xE6A93D : 0xE65A5A;
  if (wtop < floorY) {
    T->fillRect(iL, wtop, iR-iL, floorY-wtop, wc);
    for (int xx = iL; xx < iR; xx += 3)                       // wobbling surface
      T->drawPixel(xx, wtop + (int)(2*sinf(now/350.0f + xx*0.3f + idx)), 0xFFFFFF);
    if (rm.st == 1)                                           // bubbles while working
      for (int k = 0; k < 3; ++k) {
        int bx = (x+w/2) - 8 + k*8;
        int by = floorY - (int)((now/6 + k*220 + idx*90) % (uint32_t)(floorY-wtop+1));
        T->fillCircle(bx, by, 1, 0xE8FBFF);
      }
  }
  T->drawRoundRect(x+3, y+3, w-6, h-6, 6, 0x3A5A6E);          // glass tank outline
  int rise = rm.fill > 70 ? (int)((rm.fill - 70) * 0.4f) : 0; // dino floats up near full
  drawMiniDino(x + w/2, floorY - rise, rm.st, now, idx);
  // label + cost
  T->setTextDatum(lgfx::top_center); T->setTextSize(1);
  T->setTextColor(0xEAF2F6); T->drawString(rm.label, x+w/2, y+6);
  char cs[10]; snprintf(cs, sizeof cs, "$%.1f", rm.cost);
  T->setTextColor(0xDCEFFA); T->drawString(cs, x+w/2, y+15);
  T->setTextDatum(lgfx::top_left);
}

void renderWorld(uint32_t now) {
  drawSky(now);
  int n = g_numRooms, cols = 2, rows = (n + 1) / 2;
  int top = 20, cw = W / cols, ch = (H - top) / rows;
  for (int i = 0; i < n; ++i)
    drawRoom((i % cols) * cw, top + (i / cols) * ch, cw, ch, g_rooms[i], now, i);
  // header: session count + reset countdown
  T->setTextColor(0xFFFFFF); T->setTextDatum(lgfx::top_left); T->setTextSize(1);
  char hl[14]; snprintf(hl, sizeof hl, "%d sess", n);
  T->drawString(hl, 4, 6);
  if (g_resetMin >= 0) {
    char rs[16]; snprintf(rs, sizeof rs, "reset %d:%02d", g_resetMin/60, g_resetMin%60);
    T->setTextDatum(lgfx::top_right); T->drawString(rs, W-4, 6); T->setTextDatum(lgfx::top_left);
  }
  if (useSprite) canvas.pushSprite(0, 0);
}

const char* idleMessage() {
  switch (g_pet.life) {
    case PetLife::Sick:     return "feeling sick...";
    case PetLife::Sleeping: return nullptr;       // Zzz shown instead
    default: break;
  }
  if (g_pet.hunger < 25) return "so hungry...";
  if (g_pet.happy  < 25) return "play with me?";
  if (g_pet.energy < 20) return "sleepy...";
  if (g_usageValid && g_usagePct >= 85) return "phew... so much work";
  if (g_usageValid && g_usagePct >= 60 && (chatterIdx & 1)) return "need a break?";
  if (g_usageValid) {
    int k = chatterIdx % 3;
    if (k == 1) return g_usageStr;
    if (k == 2) return g_usageStr2;
  }
  return CHATTER[chatterIdx % CHATTER_N];
}

void renderFrame(const BuddyEvent& ev, uint32_t now) {
  // Multi-session world view takes over when 2+ sessions are active.
  if (g_numRooms >= 2 && now - g_worldMs < 90000) { renderWorld(now); return; }

  // Faint overrides everything until a new egg is hatched.
  if (g_pet.life == PetLife::Faint) {
    drawFaint(themeFor(BuddyMood::Sleepy), now);
    if (useSprite) canvas.pushSprite(0, 0);
    return;
  }

  bool idle = (ev.state == BuddyState::Idle);
  BuddyMood mood = idle ? g_petMood : ev.mood;
  bool petSleep = (g_pet.life == PetLife::Sleeping);
  Theme t = themeFor(petSleep ? BuddyMood::Sleepy : mood);
  bool busy = (ev.state==BuddyState::Thinking||ev.state==BuddyState::Coding||
               ev.state==BuddyState::Testing ||ev.state==BuddyState::Starting);
  int dy = (int)(3.0f * sinf(breathe));     // idle breathing
  g_ox = 0;                                  // motion offsets
  if (now < effectUntil) {
    if (ev.state == BuddyState::Error) g_ox = (int)(5 * sinf(now * 0.05f));         // shake
    else if (ev.state==BuddyState::Done || ev.state==BuddyState::React)
      dy -= (int)(9 * fabsf(sinf(now * 0.018f)));                                   // bounce
  }

  bg(t);
  if (busy) drawCodeRain(t, now); else drawStars(t, now);

  drawHead(t, dy);                                   // body + spikes + belly
  drawBody(t, dy);                                   // arms
  bool blink = (now < blinkUntil);
  bool tired = idle && !petSleep && g_pet.life == PetLife::Ok &&
               g_usageValid && g_usagePct >= 55;
  if (tired) {
    drawTiredFace(g_usagePct, t, dy);                // worn out from a busy day
  } else {
    drawEyes(mood, t, dy, blink || petSleep);
    drawCheeksMouth(petSleep ? BuddyMood::Sleepy : mood, t, dy);
  }

  if (petSleep) drawZzz(t, now);
  if (busy) drawSpinner(t, now);
  if (ev.state==BuddyState::Error || (idle && g_pet.life==PetLife::Sick)) drawSweat(t, now);
  if ((ev.state==BuddyState::Done || ev.state==BuddyState::React) && now < effectUntil) drawConfetti();

  drawHUD(t);
  drawStatBars(t);
  drawUsageBar();

  const char* txt = nullptr;
  if (idle) txt = idleMessage();
  else if (ev.text.length()) txt = ev.text.c_str();
  drawBubble(t, txt);

  if (useSprite) canvas.pushSprite(0, 0);
}
}  // namespace

void Display::begin() {
  gfx.init();
  gfx.setRotation(0);
  gfx.setBrightness(200);
  randomSeed(esp_random());

  canvas.setColorDepth(16);
  useSprite = (canvas.createSprite(W, H) != nullptr);
  T = useSprite ? (lgfx::LovyanGFX*)&canvas : (lgfx::LovyanGFX*)&gfx;

  // boot splash: a happy dino drawn straight to the panel
  {
    lgfx::LovyanGFX* save = T; T = &gfx;
    Theme th = themeFor(BuddyMood::Happy);
    T->fillScreen(th.bg);
    g_ox = 0;
    drawHead(th, 0); drawBody(th, 0);
    drawEyes(BuddyMood::Happy, th, 0, false);
    drawCheeksMouth(BuddyMood::Happy, th, 0);
    T->setTextColor(th.head); T->setTextDatum(lgfx::middle_center); T->setTextSize(2);
    T->drawString("Agent Buddy", CX, 300);
    T->setTextDatum(lgfx::top_left);
    T = save;
  }

  uint32_t now = millis();
  nextBlinkAt = now + random(1500, 4000);
  nextLookAt  = now + random(2000, 5000);
  nextChatterAt = now + 5000;
  seedStars();
  ready_ = true;
}

void Display::setPet(const PetStats& st, BuddyMood idleMood) {
  g_pet = st; g_petMood = idleMood;
}

void Display::setUsage(float cost, uint32_t tokens, int pct, int blockPct) {
  g_usageValid = true;
  g_usagePct = pct;
  g_blockPct = blockPct;
  snprintf(g_usageStr,  sizeof g_usageStr,  "$%.1f today", cost);
  snprintf(g_usageStr2, sizeof g_usageStr2, "%.1fM tokens", tokens / 1e6);
}

void Display::setWorld(const WorldRoom* rooms, int n, int resetPct, int resetMin) {
  if (n > 6) n = 6;
  for (int i = 0; i < n; ++i) g_rooms[i] = rooms[i];
  g_numRooms = n;
  if (resetPct >= 0) g_resetPct = resetPct;
  if (resetMin >= 0) g_resetMin = resetMin;
  g_worldMs = millis();
}

void Display::render(const BuddyEvent& ev) {
  cur_ = ev;
  if ((ev.state == BuddyState::Done || ev.state == BuddyState::React)) {
    effectUntil = millis() + 1700; spawnConfetti(themeFor(ev.mood));
  }
  if (ev.state == BuddyState::Error) effectUntil = millis() + 1500;
  // frame is drawn in tick(); render() just updates state.
}

void Display::tick(uint32_t now) {
  if (!ready_) return;
  if (now - lastFrame < 33) return;        // ~30 fps
  lastFrame = now;

  breathe += 0.12f; if (breathe > 6.283f) breathe -= 6.283f;

  if (now >= nextBlinkAt) { blinkUntil = now + 120; nextBlinkAt = now + random(1800, 4500); }
  if (now >= nextLookAt) { lookX = random(-8, 9); nextLookAt = now + random(1500, 4000); }
  else if (lookX != 0 && (now % 64) < 33) { lookX += (lookX > 0 ? -1 : 1); }
  if (cur_.state == BuddyState::Idle && now >= nextChatterAt) {
    chatterIdx = (chatterIdx + 1) % CHATTER_N; nextChatterAt = now + 6000;
  }

  renderFrame(cur_, now);
}

#else
// =====================================================================
//  NO-LCD FALLBACK — render the face to Serial
// =====================================================================
void Display::begin() { ready_ = true; }
void Display::render(const BuddyEvent& ev) {
  Serial.print("[face] "); Serial.print(faceFor(ev.mood));
  Serial.print("  <"); Serial.print(stateName(ev.state)); Serial.print(">");
  if (ev.text.length()) { Serial.print("  \""); Serial.print(ev.text); Serial.print("\""); }
  Serial.println();
}
void Display::tick(uint32_t now) { (void)now; }
#endif
