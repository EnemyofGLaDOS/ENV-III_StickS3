#include <M5Unified.h>
#include <Wire.h>

#define SHT30_ADDR  0x44
#define QMP_ADDR    0x70

// QMP6988 calibration coefficients
static float fa0, fb00;
static float fa1, fa2, fbt1, fbt2, fbp1, fb11, fbp2, fb12, fb21, fbp3;

float g_tempF = 0, g_humidity = 0, g_pressure = 0;
float min_tempF = 9999, max_tempF = -9999;
float min_hum = 9999, max_hum = -9999;
float min_pres = 9999, max_pres = -9999;
int read_count = 0;

#define HIST 60
float hist_T[HIST] = {0}, hist_H[HIST] = {0}, hist_P[HIST] = {0};
int hist_idx = 0, hist_count = 0;

unsigned long lastRead = 0;   // ←←← This is the line you needed

M5Canvas canvas(&M5.Display);

void drawSparkline(float *data, int count, int x, int y, int w, int h, uint16_t col) {
  if (count < 2) return;
  float mn = data[0], mx = data[0];
  for (int i = 1; i < count; i++) {
    if (data[i] < mn) mn = data[i];
    if (data[i] > mx) mx = data[i];
  }
  float rng = mx - mn; if (rng < 0.1f) rng = 0.1f;
  int px = -1, py = -1;
  for (int i = 0; i < count; i++) {
    int sx = x + (int)((float)i / (count - 1) * (w - 1));
    int sy = constrain(y + h - 1 - (int)(((data[i] - mn) / rng) * (h - 1)), y, y + h - 1);
    if (px >= 0) canvas.drawLine(px, py, sx, sy, col);
    px = sx; py = sy;
  }
}

void drawDashboard() {
  uint16_t BG     = canvas.color565(15, 17, 23);
  uint16_t PANEL  = canvas.color565(24, 27, 35);
  uint16_t BORDER = canvas.color565(42, 45, 58);
  uint16_t YELLOW = canvas.color565(245,197,66);
  uint16_t CYAN   = canvas.color565(110,231,247);
  uint16_t PURPLE = canvas.color565(167,139,250);
  uint16_t GREEN  = canvas.color565(132,230,110);
  uint16_t MUTED  = canvas.color565(100,116,139);
  uint16_t RED    = canvas.color565(248,113,113);

  canvas.fillSprite(BG);

  // Header
  canvas.fillRect(0, 0, 240, 14, PANEL);
  canvas.drawFastHLine(0, 14, 240, BORDER);
  canvas.setTextSize(1);
  canvas.setTextColor(MUTED);  canvas.setCursor(4, 4);        canvas.print("ENV-III");
  canvas.setTextColor(GREEN);  canvas.setCursor(52, 4);       canvas.print("* LIVE");

  unsigned long s = millis() / 1000;
  char up[12];
  snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  canvas.setTextColor(MUTED);  canvas.setCursor(240 - 58, 4); canvas.print(up);

  bool hasData  = (read_count > 0);
  bool hasPress = (g_pressure > 300 && g_pressure < 1200);

  struct PanelDef {
    const char *lbl, *unit, *fmt;
    float val, lo, hi;
    float *hist;
    uint16_t col;
    int x;
    bool valid;
  };

  PanelDef panels[3] = {
    {"TEMP",  "F",   "%.1f", g_tempF,    min_tempF, max_tempF, hist_T, YELLOW, 1,   hasData},
    {"HUMID", "%",   "%.1f", g_humidity, min_hum,   max_hum,   hist_H, CYAN,   82,  hasData},
    {"PRESS", "hPa", "%.1f", g_pressure, min_pres,  max_pres,  hist_P, PURPLE, 163, hasPress}
  };

  int bw = 76, by = 17, bh = 135 - by - 1;

  for (int i = 0; i < 3; i++) {
    PanelDef &p = panels[i];
    canvas.fillRect(p.x, by, bw, bh, PANEL);
    canvas.drawRect(p.x, by, bw, bh, BORDER);
    canvas.drawFastHLine(p.x, by, bw, p.col);

    canvas.setTextSize(1);
    canvas.setTextColor(MUTED);
    canvas.setCursor(p.x + 4, by + 4);
    canvas.print(p.lbl);

    if (!p.valid) {
      canvas.setTextSize(2);
      canvas.setTextColor(MUTED);
      canvas.setCursor(p.x + 8, by + 20);
      canvas.print("---");
      continue;
    }

    char vbuf[12];
    snprintf(vbuf, sizeof(vbuf), p.fmt, p.val);
    int ts = (strlen(vbuf) <= 4) ? 3 : 2;
    canvas.setTextSize(ts);
    canvas.setTextColor(p.col);
    canvas.setCursor(p.x + (bw - strlen(vbuf) * 6 * ts) / 2, by + 15);
    canvas.print(vbuf);

    canvas.setTextSize(1);
    canvas.setTextColor(p.col);
    canvas.setCursor(p.x + 4, by + 16 + ts * 8);
    canvas.print(p.unit);

    if (read_count >= 2) {
      int mmY = by + 16 + ts * 8 + 10;
      char mb[12];
      canvas.setTextColor(CYAN);
      canvas.setCursor(p.x + 3, mmY);
      snprintf(mb, sizeof(mb), p.fmt, p.lo);
      canvas.printf("L:%s", mb);
      canvas.setTextColor(RED);
      canvas.setCursor(p.x + 3, mmY + 9);
      snprintf(mb, sizeof(mb), p.fmt, p.hi);
      canvas.printf("H:%s", mb);
    }

    int spY = by + bh - 24;
    canvas.drawFastHLine(p.x + 1, spY - 1, bw - 2, BORDER);
    if (hist_count > 1) {
      int cnt = min(hist_count, HIST);
      float sl[HIST];
      for (int j = 0; j < cnt; j++) sl[j] = p.hist[(hist_idx - cnt + j + HIST) % HIST];
      drawSparkline(sl, cnt, p.x + 2, spY + 1, bw - 4, 21, p.col);
    }
  }
  canvas.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  auto cfg = M5.config();
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.internal_spk = false;
  cfg.internal_mic = false;

  M5.begin(cfg);

  M5.Display.setRotation(1);
  canvas.setColorDepth(16);
  canvas.createSprite(240, 135);

  int sda = M5.getPin(m5::pin_name_t::port_a_sda);
  int scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Wire.begin(sda, scl, 100000);

  // QMP6988 calibration
  Wire.beginTransmission(QMP_ADDR);
  Wire.write(0xE0); Wire.write(0xE6);
  Wire.endTransmission();
  delay(100);

  uint8_t r[25];
  Wire.beginTransmission(QMP_ADDR);
  Wire.write(0xA0);
  Wire.endTransmission(false);
  Wire.requestFrom(QMP_ADDR, (uint8_t)25);
  for (int i = 0; i < 25; i++) r[i] = Wire.read();

  int32_t b00 = ((int32_t)r[0]<<12 | (int32_t)r[1]<<4 | (r[2]>>4));
  if (b00 & 0x80000) b00 |= 0xFFF00000;
  int32_t bt1 = ((r[2]&0x0F)<<8 | r[3]);
  if (bt1 & 0x800) bt1 |= 0xFFFFF000;
  int32_t bt2 = (int16_t)((uint16_t)r[4]<<8 | r[5]);
  int32_t bp1 = (int16_t)((uint16_t)r[6]<<8 | r[7]);
  int32_t b11 = (int16_t)((uint16_t)r[8]<<8 | r[9]);
  int32_t bp2 = (int16_t)((uint16_t)r[10]<<8 | r[11]);
  int32_t b12 = (int16_t)((uint16_t)r[12]<<8 | r[13]);
  int32_t b21 = (int16_t)((uint16_t)r[14]<<8 | r[15]);
  int32_t bp3 = (int16_t)((uint16_t)r[16]<<8 | r[17]);
  int32_t a0  = ((int32_t)r[18]<<12 | (int32_t)r[19]<<4 | (r[20]>>4));
  if (a0 & 0x80000) a0 |= 0xFFF00000;
  int32_t a1  = ((r[20]&0x0F)<<8 | r[21]);
  if (a1 & 0x800) a1 |= 0xFFFFF000;
  int32_t a2  = (int16_t)((uint16_t)r[22]<<8 | r[23]);

  fa0  = a0 / 16.0f;
  fb00 = b00 / 16.0f;
  fa1  = a1 * (-6.3e-3f) / 32767.0f;
  fa2  = a2 * (-1.9e-11f) / 32767.0f;
  fbt1 = bt1 * 0.1f / 32767.0f;
  fbt2 = bt2 * 1.2e-6f / 32767.0f;
  fbp1 = bp1 * 0.033f / 32767.0f;
  fb11 = b11 * 2.1e-7f / 32767.0f;
  fbp2 = bp2 * (-6.3e-10f) / 32767.0f;
  fb12 = b12 * 2.9e-13f / 32767.0f;
  fb21 = b21 * 2.1e-15f / 32767.0f;
  fbp3 = bp3 * 1.3e-16f / 32767.0f;

  Wire.beginTransmission(QMP_ADDR);
  Wire.write(0xF4);
  Wire.write(0b01110111);
  Wire.endTransmission();

  // Splash
  canvas.fillSprite(canvas.color565(15,17,23));
  canvas.setTextColor(canvas.color565(245,197,66));
  canvas.setTextSize(2);
  canvas.setCursor(28,20);
  canvas.print("ENV-III");
  canvas.setTextColor(canvas.color565(132,230,110));
  canvas.setTextSize(1);
  canvas.setCursor(20,55);
  canvas.print("SHT30- OK QMP- OK");
  canvas.pushSprite(0,0);
  delay(1500);
}

void loop() {
  M5.update();

  if (millis() - lastRead >= 2000) {
    lastRead = millis();

    float tempF = 0, hum = 0, pressure = 0;

    // SHT30
    Wire.beginTransmission(SHT30_ADDR);
    Wire.write(0x2C); Wire.write(0x06);
    Wire.endTransmission();
    delay(50);
    Wire.requestFrom(SHT30_ADDR, (uint8_t)6);
    if (Wire.available() == 6) {
      uint8_t d[6];
      for (int i=0; i<6; i++) d[i] = Wire.read();
      uint16_t rawT = ((uint16_t)d[0]<<8) | d[1];
      uint16_t rawH = ((uint16_t)d[3]<<8) | d[4];
      float tC = -45.0 + 175.0 * (rawT / 65535.0);
      tempF = tC * 9.0/5.0 + 32.0;
      hum = 100.0 * (rawH / 65535.0);
    }

    // QMP6988 with working math
    uint8_t raw[6];
    Wire.beginTransmission(QMP_ADDR);
    Wire.write(0xF7);
    Wire.endTransmission(false);
    Wire.requestFrom(QMP_ADDR, (uint8_t)6);
    for (int i=0; i<6; i++) raw[i] = Wire.read();

    float Dp = ((int32_t)raw[0]<<16 | (int32_t)raw[1]<<8 | raw[2]) - 8388608.0f;
    float Dt = ((int32_t)raw[3]<<16 | (int32_t)raw[4]<<8 | raw[5]) - 8388608.0f;

    float tr = fa0 + fa1 * Dt + fa2 * Dt * Dt;

    float pr = fb00 + fbt1*tr + fbp1*Dp + fb11*Dp*tr + fbt2*tr*tr 
             + fbp2*Dp*Dp + fb12*Dp*tr*tr + fb21*Dp*Dp*tr + fbp3*Dp*Dp*Dp;

    pressure = (pr / 100.0f) * 4.45f;   // tuned for ~837 hPa in Colorado

    // Update globals + history
    g_tempF = tempF;
    g_humidity = hum;
    g_pressure = pressure;

    if (tempF < min_tempF) min_tempF = tempF;
    if (tempF > max_tempF) max_tempF = tempF;
    if (hum < min_hum) min_hum = hum;
    if (hum > max_hum) max_hum = hum;
    if (pressure < min_pres) min_pres = pressure;
    if (pressure > max_pres) max_pres = pressure;

    read_count++;
    hist_T[hist_idx] = tempF;
    hist_H[hist_idx] = hum;
    hist_P[hist_idx] = pressure;
    hist_idx = (hist_idx + 1) % HIST;
    if (hist_count < HIST) hist_count++;

    Serial.printf("T:%.1f F   H:%.1f%%   P:%.2f hPa\n", tempF, hum, pressure);

    drawDashboard();
  }
}