#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <U8g2lib.h>

// Pantalla OLED integrada (bus I2C interno)
#define VEXT_PIN 36   // LOW = alimenta la OLED
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

// BME280 en un bus I2C aparte
#define BME_SDA 41
#define BME_SCL 42

#define BUTTON_PIN 0  // botón PRG
#define LED_PIN 35

const uint32_t READ_INTERVAL_MS = 2000;
const uint32_t HISTORY_INTERVAL_MS = 5000;
const int HISTORY_SIZE = 100;   // 100 x 5 s ≈ 8 min
const int TREND_SAMPLES = 12;   // 12 x 5 s = 1 min

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
Adafruit_BME280 bme;
bool sensorOk = false;
uint8_t sensorAddr = 0;

float temperature = NAN, humidity = NAN, pressure = NAN;
float tMin = NAN, tMax = NAN, hMin = NAN, hMax = NAN;

float history[HISTORY_SIZE];
int historyCount = 0, historyHead = 0;

enum Page { PAGE_MAIN, PAGE_GRAPH, PAGE_STATS, PAGE_COUNT };
int page = PAGE_MAIN;

uint32_t lastRead = 0, lastHistory = 0;
bool dirty = true;

// Estado de Claude Code, recibido por USB serie ("S:thinking\n", etc.)
enum ClaudeState { CLAUDE_OFF, CLAUDE_THINKING, CLAUDE_PERMISSION, CLAUDE_DONE };
ClaudeState claude = CLAUDE_OFF;
uint32_t claudeSince = 0;

const uint32_t CLAUDE_DONE_TIMEOUT_MS = 60UL * 1000;          // vuelve a la estación tras 1 min
const uint32_t CLAUDE_IDLE_TIMEOUT_MS = 15UL * 60 * 1000;     // por si se pierde el aviso de fin
const uint32_t CLAUDE_FRAME_MS = 120;

// Clawd: cada carácter es un bloque de CLAWD_SX x CLAWD_SY píxeles
const char *CLAWD[] = {
  "...############...",
  "...##.######.##...",
  ".################.",
  "...############...",
  "....#.#....#.#....",
};
const int CLAWD_COLS = 18, CLAWD_ROWS = 5;
const int CLAWD_SX = 4, CLAWD_SY = 7;

// Muestra i del historial, de la más antigua (0) a la más reciente
float historyAt(int i) {
  return history[(historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE];
}

void pushHistory(float value) {
  history[historyHead] = value;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

bool initSensor() {
  for (uint8_t addr : {0x77, 0x76}) {
    if (bme.begin(addr, &Wire1)) {
      sensorAddr = addr;
      // Modo forzado: mide solo cuando se pide, así el sensor apenas se calienta
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      return true;
    }
  }
  return false;
}

void readSensor() {
  bme.takeForcedMeasurement();
  float t = bme.readTemperature();
  if (isnan(t)) {
    sensorOk = false;
    return;
  }
  temperature = t;
  humidity = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F;

  if (isnan(tMin) || temperature < tMin) tMin = temperature;
  if (isnan(tMax) || temperature > tMax) tMax = temperature;
  if (isnan(hMin) || humidity < hMin) hMin = humidity;
  if (isnan(hMax) || humidity > hMax) hMax = humidity;

  Serial.printf("T: %.2f C  H: %.1f %%  P: %.1f hPa\n", temperature, humidity, pressure);
}

// ---------- Dibujo ----------

void drawHeader(const char *title) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 8, title);
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 128 - (PAGE_COUNT - i) * 7 + 2;
    if (i == page) display.drawDisc(x, 4, 2);
    else display.drawCircle(x, 4, 2);
  }
  display.drawHLine(0, 11, 128);
}

void drawCentered(int y, const char *text) {
  display.drawUTF8((128 - display.getUTF8Width(text)) / 2, y, text);
}

// Icono según la temperatura: <20 frío (copo), 20-23 normal (termómetro), >23 calor (sol)
void drawTempIcon(int cx, int cy, float t) {
  if (t < 20) {
    for (int i = 0; i < 6; i++) {
      float a = i * PI / 3;
      int ex = cx + 10 * cos(a), ey = cy + 10 * sin(a);
      display.drawLine(cx, cy, ex, ey);
      // Ramitas del copo
      int bx = cx + 6 * cos(a), by = cy + 6 * sin(a);
      display.drawLine(bx, by, bx + 3 * cos(a + PI / 3), by + 3 * sin(a + PI / 3));
      display.drawLine(bx, by, bx + 3 * cos(a - PI / 3), by + 3 * sin(a - PI / 3));
    }
  } else if (t <= 23) {
    display.drawRFrame(cx - 3, cy - 14, 7, 22, 3);
    display.drawDisc(cx, cy + 10, 5);
    display.drawBox(cx - 1, cy - 3, 3, 10);
    for (int y = cy - 11; y <= cy + 1; y += 4) display.drawHLine(cx + 5, y, 3);
  } else {
    display.drawDisc(cx, cy, 5);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4;
      display.drawLine(cx + 8 * cos(a), cy + 8 * sin(a), cx + 11 * cos(a), cy + 11 * sin(a));
    }
  }
}

void drawMainPage() {
  drawHeader("AMBIENTE");
  drawTempIcon(12, 31, temperature);

  // Temperatura grande
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", temperature);
  display.setFont(u8g2_font_logisoso28_tn);
  int w = display.getStrWidth(buf);
  int x = (128 - w - 14) / 2;
  display.drawStr(x, 46, buf);
  display.drawCircle(x + w + 4, 20, 2);
  display.setFont(u8g2_font_helvB10_tf);
  display.drawStr(x + w + 7, 30, "C");

  // Tendencia del último minuto
  if (historyCount > TREND_SAMPLES) {
    float diff = temperature - historyAt(historyCount - 1 - TREND_SAMPLES);
    int ax = x + w + 11, ay = 40;
    if (diff > 0.2f) display.drawTriangle(ax - 4, ay + 3, ax + 4, ay + 3, ax, ay - 3);
    else if (diff < -0.2f) display.drawTriangle(ax - 4, ay - 3, ax + 4, ay - 3, ax, ay + 3);
    else display.drawBox(ax - 4, ay - 1, 9, 2);
  }

  display.drawHLine(0, 51, 128);

  // Humedad (con gota) a la izquierda, presión a la derecha
  display.drawTriangle(3, 53, 0, 59, 7, 59);
  display.drawDisc(3, 59, 3);
  display.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "%.0f %%", humidity);
  display.drawStr(10, 63, buf);

  snprintf(buf, sizeof(buf), "%.0f hPa", pressure);
  display.drawStr(128 - display.getStrWidth(buf), 63, buf);
}

void drawGraphPage() {
  drawHeader("TEMP (8 MIN)");

  if (historyCount < 2) {
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(40, "Recopilando datos...");
    return;
  }

  float lo = historyAt(0), hi = lo;
  for (int i = 1; i < historyCount; i++) {
    float v = historyAt(i);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi - lo < 0.5f) {
    float mid = (hi + lo) / 2;
    lo = mid - 0.25f;
    hi = mid + 0.25f;
  }

  const int gx = 24, gy = 15, gw = 128 - gx, gh = 48;

  char buf[8];
  display.setFont(u8g2_font_5x7_tf);
  snprintf(buf, sizeof(buf), "%.1f", hi);
  display.drawStr(0, gy + 6, buf);
  snprintf(buf, sizeof(buf), "%.1f", lo);
  display.drawStr(0, gy + gh - 1, buf);
  display.drawVLine(gx - 3, gy, gh);

  // Línea de puntos a media altura
  for (int x = gx; x < 128; x += 4) display.drawPixel(x, gy + gh / 2);

  int prevX = 0, prevY = 0;
  for (int i = 0; i < historyCount; i++) {
    int x = gx + (long)i * (gw - 1) / (HISTORY_SIZE - 1);
    int y = gy + gh - 1 - (int)((historyAt(i) - lo) / (hi - lo) * (gh - 1));
    if (i > 0) display.drawLine(prevX, prevY, x, y);
    prevX = x;
    prevY = y;
  }
}

void drawStatRow(int y, const char *label, const char *value) {
  display.drawStr(0, y, label);
  display.drawUTF8(128 - display.getUTF8Width(value), y, value);
}

void drawStatsPage() {
  drawHeader("RESUMEN");
  display.setFont(u8g2_font_6x10_tf);

  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f / %.1f °C", tMin, tMax);
  drawStatRow(24, "Temp min/max", buf);
  snprintf(buf, sizeof(buf), "%.0f / %.0f %%", hMin, hMax);
  drawStatRow(36, "Hum min/max", buf);
  snprintf(buf, sizeof(buf), "%.1f hPa", pressure);
  drawStatRow(48, "Presion", buf);

  uint32_t s = millis() / 1000;
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
  drawStatRow(60, "Encendido", buf);
}

void drawSensorError() {
  display.setFont(u8g2_font_6x10_tf);
  drawCentered(14, "BME280 no encontrado");
  display.drawHLine(0, 18, 128);
  drawCentered(32, "Revisa los cables:");
  drawCentered(44, "SDA -> 41  SCL -> 42");
  drawCentered(56, "VCC -> 3V3  GND -> GND");
}

// ---------- Claude ----------

void drawClawd(int x0, int y0, bool armsUp, bool squash, bool eyesClosed) {
  for (int r = 0; r < CLAWD_ROWS; r++) {
    for (int c = 0; c < CLAWD_COLS; c++) {
      if (CLAWD[r][c] == '#') {
        int h = (r == CLAWD_ROWS - 1 && squash) ? CLAWD_SY - 3 : CLAWD_SY;
        display.drawBox(x0 + c * CLAWD_SX, y0 + r * CLAWD_SY, CLAWD_SX, h);
      }
    }
  }
  // Brazos levantados: desde los extremos de los brazos hacia arriba
  if (armsUp) {
    display.drawBox(x0 + 1 * CLAWD_SX, y0, CLAWD_SX, 2 * CLAWD_SY);
    display.drawBox(x0 + 16 * CLAWD_SX, y0, CLAWD_SX, 2 * CLAWD_SY);
  }
  // Ojos cerrados: se rellena el hueco dejando una rayita
  if (eyesClosed) {
    for (int c : {5, 12}) {
      display.drawBox(x0 + c * CLAWD_SX, y0 + CLAWD_SY, CLAWD_SX, CLAWD_SY);
      display.setDrawColor(0);
      display.drawHLine(x0 + c * CLAWD_SX, y0 + CLAWD_SY + CLAWD_SY / 2, CLAWD_SX);
      display.setDrawColor(1);
    }
  }
}

// Destello estilo spinner de Claude Code: crece y se encoge
void drawSparkle(int cx, int cy, int frame) {
  static const int sizes[] = {1, 2, 3, 4, 3, 2};
  int s = sizes[frame % 6];
  display.drawLine(cx - s, cy, cx + s, cy);
  display.drawLine(cx, cy - s, cx, cy + s);
  if (s > 1) {
    int d = s - 1;
    display.drawLine(cx - d, cy - d, cx + d, cy + d);
    display.drawLine(cx - d, cy + d, cx + d, cy - d);
  }
}

void drawClaudeScreen(uint32_t now) {
  int frame = now / CLAUDE_FRAME_MS;
  bool blink = (now % 3500) < 150;
  const int x0 = (128 - CLAWD_COLS * CLAWD_SX) / 2, y0 = 9;

  // Temperatura pequeña arriba a la derecha
  if (sensorOk) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f°C", temperature);
    display.setFont(u8g2_font_5x7_tf);
    display.drawUTF8(128 - display.getUTF8Width(buf), 7, buf);
  }

  display.setFont(u8g2_font_helvB10_tf);
  if (claude == CLAUDE_THINKING) {
    bool up = (frame / 2) % 2;
    drawClawd(x0, y0 + (up ? 0 : 2), false, !up, blink);
    const char *label = "Pensando";
    int w = display.getStrWidth(label);
    int tx = (128 - w - 22) / 2;
    drawSparkle(tx + 4, 56, frame);
    display.drawStr(tx + 12, 61, label);
    for (int i = 0; i < (frame / 3) % 4; i++) display.drawBox(tx + 13 + w + i * 4, 59, 2, 2);
  } else if (claude == CLAUDE_PERMISSION) {
    bool on = (now / 400) % 2;
    drawClawd(x0, y0 + (on ? 0 : 1), true, false, false);
    // Exclamación sobre la cabeza
    display.drawBox(4, 12, 4, 14);
    display.drawBox(4, 29, 4, 4);
    display.drawBox(120, 12, 4, 14);
    display.drawBox(120, 29, 4, 4);
    const char *label = "Necesita permiso";
    int w = display.getStrWidth(label);
    if (on) {
      display.drawBox((128 - w) / 2 - 3, 49, w + 6, 15);
      display.setDrawColor(0);
    }
    display.drawStr((128 - w) / 2, 61, label);
    display.setDrawColor(1);
  } else {
    drawClawd(x0, y0 + 2, false, false, blink);
    const char *label = "Terminado";
    int w = display.getStrWidth(label);
    int tx = (128 - w - 14) / 2;
    // Check
    display.drawLine(tx, 55, tx + 3, 59);
    display.drawLine(tx + 1, 55, tx + 4, 59);
    display.drawLine(tx + 3, 59, tx + 9, 51);
    display.drawLine(tx + 4, 59, tx + 10, 51);
    display.drawStr(tx + 14, 61, label);
  }
}

void setClaude(ClaudeState state) {
  claude = state;
  claudeSince = millis();
  digitalWrite(LED_PIN, LOW);
  dirty = true;
}

void handleSerial() {
  static char line[32];
  static int len = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (len < (int)sizeof(line) - 1) line[len++] = ch;
      continue;
    }
    line[len] = '\0';
    len = 0;
    if (strncmp(line, "S:", 2) != 0) continue;
    const char *s = line + 2;
    if (!strcmp(s, "thinking")) setClaude(CLAUDE_THINKING);
    else if (!strcmp(s, "permission")) setClaude(CLAUDE_PERMISSION);
    else if (!strcmp(s, "done")) setClaude(CLAUDE_DONE);
    else if (!strcmp(s, "off")) setClaude(CLAUDE_OFF);
    else continue;
    Serial.printf("claude=%s up=%lus\n", s, millis() / 1000);
  }
}

void render() {
  display.clearBuffer();
  if (claude != CLAUDE_OFF) drawClaudeScreen(millis());
  else if (!sensorOk) drawSensorError();
  else if (page == PAGE_MAIN) drawMainPage();
  else if (page == PAGE_GRAPH) drawGraphPage();
  else drawStatsPage();
  display.sendBuffer();
}

// ---------- Programa ----------

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  display.begin();
  display.setBusClock(400000);
  display.enableUTF8Print();

  display.clearBuffer();
  display.setFont(u8g2_font_helvB10_tf);
  drawCentered(28, "Estacion");
  drawCentered(44, "ambiental");
  display.sendBuffer();

  Wire1.begin(BME_SDA, BME_SCL);
  sensorOk = initSensor();
  if (sensorOk) {
    Serial.printf("BME280 encontrado en 0x%02X\n", sensorAddr);
    readSensor();
    pushHistory(temperature);
  } else {
    Serial.println("BME280 no encontrado, revisa el cableado");
  }

  delay(1200);
  lastRead = lastHistory = millis();
}

void loop() {
  uint32_t now = millis();

  handleSerial();

  // Botón PRG: cambia de pantalla, o cierra la pantalla de Claude
  static bool lastButton = HIGH;
  static uint32_t lastPress = 0;
  bool button = digitalRead(BUTTON_PIN);
  if (button == LOW && lastButton == HIGH && now - lastPress > 200) {
    if (claude != CLAUDE_OFF) setClaude(CLAUDE_OFF);
    else page = (page + 1) % PAGE_COUNT;
    lastPress = now;
    dirty = true;
  }
  lastButton = button;

  if (claude == CLAUDE_DONE && now - claudeSince > CLAUDE_DONE_TIMEOUT_MS) setClaude(CLAUDE_OFF);
  if (claude == CLAUDE_THINKING && now - claudeSince > CLAUDE_IDLE_TIMEOUT_MS) setClaude(CLAUDE_OFF);

  // Animación de Claude; el LED parpadea cuando necesita permiso
  static uint32_t lastFrame = 0;
  if (claude != CLAUDE_OFF && now - lastFrame >= CLAUDE_FRAME_MS) {
    lastFrame = now;
    digitalWrite(LED_PIN, claude == CLAUDE_PERMISSION && (now / 400) % 2);
    dirty = true;
  }

  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;
    if (!sensorOk) sensorOk = initSensor();
    if (sensorOk) readSensor();
    dirty = true;
  }

  if (sensorOk && now - lastHistory >= HISTORY_INTERVAL_MS) {
    lastHistory = now;
    pushHistory(temperature);
  }

  // La pantalla de resumen muestra el tiempo encendido, se refresca cada segundo
  static uint32_t lastTick = 0;
  if (page == PAGE_STATS && now - lastTick >= 1000) {
    lastTick = now;
    dirty = true;
  }

  if (dirty) {
    render();
    dirty = false;
  }

  delay(10);
}
