# 開発ログ

## 2026-02-14: コードリファクタリング＋バグ修正

### リファクタリング内容

`src/main.cpp` を全体的に書き直し、165行→161行に簡略化。動作ロジックは変更なし。

- `turnOffLED()` を廃止し `setRGB(0, 0, 0)` に統一
- `setRGB()` ヘルパー関数を追加し `analogWrite` 3行の重複を排除
- `setColor()` の switch 内で直接 `setRGB()` を呼び出し、中間変数 `r, g, b` を削除
- エンコーダー方向判定を三項演算子で1行に簡略化
- 変数名を短縮（`colorHue`→`hue`、`lastEncMoveTime`→`lastEncMove`、`lastEncState`→`lastClk` 等）
- 関数定義順を整理し前方宣言 `void goToSleep()` を不要化
- `setup()` 内の冗長な `setLEDColor()` → `turnOffLED()` 連続呼び出しを `setRGB(0, 0, 0)` 1行に

### バグ修正

#### 1. `lastEncMove` の `volatile` 宣言漏れ
- **問題**: ISR(`readEncoder`)で書き込み、`loop()`で読み取る変数に `volatile` が付いていなかった。コンパイラ最適化で値がキャッシュされ、カラーモードのタイムアウト判定が誤動作する可能性があった。
- **修正**: `volatile unsigned long lastEncMove` に変更。

#### 2. `lastEncMove` の更新位置が不適切
- **問題**: `readEncoder()` ISR の末尾で毎回 `lastEncMove = millis()` を実行していた。CLK立ち上がりエッジ（実際の回転）を検出しなくても更新されるため、ノイズやバウンスでタイムアウトがリセットされ続け、カラーモードからスリープに戻れなくなる可能性があった。
- **修正**: 回転検出の `if` ブロック内に移動し、実際の回転時のみ更新するよう変更。

#### 3. `goToSleep()` で復帰後に `sleep_disable()` を呼んでいない
- **問題**: `sleep_mode()` から復帰しても SE(Sleep Enable) ビットがセットされたままだった。意図しない再スリープの原因となりうる。
- **修正**: `sleep_mode()` 直後に `sleep_disable()` を追加。これにより `loop()` 内の `sleep_disable()` は不要となったため削除。

#### 4. ISR共有変数のアトミック読み取り未対応
- **問題**: AVR は 8bit MCU のため、`int`(2バイト) や `unsigned long`(4バイト) の読み書きは複数命令にまたがる。`loop()` が読み取り中に ISR が値を更新すると、上位・下位バイトが不整合になる可能性があった。
- **修正**: `loop()` 内で `hue` と `lastEncMove` を読み取る際に `noInterrupts()` / `interrupts()` で保護し、ローカル変数にコピーしてから使用。

### 命名リファクタリング

変数名・関数名を役割が明確になるよう改名。

| 旧名 | 新名 | 理由 |
|-------|------|------|
| `ENC_STEP` | `HUE_STEP` | エンコーダーの物理ステップではなく色相の変化量 |
| `setColor()` | `applyHue()` | HSV色相→RGB変換して適用する関数だと明確に |
| `wakeUp()` | `onMotionWake()` | PIR起因のISRであることを明確に |
| `readEncoder()` | `onEncoderChange()` | 「読む」ではなく「変化に反応する」ISR |
| `lastClk` | `prevClkState` | 「前回のCLKの状態」を明確に |
| `lastEncTime` | `lastDebounceTime` | デバウンス用であることを明確に |
| `lastEncMove` | `lastRotationTime` | 回転検出時刻であることを明確に |

※ `goToSleep()` は既に明確なため変更なし。

### クールダウン機能の追加

- **問題**: LED消灯後、PIRセンサーの出力がまだHIGHのままスリープに入るため、即座に再検知して再点灯してしまう。
- **修正**: LED消灯後にクールダウン時間（`COOLDOWN_TIME` = 3秒）の `delay()` を追加。PIRセンサーの出力が安定するまで待機してからスリープに移行するようにした。値は `#define` で変更可能。

### PIRセンサーのウォームアップ待機を追加

- **問題**: PIRセンサーは電源投入直後に赤外線キャリブレーションを行うため、30秒〜1分間は出力が不安定。従来は `setup()` 終了後すぐにスリープに入っていたため、不安定な信号で誤検知・誤点灯する可能性があった。
- **修正**: `setup()` 内の `goToSleep()` 前に `PIR_WARMUP_TIME`（30秒）の待機ループを追加。待機中は LED_BUILTIN を500ms間隔で点滅させ、準備中であることを表示する。センサーによっては1分必要な場合があり、その際は値を `60000` に変更する。

### ユニットテスト環境の構築

HSV→RGB変換と色相正規化のロジックを純粋関数として分離し、PC上で実行可能なユニットテストを作成。

#### ロジックの分離

`applyHue()` 内の変換ロジックを `include/color_utils.h` に抽出。

- `hueToRGB(int hue)` — HSV色相をRGB構造体に変換する純粋関数
- `normalizeHue(int hue)` — 色相を0-359°に正規化する純粋関数

`main.cpp` の `applyHue()` は `hueToRGB()` を呼び出してから `setRGB()` に渡す形に変更。エンコーダーISR内の色相正規化も `normalizeHue()` を使用するよう変更。

#### テスト内容（`test/test_color_utils/test_color_utils.cpp`、全15ケース）

- **HSV→RGB境界値**: 0°(赤), 60°(黄), 120°(緑), 180°(シアン), 240°(青), 300°(マゼンタ)
- **HSV→RGB中間値**: 30°(オレンジ), 359°(赤に近いマゼンタ)
- **HUE_STEP刻み幅**: 15°刻みで全24ステップが0-255範囲内であることを確認
- **色相正規化**: 範囲内の値、360以上、負の値、大きな正負の値

#### platformio.ini の構造変更

`[env:native]`（PC上テスト用）追加に伴い、AVR共通設定を `[env]` から `[avr_common]` セクションに分離。各AVR環境は `extends = avr_common` で継承する構造に変更。

#### テスト実行方法

```bash
pio test -e native
```

※ PC上に gcc/g++ が必要（MinGW-w64 等）。MSYS2の MinGW-w64 gcc を使用して全14テストPASS確認済み。

## 2026-02-14: 元ファイルへのロジック復元＋割り込みバグ修正

### 元ファイルへのロジック復元

前回セッションのリファクタリング（命名変更、`color_utils.h`分離等）を試みたが、実機で複数の問題が発生したため、元の Arduino スケッチ（`sketch_mar21a_SENSOR_Light_UPDATE01.ino`）のロジックに戻した。前回セッションで追加した改善（`volatile`宣言、`sleep_disable()`、アトミック読み取り）は維持している。

現在の `main.cpp` は元の `.ino` ファイルのロジックをベースに、以下の2件のバグ修正を適用した状態。

### バグ修正

#### 1. PIR再トリガーループの防止（EIFRフラグクリア）

- **問題**: LED点灯中（20秒）にPIRが再検知すると、INT0が無効（`detachInterrupt`済み）でもEIFR（External Interrupt Flag Register）のINTF0ビットがセットされる。`goToSleep()`で`attachInterrupt(PIR, RISING)`した瞬間に保留フラグによりISRが即発火し、スリープ→復帰→スリープの再トリガーループが発生する。HC-SR501のHIGH期間を短く（3秒以内）設定していても、LED点灯中（t=6〜20秒）に人がセンサー前にいれば発生する。
- **修正**: `goToSleep()`内で`attachInterrupt`の前に`EIFR = (1 << INTF0);`を追加し、保留中のフラグをクリア。`|=`ではなく`=`を使用することで、INT1（エンコーダー）側のフラグを誤ってクリアすることを防いでいる（AVRの「write-1-to-clear」レジスタの仕様）。
- **参考**: ATmega328Pデータシート Section 13.2.3 EIFR、ArduinoCore-avr Issue #244

#### 2. スリープ復帰時のエンコーダー取りこぼし防止（デバウンスタイマーリセット）

- **問題**: PWR_DOWNスリープ中はTimer0が停止し`millis()`が凍結する。エンコーダー1クリックでCLKは HIGH→LOW→HIGH と変化しCHANGE割り込みが2回発火するが、回転検出はLOW→HIGH（2回目）でのみ行う。1回目のエッジで`lastEncTime`に凍結値が記録され、直後の2回目のエッジでも`millis()`がほぼ同じ凍結値を返すため、デバウンスフィルター（5ms以内の変化を無視）に弾かれ、最初の1クリックが無視される。
- **修正**: `goToSleep()`内でスリープ前に`lastEncTime = 0`にリセット。復帰時の1回目のエッジで`now - lastEncTime`が大きな値となりデバウンスを通過するため、`lastEncTime`に適切な値が記録され、2回目のエッジも正しく処理される。

### クロック周波数の変更

`platformio.ini`の`board_build.f_cpu`を`1000000L`（1 MHz）から`8000000L`（8 MHz）に変更。内蔵8 MHzオシレータの実際の動作周波数に合わせた。
