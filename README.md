# M5Paper Home Sensors Viewer

M5Paper V1.1 で SwitchBot の温湿度計・CO2 センサーのデータを BLE 経由で受信し、指定地点の天気予報とともに e-ink ディスプレイに表示するファームウェアです。

## 対応デバイス

| デバイス | 表示データ |
|---------|-----------|
| SwitchBot Meter Pro (CO2) | 温度, 湿度, CO2 濃度 |
| SwitchBot 防水温湿度計 (Indoor/Outdoor) | 温度, 湿度 |

## 動作フロー

1. NTP同期または天気取得が必要な起動で WiFi 接続。NTPは初回・12回ごと、天気は初回・前回成功から1時間経過後に取得（失敗時は次の起動で再試行）
   - NTPは今回の同期完了通知を待ってRTCを設定。天気取得と接続を共用し、終了後にWiFiを切断
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

// 天気の表示地点 (WGS84)。東京の例なので、自分の地点に変更してください。
#define WEATHER_LATITUDE 35.68
#define WEATHER_LONGITUDE 139.76
```

> `src/secrets.h` は `.gitignore` に含まれているため、Git にコミットされません。

天気の緯度・経度を両方設定すると、Open-Meteoにその座標を送信して取得します。未設定・不正な座標の場合は通信せず、天気を `--` 表示にします。APIキーは不要です（個人の非商用利用）。

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
| `CO2_GOOD` | `1000` | CO2 表情アイコン閾値 |
| `CO2_WARNING` | `1500` | CO2 表情アイコン閾値 |

## 天気表示

- 天気アイコン・最高最低気温・大きな降水確率は、日本時間の00:00〜20:59に「今日」、21:00〜23:59に「明日」を表示します。切替はその時刻以降の画面更新で反映し、ヘッダーは現在の日付を保ちます。
- 小さい2つの降水確率は、現在時刻を正時に切り下げた時刻から4時間ずつの範囲です。21時は `21-01時` / `01-05時`、23時30分は `23-03時` / `03-07時`。日別の切替に影響されません。「翌」は付けず、午前0時で終わる枠は `20-24時` と表記します。
- 気温は整数に四捨五入します。風速・地点名・時間別の天気・服装や傘の推奨は表示しません。
- 天気欄の右下の「最終更新」は、両APIの取得・解析に成功した端末側の時刻です。予報モデルの更新時刻ではありません。センサー欄の時刻は従来どおり最終受信時刻です。
- 2日分の予報をRTCメモリに保存し、約5分ごとの起動では保存済みデータから表示を再計算します。電源断ではキャッシュは失われます。取得失敗時は成功時刻を変更せず、6時間までは保存データを利用します。
- 6時間を超えたデータ、対象日がない項目、欠損を含む4時間枠は `--` になります。未知の天気コードも `--` とし、未取得の更新時刻は `--:--` にします。

### 取得元・計算方法

[Open-Meteo Weather Forecast API](https://open-meteo.com/en/docs) の `/v1/forecast` に、共通で `timezone=Asia/Tokyo` と `forecast_days=2` を指定します。

| 用途 | モデル | 取得項目 |
|------|--------|----------|
| 日別の天気・最高最低気温 | `jma_seamless` | `daily=weather_code,temperature_2m_max,temperature_2m_min` |
| 日別・4時間枠の降水確率 | `best_match` | `daily=precipitation_probability_max`、`hourly=precipitation_probability` |

[JMA Seamless](https://open-meteo.com/en/docs/jma-api) は [気象庁（JMA）](https://www.jma.go.jp/) のMSM/GSMを接続した予報です。天気コードはOpen-Meteoがモデル値から算出したその日の最も厳しい天気であり、気象庁が発表する天気概況そのものではありません。JMAモデルだけでは降水確率を取得できないため、確率にはBest Matchのアンサンブル由来の値を使います。

日別の確率はAPIの日最大値、各4時間枠はその範囲を構成する1時間値4個の最大値です。「期間内に一度でも降る確率」を計算したものではありません。時間別値は表示時刻の直前1時間に0.1mmを超える降水がある確率なので、開始時刻より後・終了時刻以下の4時点を使用します。日付を含む時刻で照合し、日またぎでも重複させません。これらの説明は画面上には注記しません。

HTTPSはESP-IDFに同梱されたルート証明書群で検証します。RTCが未設定の場合はNTP同期後に取得し、証明書検証を無効化するフォールバックは行いません。

### 出典

天気欄の左下に **Open-Meteo.com**、右下に最終更新時刻を表示します。Weather data by [Open-Meteo.com](https://open-meteo.com/)、天気・気温の原データは [JMA](https://www.jma.go.jp/) です。データは [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) に基づき利用します（[Open-Meteoのライセンス](https://open-meteo.com/en/licence)、[気象庁の利用規約](https://www.jma.go.jp/jma/kishou/info/coment.html)）。このアプリは気温の丸め、天気コードのアイコン化、時間別降水確率の4時間ごとの最大値集計を行っています。

## 開発時の検証

```bash
pio test -e native
pio run -e m5paper
```

ネイティブテストは通信・実機なしで、21時と0時の切替、月末・年末、降水確率の時間範囲、JSONの欠損・異常、キャッシュの有効期限を検証します。M5Paperビルドには `src/secrets.h` が必要です。動作確認だけなら設定例をコピーしたダミー値でビルドできます。書き込みには `--target upload` を明示します。

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

### 天気が表示されない

- `WEATHER_LATITUDE` と `WEATHER_LONGITUDE` の両方を設定してください
- WiFi・NTPの成功を確認し、シリアルログの `[Weather] HTTP` と `[Weather] Forecast updated` を確認してください
- 通信・解析に失敗するとキャッシュを維持して次回起動時に再試行します。6時間以上取得できない場合やAPIが値を返さない場合は `--` を表示します
- 証明書エラーの場合はRTC日時とPlatformIOのESP32フレームワークに含まれるルート証明書を確認してください

## ライセンス

MIT
