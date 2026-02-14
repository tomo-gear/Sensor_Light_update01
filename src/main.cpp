#include <Arduino.h>
#include <avr/sleep.h>

// 【注意】PIR再トリガー防止について
//
// AVRの外部割り込みにはEIFR（External Interrupt Flag Register）があり、
// 割り込み条件（RISINGエッジ等）が発生するとINTF0/INTF1ビットがセットされる。
// このフラグは割り込みが無効（detachInterrupt済み）でもセットされる。
//
// 問題：LED点灯中（20秒）はINT0（PIR）を無効化しているが、その間にPIRが
// 再検知してRISINGエッジが発生すると、EIFRのINTF0ビットに保留される。
// goToSleep()でattachInterrupt(PIR, RISING)した瞬間、保留フラグにより
// ISRが即発火し、スリープに入れず再トリガーループが発生する。
//
// 対策：goToSleep()内でattachInterruptの前に EIFR = (1 << INTF0) で
// 保留フラグをクリアしている。（EIFRは該当ビットに1を書くとクリアされる）
//
// 【注意】スリープ復帰時のエンコーダー取りこぼしについて
//
// PWR_DOWNスリープ中はTimer0が停止するため、millis()が凍結する。
// エンコーダー1クリックでCLKピンは HIGH→LOW→HIGH と変化し、CHANGE割り込みが2回発火する。
// 回転検出はLOW→HIGH（2回目）でのみ行うが、デバウンス処理（5ms以内の変化を無視）が
// millis()凍結の影響で2回目のエッジを誤って弾いてしまい、最初の1クリックが無視される。
//
// 対策：goToSleep()内でスリープ前に lastEncTime = 0 にリセットし、
// 復帰時のデバウンス判定が正しく通過するようにしている。

#define PIR_PIN 2        // 人感センサー
#define LIGHT_SENSOR A0  // フォトレジスタ
#define RED_PIN 9        // RGB LED（赤）
#define GREEN_PIN 10     // RGB LED（緑）
#define BLUE_PIN 11      // RGB LED（青）

#define ENC_CLK 3        // ロータリーエンコーダー CLK（INT1）
#define ENC_DT 4         // ロータリーエンコーダー DT

#define THRESHOLD 50    // 明るさしきい値
#define LED_ON_TIME 20000 // 20秒点灯
#define COLOR_MODE_TIMEOUT 5000  // 5秒で通常モードに戻る
#define ENC_STEP 15      // 24クリック = 360° を均等割り（15°/クリック）
#define ENC_DEBOUNCE_TIME 5  // 5ms 以内の変化を無視

volatile bool motionDetected = false;
volatile int colorHue = 0;  // 色相（0-359°）
volatile bool colorChanged = false;
volatile bool colorMode = false; // 色設定モード
volatile unsigned long lastEncMoveTime = 0;  // 最後のエンコーダー操作時間
volatile int lastEncState = 0;
volatile unsigned long lastEncTime = 0; // 最後にエンコーダーが変化した時間

// 割り込み処理（PIR）
void wakeUp() {
    motionDetected = true;
}

// 割り込み処理（エンコーダー回転）【チャタリング対策付き】
void readEncoder() {
    unsigned long now = millis();
    if (now - lastEncTime < ENC_DEBOUNCE_TIME) return; // 5ms 以内の変化は無視
    lastEncTime = now;

    int currentState = digitalRead(ENC_CLK);
    int dtState = digitalRead(ENC_DT);

    // A 相（CLK）の立ち上がりエッジを検出（LOW → HIGH になったとき）
    if (lastEncState == LOW && currentState == HIGH) {
        if (dtState == LOW) {
            colorHue += ENC_STEP;  // 時計回り
        } else {
            colorHue -= ENC_STEP;  // 反時計回り
        }
        colorHue = (colorHue + 360) % 360; // 0-359 の範囲にする
        colorChanged = true;
        // 色設定モードに入る
        colorMode = true;
        
        
    }
    lastEncState = currentState;
    lastEncMoveTime = millis(); // 最後の操作時間を記録
    
}

// HUE (0-359) を RGB (0-255) に変換（整数演算のみ）
void setLEDColor(int hue) {
    int r = 0, g = 0, b = 0;
    int region = hue / 60;  // 0〜5 のどの区間か
    int remainder = (hue % 60) * 255 / 60;  // 0〜255 の割合

    switch (region) {
        case 0: r = 255; g = remainder; b = 0; break;
        case 1: r = 255 - remainder; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = remainder; break;
        case 3: r = 0; g = 255 - remainder; b = 255; break;
        case 4: r = remainder; g = 0; b = 255; break;
        case 5: r = 255; g = 0; b = 255 - remainder; break;
    }

    analogWrite(RED_PIN, r);
    analogWrite(GREEN_PIN, g);
    analogWrite(BLUE_PIN, b);
}

// LED消灯
void turnOffLED() {
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);
}

void goToSleep() {
    lastEncTime = 0;  // スリープ中はmillis()が凍結するため、復帰時のデバウンス誤判定を防止
    sleep_enable();
    EIFR = (1 << INTF0);  // LED点灯中に蓄積されたPIR割り込みフラグをクリアし、再トリガー防止
    attachInterrupt(digitalPinToInterrupt(PIR_PIN), wakeUp, RISING);
    digitalWrite(LED_BUILTIN, LOW);
    sleep_mode();
    sleep_disable();
    detachInterrupt(digitalPinToInterrupt(PIR_PIN));
    digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {

    pinMode(PIR_PIN, INPUT);
    pinMode(LIGHT_SENSOR, INPUT);
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);

    
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), readEncoder, CHANGE);

    setLEDColor(colorHue);

    turnOffLED();

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    

    goToSleep();
}

void loop() {
    if (colorChanged) {
        //sleep_disable();
        //detachInterrupt(digitalPinToInterrupt(PIR_PIN));
        noInterrupts();
        int h = colorHue;
        interrupts();
        setLEDColor(h);

        
        colorChanged = false;
    }
    delay(1);

    // 色設定モードから通常モードに戻る（最後のエンコーダー操作から何秒後）
    noInterrupts();
    unsigned long rotTime = lastEncMoveTime;
    interrupts();
    if (colorMode && (millis() - rotTime > COLOR_MODE_TIMEOUT)) {
        turnOffLED();
        //EEPROM.put(0, colorHue);
        colorMode = false;
        goToSleep();
    }
    
    delay(1);
    if (!colorMode && motionDetected) {
        motionDetected = false;
        detachInterrupt(digitalPinToInterrupt(ENC_CLK));

        // ADC有効化
        ADCSRA |= (1<<ADEN);
        delay(10);

        int lightLevel = analogRead(LIGHT_SENSOR);
        if (lightLevel < THRESHOLD) {
            setLEDColor(colorHue);
            delay(LED_ON_TIME);
            turnOffLED();
        } else {
          delay(1);
        }

        // ADC無効化
        ADCSRA &= ~(1<<ADEN);

        attachInterrupt(digitalPinToInterrupt(ENC_CLK), readEncoder, CHANGE);
        goToSleep();
    }
}