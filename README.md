# M5Paper Home Sensors Viewer

M5Paper V1.1 で SwitchBot の温湿度計・CO2 センサーのデータを BLE 経由で受信し、e-ink ディスプレイに表示するファームウェアです。

## 対応デバイス

| デバイス | 表示データ |
|---------|-----------|
| SwitchBot Meter Pro (CO2) | 温度, 湿度, CO2 濃度 |
| SwitchBot 防水温湿度計 (Indoor/Outdoor) | 温度, 湿度 |

## 動作フロー

1. 初回・12 回ごとの起動で WiFi 接続 → NTP の同期完了通知を待つ → RTC 設定 → WiFi 切断（失敗した場合は次の起動で再試行）
2. BLE スキャン（15 秒間）でセンサーデータと受信タイミングを記録
3. スキャン後の RTC から現在日時と各センサーの最終受信時刻を求め、e-ink ディスプレイに描画
4. ディープスリープ（5 分）→ 1 に戻る

## セットアップ

### 必要なもの

- M5Paper V1.1
- PlatformIO (VSCode)
- SwitchBot Meter Pro (CO2)
- SwitchBot 防水温湿度計

### 1. BLE MAC アドレスの確認

SwitchBot アプリで各デバイスの BLE MAC アドレスを確認します:

**SwitchBot アプリ → デバイス選択 → 設定（歯車アイコン）→ デバイス情報 → BLE MAC**

### 2. 設定ファイルの編集

`src/secrets.h.example` をコピーして `src/secrets.h` を作成し、自分の値を記入してください:

```bash
cp src/secrets.h.example src/secrets.h
```

```cpp
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

#define CO2_SENSOR_MAC "xx:xx:xx:xx:xx:xx"      // Meter Pro (CO2) のMAC
#define OUTDOOR_METER_MAC "xx:xx:xx:xx:xx:xx"    // 防水温湿度計のMAC
```

> `src/secrets.h` は `.gitignore` に含まれているため、Git にコミットされません。

### 3. 日本語フォントの準備

日本語表示には TTF フォントファイルが必要です。

1. [GenSenRounded](https://github.com/nicofont/GenSenRounded-Nerd/releases) から `GenSenRounded-R.ttf` をダウンロード
2. プロジェクトルートに `data/` フォルダを作成
3. `data/GenSenRounded-R.ttf` に配置
4. PlatformIO で SPIFFS にアップロード:

```bash
pio run --target uploadfs
```

> **注意**: フォントファイルがない場合はデフォルトフォント（ASCII のみ）で描画されます。

### 4. ビルド & 書き込み

```bash
pio run --target upload
```

### 5. シリアルモニター（デバッグ用）

```bash
pio device monitor
```

## カスタマイズ

`src/config.h` で以下の設定を変更できます:

| 設定 | デフォルト値 | 説明 |
|------|-------------|------|
| `NTP_SERVER` | `"ntp.nict.jp"` | NTP サーバー |
| `GMT_OFFSET_SEC` | `32400` (9h) | タイムゾーンオフセット (JST) |
| `NTP_SYNC_EVERY_N_BOOTS` | `12` | 定期同期の起動間隔（スキャン・描画時間を含むため約 1 時間以上） |
| `NTP_SYNC_TIMEOUT_MS` | `10000` | 今回の NTP 同期完了を待つ最大時間（ミリ秒） |
| `BLE_SCAN_DURATION_SEC` | `15` | BLE スキャン時間（秒） |
| `SLEEP_DURATION_SEC` | `300` | ディープスリープ時間（秒） |
| `CO2_EXCELLENT` | `600` | CO2 表情アイコン閾値 |
| `CO2_GOOD` | `1000` | CO2 表情アイコン閾値 |
| `CO2_FAIR` | `1500` | CO2 表情アイコン閾値 |

## トラブルシューティング

### センサーが検出されない

- SwitchBot アプリで BLE MAC アドレスが正しいか再確認
- MAC アドレスは小文字で入力（例: `a1:b2:c3:d4:e5:f6`）
- M5Paper とセンサーの距離が近いか確認（BLE の到達範囲は約 10m）
- `BLE_SCAN_DURATION_SEC` を長く（60 秒等）してみる

### 日本語が表示されない

- `data/GenSenRounded-R.ttf` が正しく配置されているか確認
- `pio run --target uploadfs` で SPIFFS にアップロード済みか確認

### 時刻がずれる

- WiFi の SSID / パスワードが正しいか確認
- NTP サーバーに到達できるネットワーク環境か確認
- シリアルログの `[NTP] Fresh response received` が今回の同期完了を示します。続く `[NTP] RTC updated` と `[Info]` の日時を確認してください
- `[NTP] No fresh response` または WiFi 接続失敗の場合は RTC を維持し、次のスリープ復帰時に再試行します
- 「最終更新」はセンサーの最終受信時刻です。5 分のスリープ中や受信できなかった場合は以前の時刻が表示されます

## ライセンス

MIT
