#include "display.h"
#include "config.h"
#include <SPIFFS.h>
#include <time.h>

static M5EPD_Canvas canvas(&M5.EPD);
static bool fontLoaded = false;

static const char* WEEKDAY_JP[] = {"日", "月", "火", "水", "木", "金", "土"};

static const int SCREEN_W = 540;
static const int SCREEN_H = 960;
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
static const int FONT_FORECAST = 52;
static const int FONT_DAILY_RAIN = 84;
static const int FONT_WINDOW_RAIN = 52;
static const int WEATHER_TOP = 716;

static const int COL_BLACK = 15;
static const int COL_WHITE = 0;
static const int COL_GRAY_BG = 1;
static const int COL_GRAY_DIV = 6;

static void setFontSize(int size) {
    canvas.setTextSize(size);
    // M5EPD resets these margins on every size change, including the same size.
    // Keep each text background within its own row so it cannot erase labels.
    canvas.setTextFillMargin(0, 0, 0, 0);
}

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
    setFontSize(FONT_TAG);
    canvas.setTextDatum(TR_DATUM);
    int px = 12, py = 6;
    int tw = canvas.drawString(text, rightX - px, y + py + 4); // 日本語フォントの中心線のずれを調整 (TODO: フォントサイズを考慮する)
    int th = FONT_TAG + 4;
    int totalW = tw + px * 2;
    canvas.drawRoundRect(rightX - totalW, y, totalW, th + py * 2, 8, COL_BLACK);
}

static void drawLabeledValue(int x, int y,
                             const char* label, const char* value,
                             const char* unit) {
    setFontSize(FONT_LABEL);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(label, x, y);

    setFontSize(FONT_VALUE);
    canvas.setTextDatum(TL_DATUM);
    int vw = canvas.drawString(value, x, y + FONT_LABEL + 8);

    setFontSize(FONT_UNIT);
    canvas.setTextDatum(BL_DATUM);
    canvas.drawString(unit, x + vw + 4, y + FONT_LABEL + 8 + FONT_VALUE);
}

static void drawSun(int cx, int cy, int radius) {
    for (int d = 0; d < 4; ++d) canvas.drawCircle(cx, cy, radius - d, COL_BLACK);
    const int inner = radius + 6;
    const int outer = radius + 14;
    for (int d = -1; d <= 1; ++d) {
        canvas.drawLine(cx + inner, cy + d, cx + outer, cy + d, COL_BLACK);
        canvas.drawLine(cx - inner, cy + d, cx - outer, cy + d, COL_BLACK);
        canvas.drawLine(cx + d, cy + inner, cx + d, cy + outer, COL_BLACK);
        canvas.drawLine(cx + d, cy - inner, cx + d, cy - outer, COL_BLACK);
        for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
            canvas.drawLine(cx + sx * (inner * 7 / 10) + d, cy + sy * (inner * 7 / 10),
                            cx + sx * (outer * 7 / 10) + d, cy + sy * (outer * 7 / 10), COL_BLACK);
        }
    }
}

static void drawCloud(int x, int y) {
    // Paint the silhouette, then its inset, so the lobes share one outline.
    for (int inset : {0, 4}) {
        const int color = inset == 0 ? COL_BLACK : COL_WHITE;
        canvas.fillCircle(x + 25, y + 41, 19 - inset, color);
        canvas.fillCircle(x + 47, y + 30, 25 - inset, color);
        canvas.fillCircle(x + 71, y + 42, 18 - inset, color);
        canvas.fillRect(x + 25, y + 38, 47, 22 - inset, color);
    }
}

static void drawWeatherIcon(int x, int y, WeatherIcon icon) {
    if (icon == WeatherIcon::Unknown) {
        setFontSize(FONT_FORECAST);
        canvas.setTextDatum(CC_DATUM);
        canvas.drawString("--", x + 48, y + 48);
        return;
    }
    if (icon == WeatherIcon::Sun) {
        drawSun(x + 48, y + 48, 24);
        return;
    }
    if (icon == WeatherIcon::PartlyCloudy) drawSun(x + 25, y + 26, 12);
    drawCloud(x, y + (icon == WeatherIcon::PartlyCloudy ? 14 : 4));
    if (icon == WeatherIcon::Rain) {
        for (int dx : {26, 48, 70}) canvas.fillRoundRect(x + dx, y + 71, 5, 20, 2, COL_BLACK);
    } else if (icon == WeatherIcon::Snow) {
        for (int dx : {26, 48, 70}) {
            canvas.fillRect(x + dx - 1, y + 73, 3, 15, COL_BLACK);
            canvas.fillRect(x + dx - 7, y + 79, 15, 3, COL_BLACK);
            canvas.drawLine(x + dx - 5, y + 75, x + dx + 5, y + 85, COL_BLACK);
            canvas.drawLine(x + dx - 5, y + 85, x + dx + 5, y + 75, COL_BLACK);
        }
    } else if (icon == WeatherIcon::Fog) {
        canvas.fillRoundRect(x + 12, y + 73, 72, 4, 2, COL_BLACK);
        canvas.fillRoundRect(x + 20, y + 85, 56, 4, 2, COL_BLACK);
    } else if (icon == WeatherIcon::Thunder) {
        canvas.fillTriangle(x + 49, y + 67, x + 35, y + 82, x + 53, y + 82, COL_BLACK);
        canvas.fillTriangle(x + 43, y + 78, x + 61, y + 78, x + 43, y + 96, COL_BLACK);
    }
}

static void drawWeatherValue(int centerX, int bottomY, int size, const char* value, const char* unit) {
    // M5EPD's textWidth() returns zero for TTF fonts; use drawString's advance.
    const int numberCenter = centerX - (FONT_LABEL + 3) / 2;
    setFontSize(size);
    canvas.setTextDatum(BC_DATUM);
    const int valueWidth = canvas.drawString(value, numberCenter, bottomY);
    setFontSize(FONT_LABEL);
    canvas.setTextDatum(BL_DATUM);
    canvas.drawString(unit, numberCenter + (valueWidth + 1) / 2 + 3,
                      bottomY - (size - FONT_LABEL) / 4);
}

static void drawForecastTemperature(int centerX, const char* label, bool available, int value) {
    setFontSize(FONT_LABEL);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(label, centerX, WEATHER_TOP + 140);
    char text[12];
    if (available) snprintf(text, sizeof(text), "%d", value);
    else snprintf(text, sizeof(text), "--");
    drawWeatherValue(centerX, WEATHER_TOP + 224, FONT_FORECAST, text, "°C");
}

static void drawProbability(int centerX, int bottomY, int size, int probability) {
    char text[8];
    if (probability >= 0) snprintf(text, sizeof(text), "%d", probability);
    else snprintf(text, sizeof(text), "--");
    drawWeatherValue(centerX, bottomY, size, text, "%");
}

static void drawWeather(const DisplayWeatherData& weather) {
    canvas.fillRect(MARGIN, WEATHER_TOP, SCREEN_W - 2 * MARGIN, 2, COL_BLACK);
    setFontSize(FONT_SECT);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(weather.tomorrow ? "明日の天気" : "今日の天気", MARGIN, WEATHER_TOP + 12);
    // Two columns share a bottom row: temperatures left, four-hour rain right.
    drawWeatherIcon(90, WEATHER_TOP + 44, weather.icon);
    drawForecastTemperature(82, "最高", weather.hasMaxTemp, weather.maxTemp);
    drawForecastTemperature(204, "最低", weather.hasMinTemp, weather.minTemp);
    canvas.drawLine(270, WEATHER_TOP + 50, 270, WEATHER_TOP + 210, COL_GRAY_DIV);
    setFontSize(FONT_LABEL);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(weather.tomorrow ? "明日の降水確率" : "今日の降水確率", 400, WEATHER_TOP + 18);
    drawProbability(400, WEATHER_TOP + 140, FONT_DAILY_RAIN, weather.probability);
    canvas.drawLine(400, WEATHER_TOP + 142, 400, WEATHER_TOP + 210, COL_GRAY_DIV);

    for (int i = 0; i < 2; ++i) {
        const int centerX = 342 + i * 116;
        setFontSize(FONT_LABEL);
        canvas.setTextDatum(TC_DATUM);
        canvas.drawString(weather.windowLabel[i], centerX, WEATHER_TOP + 140);
        drawProbability(centerX, WEATHER_TOP + 224, FONT_WINDOW_RAIN, weather.windowProbability[i]);
    }
    char updated[40] = "最終更新: --:--";
    if (weather.updatedAt != 0) {
        struct tm t;
        localtime_r(&weather.updatedAt, &t);
        snprintf(updated, sizeof(updated), "最終更新: %02d:%02d", t.tm_hour, t.tm_min);
    }
    setFontSize(FONT_UPDATE);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("Open-Meteo.com", MARGIN, 938);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(updated, SCREEN_W - MARGIN, 938);
}

static bool isDataFresh(const DisplaySensorData& data, time_t now) {
    if (!data.hasData || data.updateTimestamp == 0) return false;
    const time_t age = now - data.updateTimestamp;
    return age >= 0 && age <= DATA_STALE_SEC;
}

static void drawLastUpdated(int y, const DisplaySensorData& data) {
    setFontSize(FONT_UPDATE);
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
        canvas.createRender(FONT_FORECAST, 16);
        canvas.createRender(FONT_DAILY_RAIN, 16);
        if (FONT_WINDOW_RAIN != FONT_FORECAST) canvas.createRender(FONT_WINDOW_RAIN, 16);
    }
}

void displayDraw(const DisplaySensorData& co2, const DisplaySensorData& outdoor,
                 const DisplayWeatherData& weather,
                 time_t nowEpoch,
                 int year, int month, int day, int wday, int batteryPct) {
    canvas.fillCanvas(COL_WHITE);
    canvas.setTextColor(COL_BLACK, COL_WHITE);

    // ========== Header ==========
    setFontSize(FONT_DATE);
    canvas.setTextDatum(TL_DATUM);
    char dateBuf[32];
    snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d (%s)",
             year, month, day, WEEKDAY_JP[wday % 7]);
    canvas.drawString(dateBuf, MARGIN, 20);

    setFontSize(FONT_BATT);
    canvas.setTextDatum(TR_DATUM);
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", batteryPct);
    canvas.drawString(battBuf, SCREEN_W - MARGIN - 36, 24);
    drawBatteryIcon(SCREEN_W - MARGIN - 32, 26, batteryPct);

    int divY1 = 68;
    canvas.fillRect(MARGIN, divY1, SCREEN_W - MARGIN * 2, 4, COL_BLACK);

    // ========== CO2 Sensor Section ==========
    int secY = 100;

    setFontSize(FONT_SECT);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(CO2_SENSOR_LOCATION, MARGIN + 8, secY);
    drawTagRight(SCREEN_W - MARGIN, secY - 2, "CO2センサー");

    int cardX = MARGIN;
    int cardY = 144;
    int cardW = SCREEN_W - MARGIN * 2;
    int cardH = 164;
    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 10, COL_GRAY_BG);

    canvas.setTextColor(COL_BLACK, COL_GRAY_BG);
    setFontSize(FONT_CO2LBL);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("CO2濃度", cardX + 20, cardY + 14);

    bool co2Fresh = isDataFresh(co2, nowEpoch);
    if (co2Fresh) {
        char co2Buf[8];
        snprintf(co2Buf, sizeof(co2Buf), "%d", co2.co2);
        setFontSize(FONT_CO2VAL);
        canvas.setTextDatum(BL_DATUM);
        int co2W = canvas.drawString(co2Buf, cardX + 60, cardY + cardH - 14);

        setFontSize(FONT_SECT);
        canvas.setTextDatum(BL_DATUM);
        canvas.drawString("ppm", cardX + 60 + co2W + 6, cardY + cardH - 20);

        drawCO2Face(cardX + cardW - 65, cardY + cardH / 2, 34, co2.co2);
    } else {
        setFontSize(FONT_VALUE);
        canvas.setTextDatum(BL_DATUM);
        int w = canvas.drawString("---", cardX + 60, cardY + cardH - 24);
        setFontSize(FONT_SECT);
        canvas.drawString("ppm", cardX + 60 + w + 6, cardY + cardH - 28);
    }

    canvas.setTextColor(COL_BLACK, COL_WHITE);

    int rowY = cardY + cardH + 20;
    int halfW = (SCREEN_W - MARGIN * 2) / 2;
    int divPad = 28;
    int divLineW = 3;

    int valH = FONT_LABEL + 8 + FONT_VALUE;

    {
        char buf[8];
        if (co2Fresh) snprintf(buf, sizeof(buf), "%.1f", co2.temperature);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + 8, rowY, "気温", buf, "°C");
    }

    int divLineH = valH * 2 / 3;
    int divLineTop = rowY + (valH - divLineH) / 2;
    canvas.fillRect(MARGIN + halfW - divLineW / 2, divLineTop, divLineW, divLineH, COL_GRAY_DIV);

    {
        char buf[8];
        if (co2Fresh) snprintf(buf, sizeof(buf), "%d", (int)co2.humidity);
        else snprintf(buf, sizeof(buf), "--");
        drawLabeledValue(MARGIN + halfW + divPad, rowY, "湿度", buf, "%");
    }

    int updateY = rowY + valH + 12;
    drawLastUpdated(updateY, co2);

    // ========== Divider ==========
    int divY2 = 488;
    canvas.drawLine(MARGIN, divY2, SCREEN_W - MARGIN, divY2, COL_BLACK);

    // ========== Outdoor Section ==========
    int sec2Y = 512;

    setFontSize(FONT_SECT);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(OUTDOOR_METER_LOCATION, MARGIN + 8, sec2Y);
    drawTagRight(SCREEN_W - MARGIN, sec2Y - 2, "防水温湿度計");

    int row2Y = 560;

    bool outdoorFresh = isDataFresh(outdoor, nowEpoch);
    {
        char buf[8];
        if (outdoorFresh) snprintf(buf, sizeof(buf), "%.1f", outdoor.temperature);
        else snprintf(buf, sizeof(buf), "--.-");
        drawLabeledValue(MARGIN + 8, row2Y, "気温", buf, "°C");
    }

    int divLine2Top = row2Y + (valH - divLineH) / 2;
    canvas.fillRect(MARGIN + halfW - divLineW / 2, divLine2Top, divLineW, divLineH, COL_GRAY_DIV);

    {
        char buf[8];
        if (outdoorFresh) snprintf(buf, sizeof(buf), "%d", (int)outdoor.humidity);
        else snprintf(buf, sizeof(buf), "--");
        drawLabeledValue(MARGIN + halfW + divPad, row2Y, "湿度", buf, "%");
    }

    int update2Y = row2Y + valH + 12;
    drawLastUpdated(update2Y, outdoor);

    drawWeather(weather);
    canvas.pushCanvas(0, 0, UPDATE_MODE_GL16);
}
