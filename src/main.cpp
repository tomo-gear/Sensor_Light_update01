#include <Arduino.h>
#include <avr/sleep.h>

// --- ピン定義 ---
#define PIR_PIN       2   // 人感センサー（INT0）
#define LIGHT_SENSOR  A0  // フォトレジスタ（アナログ入力）
#define RED_PIN       9   // RGB LED（赤・PWM）
#define GREEN_PIN     10  // RGB LED（緑・PWM）
#define BLUE_PIN      11  // RGB LED（青・PWM）
#define ENC_CLK       3   // ロータリーエンコーダー CLK（INT1）
#define ENC_DT        4   // ロータリーエンコーダー DT

// --- 動作パラメータ ---
#define THRESHOLD          50     // 明るさしきい値（これ未満で暗いと判定）
#define LED_ON_TIME        20000  // モーション検知時のLED点灯時間（20秒）
#define COOLDOWN_TIME      3000   // LED消灯後の再検知抑制時間（3秒）
#define PIR_WARMUP_TIME    30000  // PIRセンサーのウォームアップ時間（30秒）
#define COLOR_TIMEOUT      5000   // カラーモードのタイムアウト（5秒）
#define HUE_STEP           15     // 1クリックあたりの色相変化（15°、24クリックで一周）
#define ENC_DEBOUNCE       5      // エンコーダーのデバウンス時間（5ms）

// --- 状態変数（ISRと共有するためvolatile） ---
volatile bool motionDetected = false;       // PIR割り込みフラグ
volatile bool colorChanged = false;         // 色相変更フラグ
volatile bool colorMode = false;            // カラーモード中フラグ
volatile int hue = 0;                       // 現在の色相（0-359°）
volatile int prevClkState = 0;              // 前回のエンコーダーCLK状態
volatile unsigned long lastDebounceTime = 0;   // デバウンス用タイムスタンプ
volatile unsigned long lastRotationTime = 0;   // 最後の回転検出時刻（タイムアウト判定用）

// --- RGB LED制御 ---

// RGB各チャンネルにPWM値を出力
void setRGB(int r, int g, int b) {
    analogWrite(RED_PIN, r);
    analogWrite(GREEN_PIN, g);
    analogWrite(BLUE_PIN, b);
}

// HSV色相(0-359°)をRGBに変換してLEDに出力（整数演算のみ）
// 色相を60°ごとに6領域に分割し、各領域内で線形補間
void applyHue(int h) {
    int region = h / 60;             // 色相の領域（0-5）
    int t = (h % 60) * 255 / 60;    // 領域内の補間値（0-255）
    switch (region) {
        case 0:  setRGB(255, t, 0);         break;  // 赤→黄
        case 1:  setRGB(255 - t, 255, 0);   break;  // 黄→緑
        case 2:  setRGB(0, 255, t);         break;  // 緑→シアン
        case 3:  setRGB(0, 255 - t, 255);   break;  // シアン→青
        case 4:  setRGB(t, 0, 255);         break;  // 青→マゼンタ
        default: setRGB(255, 0, 255 - t);   break;  // マゼンタ→赤
    }
}

// --- 割り込みハンドラ ---

// PIRセンサー割り込み（RISING）：動き検知でスリープから復帰
void onMotionWake() { motionDetected = true; }

// エンコーダー割り込み（CHANGE）：回転検出＋チャタリング対策
void onEncoderChange() {
    unsigned long now = millis();
    if (now - lastDebounceTime < ENC_DEBOUNCE) return;  // デバウンス期間内は無視
    lastDebounceTime = now;

    int clk = digitalRead(ENC_CLK);
    // CLKの立ち上がりエッジ（LOW→HIGH）で回転方向を判定
    if (prevClkState == LOW && clk == HIGH) {
        hue += (digitalRead(ENC_DT) == LOW) ? HUE_STEP : -HUE_STEP;  // DT=LOW:時計回り
        hue = (hue + 360) % 360;  // 0-359°の範囲に正規化
        colorChanged = true;
        colorMode = true;
        lastRotationTime = millis();  // タイムアウト計測用（回転検出時のみ更新）
    }
    prevClkState = clk;
    // 実際に回転を検出した場合のみタイムアウト計測を更新する。
    // ここに置くとノイズやバウンスでもリセットされ、
    // カラーモードからスリープに戻れなくなる可能性がある。
}

// --- スリープ制御 ---

// PWR_DOWNモードに移行（PIRまたはエンコーダー割り込みで復帰）
void goToSleep() {
    sleep_enable();
    attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotionWake, RISING);
    digitalWrite(LED_BUILTIN, LOW);   // 状態表示LED消灯
    sleep_mode();                      // ここでスリープ（復帰まで停止）
    // --- 以下、割り込みで復帰後に実行 ---
    sleep_disable();                   // SE(Sleep Enable)ビットをクリアし意図しない再スリープを防止
    detachInterrupt(digitalPinToInterrupt(PIR_PIN));
    digitalWrite(LED_BUILTIN, HIGH);  // 復帰表示
}

// --- 初期化 ---
void setup() {
    // ピン設定
    pinMode(PIR_PIN, INPUT);
    pinMode(LIGHT_SENSOR, INPUT);
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);

    // エンコーダー割り込み登録（CLKのCHANGEで発火）
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderChange, CHANGE);
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    setRGB(0, 0, 0);  // LED消灯

    // PIRセンサーのウォームアップ待機
    // 電源投入直後は赤外線キャリブレーション中のため出力が不安定。
    // 安定するまで待機し、LED_BUILTINの点滅で準備中を表示。
    for (unsigned long start = millis(); millis() - start < PIR_WARMUP_TIME; ) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        delay(500);
    }
    digitalWrite(LED_BUILTIN, LOW);  // 点滅終了

    goToSleep();       // 初回スリープ
}

// --- メインループ ---
void loop() {
    // エンコーダー回転による色更新
    if (colorChanged) {
        // AVRは8bitなのでint(2バイト)の読み取り中にISRが割り込むと
        // 上位・下位バイトが不整合になる可能性がある。割り込み禁止で保護。
        noInterrupts();
        int h = hue;
        interrupts();
        applyHue(h);
        colorChanged = false;
    }
    delay(1);

    // カラーモードのタイムアウト：操作がなければスリープに戻る
    // unsigned long(4バイト)は読み取りに4命令かかるため、
    // ISRによる途中更新でバイト不整合が起きないよう割り込み禁止で保護。
    noInterrupts();
    unsigned long rotTime = lastRotationTime;
    interrupts();
    if (colorMode && millis() - rotTime > COLOR_TIMEOUT) {
        setRGB(0, 0, 0);     // LED消灯
        colorMode = false;
        goToSleep();
    }
    delay(1);

    // モーション検知処理（カラーモード中は無視）
    if (!colorMode && motionDetected) {
        motionDetected = false;
        detachInterrupt(digitalPinToInterrupt(ENC_CLK));  // 点灯中のエンコーダー割り込みを無効化

        ADCSRA |= (1 << ADEN);   // ADC有効化（スリープ中は省電力のため無効）
        delay(100);               // ADC安定待ち

        if (analogRead(LIGHT_SENSOR) < THRESHOLD) {
            // 暗い場合：選択色でLED点灯
            applyHue(hue);
            delay(LED_ON_TIME);
        } else {
            delay(10);
        }
        setRGB(0, 0, 0);          // LED消灯
        delay(COOLDOWN_TIME);      // PIRセンサーが安定するまで待機し、即座の再検知を防止
        ADCSRA &= ~(1 << ADEN);   // ADC無効化（省電力）

        attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderChange, CHANGE);  // エンコーダー割り込み再開
        goToSleep();
    }
}
