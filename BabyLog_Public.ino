
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "secrets.h"

const char* TIME_ZONE = "JST-9";

// 色
const uint16_t COLOR_BREAST = 0xF6B9;
const uint16_t COLOR_SLEEP  = 0xF6CD;
const uint16_t COLOR_PEE    = 0xF6C9;
const uint16_t COLOR_POOP   = 0xEED9;

// 記録の種類
enum EventType {
  PEE = 0,
  POOP = 1,
  BREASTFEED = 2,
  SLEEP = 3,
  WAKEUP = 4
};


struct Record {
  EventType type;
  time_t timestamp;
  uint32_t leftSeconds;
  uint32_t rightSeconds;
};

const int MAX_RECORDS = 100;

Record records[MAX_RECORDS];
int recordCount = 0;

Preferences preferences;

const uint32_t STORE_MAGIC = 0x50494F59;


// 保存形式
struct StoreData {
  uint32_t magic;
  uint16_t count;
  Record records[MAX_RECORDS];
};

// ホーム画面のボタン
struct Button {
  const char* label;
  int x, y, w, h;
};

Button buttons[4];

// 画面状態
bool breastfeedingScreen = false;
bool historyScreen = false;
int historyScrollY = 0;
int historyLastY = 0;

unsigned long lastElapsedRefresh = 0;

// 授乳タイマー状態
bool sessionInProgress = false;
int activeSide = 0;  // 0:停止 1:左 2:右
uint8_t lastSide = 0;

uint32_t leftElapsedMs = 0;
uint32_t rightElapsedMs = 0;
uint32_t sideStartedAt = 0;

time_t sessionStartTime = 0;


// 授乳中の途中経過を保存するデータ
const uint32_t BF_SESSION_MAGIC = 0x42465331;

struct BreastfeedingSessionData {
  uint32_t magic;
  bool inProgress;
  time_t startTime;
  uint32_t leftMs;
  uint32_t rightMs;
  uint8_t lastSide;
};

// 自動保存の間隔
unsigned long lastSessionSave = 0;
const unsigned long SESSION_SAVE_INTERVAL = 10000;


// 画面更新
unsigned long lastRefresh = 0;

// ------------------------------
// 保存機能
// ------------------------------

void saveRecords() {
  StoreData data = {};

  data.magic = STORE_MAGIC;
  data.count = recordCount;

  for (int i = 0; i < recordCount; i++) {
    data.records[i] = records[i];
  }

  size_t written = preferences.putBytes(
    "data", &data, sizeof(data)
  );

  if (written != sizeof(data)) {
    Serial.println("保存に失敗しました");
  }
}

void loadRecords() {
  size_t storedSize =
    preferences.getBytesLength("data");

  if (storedSize == sizeof(StoreData)) {
    static StoreData data = {};

    size_t readSize = preferences.getBytes(
      "data", &data, sizeof(data)
    );

    if (readSize == sizeof(data) &&
        data.magic == STORE_MAGIC  &&
        data.count <= MAX_RECORDS) {

      recordCount = data.count;

      for (int i = 0; i < recordCount; i++) {
        records[i] = data.records[i];
      }

      Serial.println("記録を復元しました");
      return;
    }
  }

  Serial.println("保存データなし");
}

// ------------------------------
// 日付・時刻
// ------------------------------

bool isToday(time_t t) {
  struct tm eventTime;
  struct tm nowTime;

  localtime_r(&t, &eventTime);

  time_t now = time(nullptr);
  localtime_r(&now, &nowTime);

  return eventTime.tm_year == nowTime.tm_year &&
         eventTime.tm_yday == nowTime.tm_yday;
}

String formatTime(time_t t) {
  struct tm info;
  localtime_r(&t, &info);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M", &info);

  return String(buffer);
}

String formatDuration(uint32_t milliseconds) {
  uint32_t seconds = milliseconds / 1000;
  uint32_t minutes = seconds / 60;
  seconds %= 60;

  char buffer[20];
  snprintf(
    buffer, sizeof(buffer),
    "%02lu:%02lu",
    (unsigned long)minutes,
    (unsigned long)seconds
  );

  return String(buffer);
}

// ------------------------------
// 記録集計
// ------------------------------

int countToday(EventType type) {
  int count = 0;

  for (int i = 0; i < recordCount; i++) {
    if (records[i].type == type &&
        isToday(records[i].timestamp)) {
      count++;
    }
  }

  return count;
}


String latestUndoText() {
  if (recordCount <= 0) {
    return "取消";
  }

  switch (records[recordCount - 1].type) {
    case PEE:
      return "おしっこ取消";
    case POOP:
      return "うんち取消";
    case BREASTFEED:
      return "授乳取消";
    case SLEEP:
      return "睡眠取消";
    case WAKEUP:
      return "起床取消";
    default:
      return "記録取消";
  }
}

String lastBreastfeedingElapsed() {
  for (int i = recordCount - 1; i >= 0; i--) {
    if (records[i].type == BREASTFEED) {
      time_t now = time(nullptr);
      long elapsed = (long)(now - records[i].timestamp);

      if (elapsed < 0) elapsed = 0;

      int hours = elapsed / 3600;
      int minutes = (elapsed % 3600) / 60;

      return String(hours) + "時間" +
             String(minutes) + "分";
    }
  }

  return "授乳記録なし";
}



bool isSleeping() {
  for (int i = recordCount - 1; i >= 0; i--) {
    if (records[i].type == SLEEP) {
      return true;
    }

    if (records[i].type == WAKEUP) {
      return false;
    }
  }

  return false;
}


// ------------------------------
// 画面描画
// ------------------------------

void drawCenteredText(
  const String& text,
  int x, int y, int w, int h,
  uint16_t color
) {
  M5.Display.setFont(&fonts::lgfxJapanGothic_24);
  M5.Display.setTextColor(color);

  int tw = M5.Display.textWidth(text);
  int th = M5.Display.fontHeight();

  M5.Display.setCursor(
    x + (w - tw) / 2,
    y + (h - th) / 2
  );

  M5.Display.print(text);
}

void drawButton(
  Button b,
  uint16_t color,
  const String& countText = ""
) {
  M5.Display.fillRoundRect(
    b.x, b.y, b.w, b.h, 12, color
  );

  M5.Display.drawRoundRect(
    b.x, b.y, b.w, b.h, 12, TFT_WHITE
  );

  drawCenteredText(
    b.label,
    b.x, b.y + 5, b.w, b.h / 2,
    0x5AEB
  );

  if (countText.length() > 0) {
    M5.Display.setFont(&fonts::lgfxJapanGothic_24);
    M5.Display.setTextColor(0x5AEB);

    int tw = M5.Display.textWidth(countText);

    M5.Display.setCursor(
      b.x + (b.w - tw) / 2,
      b.y + b.h / 2 + 5
    );

    M5.Display.print(countText);
  }
}

void drawHome() {
  breastfeedingScreen = false;

  M5.Display.fillScreen(TFT_WHITE);

  int w = M5.Display.width();
  int margin = 8;
  int gap = 8;

  drawCenteredText(
    formatTime(time(nullptr)),
    0, 0, w, 24, TFT_BLACK
  );

  int bw = (w - margin * 2 - gap) / 2;
  int bh = 68;
  int top = 28;

  buttons[0] = {"授乳", margin, top, bw, bh};
  buttons[1] = {
    isSleeping() ? "起床" : "睡眠",
    margin + bw + gap,
    top,
    bw,
    bh
  };
  buttons[2] = {"おしっこ", margin, top + bh + gap, bw, bh};
  buttons[3] = {"うんち", margin + bw + gap, top + bh + gap, bw, bh};

  drawButton(
    buttons[0], COLOR_BREAST,
    "今日 " + String(countToday(BREASTFEED)) + "回"
  );

  drawButton(
    buttons[1],
    COLOR_SLEEP,
    isSleeping() ? "睡眠中" : "タップで記録"
  );

  drawButton(
    buttons[2], COLOR_PEE,
    "今日 " + String(countToday(PEE)) + "回"
  );

  drawButton(
    buttons[3], COLOR_POOP,
    "今日 " + String(countToday(POOP)) + "回"
  );

  // 前回の授乳からの経過時間
  M5.Display.setFont(&fonts::lgfxJapanGothic_16);
  M5.Display.setTextColor(TFT_BLACK);

  M5.Display.setCursor(8, 184);
  M5.Display.print("前回の授乳から " + lastBreastfeedingElapsed());

  // 記録一覧ボタン
  M5.Display.drawRoundRect(
    8, 210, 140, 24, 5, TFT_DARKGREY
  );
  drawCenteredText(
    "記録一覧", 8, 210, 140, 24, TFT_BLACK
  );

  // 直近の記録を取り消すボタン
  M5.Display.drawRoundRect(
    156, 210, 156, 24, 5, TFT_DARKGREY
  );
  drawCenteredText(
    latestUndoText(), 156, 210, 156, 24, TFT_BLACK
  );
}

// ------------------------------
// 記録一覧画面
// ------------------------------

void drawHistoryScreen() {
  historyScreen = true;
  M5.Display.fillScreen(TFT_WHITE);

  drawCenteredText(
    "記録一覧",
    0, 0, 320, 32, TFT_BLACK
  );

  M5.Display.drawLine(8, 34, 312, 34, TFT_LIGHTGREY);

  const int rowHeight = 40;
  const int listTop = 38;
  const int listBottom = 210;
  const int visibleHeight = listBottom - listTop;

  int maxScroll = max(
    0,
    (int)recordCount * rowHeight - visibleHeight
  );

  historyScrollY = constrain(
    historyScrollY, 0, maxScroll
  );

  for (int i = recordCount - 1; i >= 0; i--) {
    int row = recordCount - 1 - i;
    int y = listTop + row * rowHeight - historyScrollY;

    if (y + rowHeight <= listTop || y >= listBottom) {
      continue;
    }

    struct tm timeInfo;
    localtime_r(&records[i].timestamp, &timeInfo);

    char timeText[20];
    strftime(
      timeText, sizeof(timeText),
      "%m/%d %H:%M", &timeInfo
    );

    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

    M5.Display.setCursor(8, y);
    M5.Display.print(timeText);

    String label = "";

    switch (records[i].type) {
      case PEE:
        label = "おしっこ";
        break;

      case POOP:
        label = "うんち";
        break;

      case SLEEP:
        label = "睡眠開始";
        break;

      case WAKEUP:
        label = "起床";
        break;

      case BREASTFEED:
        label = "授乳";
        break;
    }

    M5.Display.setCursor(180, y);
    M5.Display.print(label);

    if (records[i].type == BREASTFEED) {
      int leftMin =
        (records[i].leftSeconds + 30) / 60;

      int rightMin =
        (records[i].rightSeconds + 30) / 60;

      M5.Display.setCursor(16, y + 19);
      M5.Display.print(
        "左 " + String(leftMin) +
        "分  右 " + String(rightMin) + "分"
      );
    }

    M5.Display.drawLine(
      8, y + rowHeight - 1,
      312, y + rowHeight - 1,
      TFT_LIGHTGREY
    );
  }

  M5.Display.drawRoundRect(
    220, 218, 92, 20, 5, TFT_DARKGREY
  );

  drawCenteredText(
    "戻る", 220, 218, 92, 20, TFT_BLACK
  );
}


void saveBreastfeedingSession() {
  if (!sessionInProgress) return;

  // 保存直前までの時間を積算
  uint32_t now = millis();

  if (activeSide == 1) {
    leftElapsedMs += now - sideStartedAt;
    sideStartedAt = now;
  } else if (activeSide == 2) {
    rightElapsedMs += now - sideStartedAt;
    sideStartedAt = now;
  }

  BreastfeedingSessionData data = {};

  data.magic = BF_SESSION_MAGIC;
  data.inProgress = true;
  data.startTime = sessionStartTime;
  data.leftMs = leftElapsedMs;
  data.rightMs = rightElapsedMs;
  data.lastSide = lastSide;

  preferences.putBytes("bfsess", &data, sizeof(data));

  lastSessionSave = millis();
}

void loadBreastfeedingSession() {
  if (preferences.getBytesLength("bfsess") !=
      sizeof(BreastfeedingSessionData)) {
    return;
  }

  BreastfeedingSessionData data = {};

  size_t size = preferences.getBytes(
    "bfsess", &data, sizeof(data)
  );

  if (size != sizeof(data) ||
      data.magic != BF_SESSION_MAGIC ||
      !data.inProgress) {
    return;
  }

  sessionInProgress = true;
  sessionStartTime = data.startTime;
  leftElapsedMs = data.leftMs;
  rightElapsedMs = data.rightMs;
  lastSide = data.lastSide;

  // 再起動後は必ず一時停止状態にする
  activeSide = 0;

  Serial.println("授乳中の記録を復元しました");
}


// ------------------------------
// 授乳タイマー
// ------------------------------

uint32_t getLeftMs() {
  uint32_t result = leftElapsedMs;

  if (activeSide == 1) {
    result += millis() - sideStartedAt;
  }

  return result;
}

uint32_t getRightMs() {
  uint32_t result = rightElapsedMs;

  if (activeSide == 2) {
    result += millis() - sideStartedAt;
  }

  return result;
}

void stopActiveSide() {
  if (activeSide == 1) {
    leftElapsedMs += millis() - sideStartedAt;
  }

  if (activeSide == 2) {
    rightElapsedMs += millis() - sideStartedAt;
  }

  activeSide = 0;
}


void selectSide(int side) {
  if (activeSide == side) {
    // 同じ側を押したら一時停止
    stopActiveSide();
    saveBreastfeedingSession();
    return;
  }

  // 現在の側を停止
  stopActiveSide();

  // 初回開始日時を記録
  if (!sessionInProgress) {
    sessionInProgress = true;
    sessionStartTime = time(nullptr);
  }

  activeSide = side;
  lastSide = side;
  sideStartedAt = millis();

  saveBreastfeedingSession();
}

void pauseBreastfeeding() {
  stopActiveSide();
  saveBreastfeedingSession();
}

void finishBreastfeeding() {
  stopActiveSide();

  if (sessionInProgress) {
    if (leftElapsedMs > 0 || rightElapsedMs > 0) {
      // 満杯なら一番古い記録を削除
      if (recordCount >= MAX_RECORDS) {
        for (int i = 1; i < MAX_RECORDS; i++) {
          records[i - 1] = records[i];
        }
        recordCount = MAX_RECORDS - 1;
      }

      Record r = {};

      r.type = BREASTFEED;
      r.timestamp = sessionStartTime;
      r.leftSeconds = leftElapsedMs / 1000;
      r.rightSeconds = rightElapsedMs / 1000;

      records[recordCount] = r;
      recordCount++;

      saveRecords();
    }
  }

  sessionInProgress = false;
  activeSide = 0;
  lastSide = 0;

  leftElapsedMs = 0;
  rightElapsedMs = 0;
  sessionStartTime = 0;

  // 途中経過データを削除
  preferences.remove("bfsess");

  drawHome();
}


void drawBreastfeedingScreen() {
  M5.Display.fillScreen(TFT_WHITE);

  drawCenteredText(
    "授乳タイマー",
    0, 0, 320, 32, TFT_BLACK
  );

  // 左右の背景は最初に一度だけ描画
  M5.Display.fillRoundRect(
    8, 38, 148, 108, 12, 0xA6CBE3
  );

  M5.Display.fillRoundRect(
    164, 38, 148, 108, 12, 0xE5B6C5
  );

  drawCenteredText("左", 8, 40, 148, 30, TFT_WHITE);
  drawCenteredText("右", 164, 40, 148, 30, TFT_WHITE);

  // 下部ボタン
  M5.Display.fillRoundRect(
    8, 160, 148, 58, 10, 0xC6C6C6
  );

  drawCenteredText(
    "一時停止", 8, 160, 148, 58, TFT_BLACK
  );

  M5.Display.fillRoundRect(
    164, 160, 148, 58, 10, 0xD47D9B
  );

  drawCenteredText(
    "授乳終了", 164, 160, 148, 58, TFT_WHITE
  );

  updateBreastfeedingDisplay();
}

void updateBreastfeedingDisplay() {
  // 左右の時間表示部分だけを消去
  M5.Display.fillRect(12, 74, 140, 42, 0xA6CBE3);
  M5.Display.fillRect(168, 74, 140, 42, 0xE5B6C5);

  // 時間
  drawCenteredText(
    formatDuration(getLeftMs()),
    12, 74, 140, 42, TFT_WHITE
  );

  drawCenteredText(
    formatDuration(getRightMs()),
    168, 74, 140, 42, TFT_WHITE
  );

  // 左右の選択状態
  M5.Display.fillRect(12, 116, 140, 26, 0xA6CBE3);
  M5.Display.fillRect(168, 116, 140, 26, 0xE5B6C5);

  drawCenteredText(
    activeSide == 1 ? "授乳中" : "タップで開始",
    12, 116, 140, 26, TFT_WHITE
  );

  drawCenteredText(
    activeSide == 2 ? "授乳中" : "タップで開始",
    168, 116, 140, 26, TFT_WHITE
  );

  // 選択中の側を枠で強調
  M5.Display.drawRoundRect(
    8, 38, 148, 108, 12,
    activeSide == 1 ? TFT_BLUE : TFT_WHITE
  );

  M5.Display.drawRoundRect(
    164, 38, 148, 108, 12,
    activeSide == 2 ? TFT_RED : TFT_WHITE
  );
}


// ------------------------------
// 排泄記録
// ------------------------------

void addRecord(EventType type) {
  // 満杯なら一番古い記録を削除
  if (recordCount >= MAX_RECORDS) {
    for (int i = 1; i < MAX_RECORDS; i++) {
      records[i - 1] = records[i];
    }
    recordCount = MAX_RECORDS - 1;
  }

  Record r = {};
  r.type = type;
  r.timestamp = time(nullptr);

  records[recordCount] = r;
  recordCount++;

  saveRecords();
  drawHome();
}

void undoRecord() {
  if (recordCount > 0) {
    recordCount--;
    saveRecords();
    drawHome();
  }
}

// ------------------------------
// Wi-Fi・時刻取得
// ------------------------------

bool connectWiFiAndGetTime() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  M5.Display.fillScreen(TFT_WHITE);

  drawCenteredText(
    "Wi-Fiに接続中...",
    0, 80, 320, 50, TFT_BLACK
  );

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 20000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  configTzTime(
    TIME_ZONE,
    "ntp.nict.jp",
    "pool.ntp.org"
  );

  struct tm timeInfo;

  return getLocalTime(&timeInfo, 15000);
}

// ------------------------------
// 起動
// ------------------------------

void showStartupMessage(const String& message) {
  M5.Display.fillScreen(TFT_WHITE);

  drawCenteredText(
    message, 0, 80, 320, 60, TFT_BLACK
  );

  delay(1200);
}


void setup() {
  Serial.begin(115200);
  delay(500);
  M5.begin();
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::lgfxJapanGothic_16); 

  M5.Display.setRotation(0);

  preferences.begin("babylog", false);

  loadRecords();

  loadBreastfeedingSession();

  bool timeOK = connectWiFiAndGetTime();

  if (timeOK) {
    showStartupMessage("時刻を取得しました！");
  } else {
    showStartupMessage("時刻取得に失敗しました");
  }

  drawHome();
  Serial.println("SETUP DONE");
}


// ------------------------------
// メインループ
// ------------------------------

void loop() {
  M5.update();

  // 授乳中の途中経過を10秒ごとに保存
  if (sessionInProgress &&
      activeSide != 0 &&
      millis() - lastSessionSave >= SESSION_SAVE_INTERVAL) {
    saveBreastfeedingSession();
  }

  if (historyScreen) {
    auto t = M5.Touch.getDetail();

    // タッチ開始
    if (t.wasPressed()) {
      historyLastY = t.y;

      // 戻るボタン
      if (t.x >= 220 && t.y >= 218) {
        historyScreen = false;
        historyScrollY = 0;
        drawHome();
        return;
      }
    }

    // スワイプ中
    if (t.isPressed()) {
      int diff = historyLastY - t.y;

      if (abs(diff) >= 2) {
        historyScrollY += diff;
        historyLastY = t.y;

        drawHistoryScreen();
      }
    }

    return;
  }

  // 授乳画面
  if (breastfeedingScreen) {
    if (millis() - lastRefresh >= 250) {
      lastRefresh = millis();
      updateBreastfeedingDisplay();
    }

    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
      int x = touch.x;
      int y = touch.y;

      // 左
      if (x >= 8 && x <= 156 &&
          y >= 38 && y <= 146) {
        selectSide(1);
        drawBreastfeedingScreen();
      }

      // 右
      else if (x >= 164 && x <= 312 &&
               y >= 38 && y <= 146) {
        selectSide(2);
        drawBreastfeedingScreen();
      }

      // 一時停止
      else if (x >= 8 && x <= 156 &&
               y >= 160 && y <= 218) {
        pauseBreastfeeding();
        drawBreastfeedingScreen();
      }

      // 授乳終了
      else if (x >= 164 && x <= 312 &&
               y >= 160 && y <= 218) {
        finishBreastfeeding();
      }
    }

    return;
  }

  // ホーム画面
  auto touch = M5.Touch.getDetail();

  if (touch.wasPressed()) {
    int x = touch.x;
    int y = touch.y;

    // 授乳
    if (x >= buttons[0].x &&
        x <= buttons[0].x + buttons[0].w &&
        y >= buttons[0].y &&
        y <= buttons[0].y + buttons[0].h) {

      breastfeedingScreen = true;
      lastRefresh = millis();
      drawBreastfeedingScreen();
    }

    // 睡眠・起床
    else if (x >= buttons[1].x &&
            x <= buttons[1].x + buttons[1].w &&
            y >= buttons[1].y &&
            y <= buttons[1].y + buttons[1].h) {

      if (isSleeping()) {
        addRecord(WAKEUP);
      } else {
        addRecord(SLEEP);
      }
    }

    // おしっこ
    else if (x >= buttons[2].x &&
             x <= buttons[2].x + buttons[2].w &&
             y >= buttons[2].y &&
             y <= buttons[2].y + buttons[2].h) {

      addRecord(PEE);
    }

    // うんち
    else if (x >= buttons[3].x &&
             x <= buttons[3].x + buttons[3].w &&
             y >= buttons[3].y &&
             y <= buttons[3].y + buttons[3].h) {

      addRecord(POOP);
    }

    // 記録一覧
    else if (x >= 8 && x <= 148 &&
            y >= 210 && y <= 234) {
      drawHistoryScreen();
    }

    // 直近取消
    else if (x >= 156 && x <= 312 &&
            y >= 210 && y <= 234) {
      undoRecord();
    }
  }

  // 時計と前回の授乳からの経過時間を30秒ごとに更新
  if (!breastfeedingScreen && !historyScreen &&
      millis() - lastElapsedRefresh >= 30000) {
    lastElapsedRefresh = millis();

    // 時計を更新
    M5.Display.fillRect(
      0, 0, M5.Display.width(), 24, TFT_WHITE
    );

    drawCenteredText(
      formatTime(time(nullptr)),
      0, 0, M5.Display.width(), 24, TFT_BLACK
    );

    // 前回の授乳からの経過時間を更新
    M5.Display.fillRect(0, 180, 320, 22, TFT_WHITE);

    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setCursor(8, 184);

    M5.Display.print(
      "前回の授乳から " + lastBreastfeedingElapsed()
    );
  }
}
