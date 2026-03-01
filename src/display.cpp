#include "display.h"
#include "config.h"
#include <SPIFFS.h>
#include <time.h>

static M5EPD_Canvas canvas(&M5.EPD);
static bool fontLoaded = false;

static const char* WEEKDAY_JP[] = {"日", "月", "火", "水", "木", "金", "土"};

static const int SCREEN_W = 540;
static const int SCREEN_H = 960;
static const int BLOCK_GAP_Y = 48;
static const int MARGIN = 24;

static const int FONT_DATE   = 32;
static const int FONT_BATT   = 24;
static const int FONT_SECT   = 32;
static const int FONT_TAG    = 20;
static const int FONT_LABEL  = 24;
static const int FONT_VALUE  = 80;
static const int FONT_CO2VAL = 104;
static const int FONT_CO2LBL = 24;
static const int FONT_UNIT   = 36;
static const int FONT_UPDATE = 20;

static const int COL_BLACK = 15;
static const int COL_WHITE = 0;
static const int COL_GRAY_BG = 3;

static void drawBatteryIcon(int x, int y, int pct) {
    canvas.drawRect(x, y, 28, 16, COL_BLACK);
    canvas.fillRect(x + 28, y + 4, 4, 8, COL_BLACK);
    int fillW = (int)(24.0f * pct / 100.0f);
    if (fillW < 1) fillW = 1;
    canvas.fillRect(x + 2, y + 2, fillW, 12, COL_BLACK);
}

static void drawCO2Face(int cx, int cy, int r, uint16_t co2) {
    canvas.drawCircle(cx, cy, r, COL_BLACK);
    canvas.drawCircle(cx, cy, r - 1, COL_BLACK);

    int eyeY = cy - r / 3;
    int eyeGap = r / 3;
    canvas.fillCircle(cx - eyeGap, eyeY, 3, COL_BLACK);
    canvas.fillCircle(cx + eyeGap, eyeY, 3, COL_BLACK);

    int mouthY = cy + r / 4;
    int mouthW = r / 3;
    if (co2 <= CO2_GOOD) {
        // 笑顔 (口が下向きのU字)
        for (int dx = -mouthW; dx <= mouthW; dx++) {
            int dy = -(dx * dx * 4) / (mouthW * mouthW);
            canvas.drawPixel(cx + dx, mouthY + 6 + dy, COL_BLACK);
            canvas.drawPixel(cx + dx, mouthY + 7 + dy, COL_BLACK);
        }
    } else if (co2 <= CO2_WARNING) {
        // 注意 (まっすぐ)
        canvas.drawLine(cx - mouthW, mouthY + 3, cx + mouthW, mouthY + 3, COL_BLACK);
        canvas.drawLine(cx - mouthW, mouthY + 4, cx + mouthW, mouthY + 4, COL_BLACK);
    } else {
        // 危険 (口が上向きの逆U字)
        for (int dx = -mouthW; dx <= mouthW; dx++) {
            int dy = (dx * dx * 4) / (mouthW * mouthW);
            canvas.drawPixel(cx + dx, mouthY + dy, COL_BLACK);
            canvas.drawPixel(cx + dx, mouthY + dy + 1, COL_BLACK);
        }
    }
}

static void drawTagRight(int rightX, int y, const char* text) {
    canvas.setTextSize(FONT_TAG);
    canvas.setTextDatum(TR_DATUM);
    int px = 12, py = 6;
    int tw = canvas.drawString(text, rightX - px, y + py + 4); // 日本語フォントの中心線のずれを調整 (TODO: フォントサイズを考慮する)
    int th = FONT_TAG + 4;
    int totalW = tw + px * 2;
    canvas.drawRoundRect(rightX - totalW, y, totalW, th + py * 2, 8, COL_BLACK);
}

static void drawLabeledValue(int x, int y, int colW,
                             const char* label, const char* value,
                             const char* unit) {
    canvas.setTextSize(FONT_LABEL);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(label, x, y);

    canvas.setTextSize(FONT_VALUE);
    canvas.setTextDatum(TL_DATUM);
    int vw = canvas.drawString(value, x, y + FONT_LABEL + 8);

    canvas.setTextSize(FONT_UNIT);
    canvas.setTextDatum(BL_DATUM);
    canvas.drawString(unit, x + vw + 4, y + FONT_LABEL + 8 + FONT_VALUE);
}

static bool isDataFresh(const DisplaySensorData& data, time_t now) {
    if (!data.hasData || data.updateTimestamp == 0) return false;
    return (now - data.updateTimestamp) <= DATA_STALE_SEC;
}

static void drawLastUpdated(int y, const DisplaySensorData& data) {
    canvas.setTextSize(FONT_UPDATE);
    canvas.setTextDatum(TR_DATUM);
    if (data.hasData && data.updateTimestamp != 0) {
        struct tm t;
        localtime_r(&data.updateTimestamp, &t);
        char buf[32];
        snprintf(buf, sizeof(buf), "最終更新: %02d:%02d", t.tm_hour, t.tm_min);
        canvas.drawString(buf, SCREEN_W - MARGIN, y);
    }
}

void displayInit(bool fullClear) {
    M5.EPD.SetRotation(90);
    if (fullClear) {
        M5.EPD.Clear(true);
    }

    canvas.createCanvas(SCREEN_W, SCREEN_H);
    canvas.setTextColor(COL_BLACK, COL_WHITE);

    if (SPIFFS.begin(true)) {
        if (SPIFFS.exists("/GenSenRounded-R.ttf")) {
            canvas.loadFont("/GenSenRounded-R.ttf", SPIFFS);
            fontLoaded = true;
            Serial.println("[Display] TTF font loaded from SPIFFS");
        } else {
            Serial.println("[Display] Font not found in SPIFFS, using default");
        }
    }

    if (fontLoaded) {
        canvas.createRender(FONT_DATE, 128);
        canvas.createRender(FONT_BATT, 64);
        canvas.createRender(FONT_SECT, 128);
        canvas.createRender(FONT_TAG, 128);
        canvas.createRender(FONT_LABEL, 128);
        canvas.createRender(FONT_VALUE, 32);
        canvas.createRender(FONT_CO2VAL, 16);
        canvas.createRender(FONT_CO2LBL, 64);
        canvas.createRender(FONT_UNIT, 32);
        canvas.createRender(FONT_UPDATE, 64);
    }
}

void displayDraw(const DisplaySensorData& co2, const DisplaySensorData& outdoor,
                 time_t nowEpoch,
                 int year, int month, int day, int wday, int batteryPct) {
    canvas.fillCanvas(COL_WHITE);
    canvas.setTextColor(COL_BLACK, COL_WHITE);

    // ========== Header ==========
    canvas.setTextSize(FONT_DATE);
    canvas.setTextDatum(TL_DATUM);
    char dateBuf[32];
    snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d (%s)",
             year, month, day, WEEKDAY_JP[wday % 7]);
    canvas.drawString(dateBuf, MARGIN, 20);

    canvas.setTextSize(FONT_BATT);
    canvas.setTextDatum(TR_DATUM);
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", batteryPct);
    canvas.drawString(battBuf, SCREEN_W - MARGIN - 36, 24);
    drawBatteryIcon(SCREEN_W - MARGIN - 32, 26, batteryPct);

    int divY1 = 68;
    canvas.fillRect(MARGIN, divY1, SCREEN_W - MARGIN * 2, 4, COL_BLACK);

    // ========== CO2 Sensor Section ==========
    int secY = divY1 + BLOCK_GAP_Y;

    canvas.setTextSize(FONT_SECT);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(CO2_SENSOR_LOCATION, MARGIN + 8, secY);
    drawTagRight(SCREEN_W - MARGIN, secY - 2, "CO2センサー");

    int cardX = MARGIN;
    int cardY = secY + 50;
    int cardW = SCREEN_W - MARGIN * 2;
    int cardH = 180;
    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 10, COL_GRAY_BG);

    canvas.setTextColor(COL_BLACK, COL_GRAY_BG);
    canvas.setTextSize(FONT_CO2LBL);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("CO2濃度", cardX + 20, cardY + 14);

    bool co2Fresh = isDataFresh(co2, nowEpoch);
    if (co2Fresh) {
        char co2Buf[8];
        snprintf(co2Buf, sizeof(co2Buf), "%d", co2.co2);
        canvas.setTextSize(FONT_CO2VAL);
        canvas.setTextDatum(BL_DATUM);
        int co2W = canvas.drawString(co2Buf, cardX + 60, cardY + cardH - 14);

        canvas.setTextSize(FONT_SECT);
        canvas.setTextDatum(BL_DATUM);
        canvas.drawString("ppm", cardX + 60 + co2W + 6, cardY + cardH - 20);

        drawCO2Face(cardX + cardW - 65, cardY + cardH / 2, 40, co2.co2);
    } else {
        canvas.setTextSize(FONT_VALUE);
        canvas.setTextDatum(BL_DATUM);
        int w = canvas.drawString("---", cardX + 60, cardY + cardH - 24);
        canvas.setTextSize(FONT_SECT);
        canvas.drawString("ppm", cardX + 60 + w + 6, cardY + cardH - 28);
    }

    canvas.setTextColor(COL_BLACK, COL_WHITE);

    int rowY = cardY + cardH + 20;
    int halfW = (SCREEN_W - MARGIN * 2) / 2;

    {
        char buf[8];
        if (co2Fresh) snprintf(buf, sizeof(buf), "%.1f", co2.temperature);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + 8, rowY, halfW, "気温", buf, "°C");
    }
    {
        char buf[8];
        if (co2Fresh) snprintf(buf, sizeof(buf), "%.1f", co2.humidity);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + halfW, rowY, halfW, "湿度", buf, "%");
    }

    int updateY = rowY + FONT_LABEL + 8 + FONT_VALUE + 12;
    drawLastUpdated(updateY, co2);

    // ========== Divider ==========
    int divY2 = updateY + BLOCK_GAP_Y;
    canvas.drawLine(MARGIN, divY2, SCREEN_W - MARGIN, divY2, COL_BLACK);

    // ========== Outdoor Section ==========
    int sec2Y = divY2 + BLOCK_GAP_Y;

    canvas.setTextSize(FONT_SECT);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(OUTDOOR_METER_LOCATION, MARGIN + 8, sec2Y);
    drawTagRight(SCREEN_W - MARGIN, sec2Y - 2, "防水温湿度計");

    int row2Y = sec2Y + 50;

    bool outdoorFresh = isDataFresh(outdoor, nowEpoch);
    {
        char buf[8];
        if (outdoorFresh) snprintf(buf, sizeof(buf), "%.1f", outdoor.temperature);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + 8, row2Y, halfW, "気温", buf, "°C");
    }
    {
        char buf[8];
        if (outdoorFresh) snprintf(buf, sizeof(buf), "%.1f", outdoor.humidity);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + halfW, row2Y, halfW, "湿度", buf, "%");
    }

    int update2Y = row2Y + FONT_LABEL + 8 + FONT_VALUE + 12;
    drawLastUpdated(update2Y, outdoor);

    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}
