#include <unity.h>
#include "color_utils.h"

// Unity が要求するセットアップ・テアダウン関数
void setUp(void) {}
void tearDown(void) {}

// --- hueToRGB: 各領域の境界値テスト ---

void test_hue_0_is_red(void) {
    RGB c = hueToRGB(0);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(0, c.g);
    TEST_ASSERT_EQUAL(0, c.b);
}

void test_hue_60_is_yellow(void) {
    RGB c = hueToRGB(60);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(255, c.g);
    TEST_ASSERT_EQUAL(0, c.b);
}

void test_hue_120_is_green(void) {
    RGB c = hueToRGB(120);
    TEST_ASSERT_EQUAL(0, c.r);
    TEST_ASSERT_EQUAL(255, c.g);
    TEST_ASSERT_EQUAL(0, c.b);
}

void test_hue_180_is_cyan(void) {
    RGB c = hueToRGB(180);
    TEST_ASSERT_EQUAL(0, c.r);
    TEST_ASSERT_EQUAL(255, c.g);
    TEST_ASSERT_EQUAL(255, c.b);
}

void test_hue_240_is_blue(void) {
    RGB c = hueToRGB(240);
    TEST_ASSERT_EQUAL(0, c.r);
    TEST_ASSERT_EQUAL(0, c.g);
    TEST_ASSERT_EQUAL(255, c.b);
}

void test_hue_300_is_magenta(void) {
    RGB c = hueToRGB(300);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(0, c.g);
    TEST_ASSERT_EQUAL(255, c.b);
}

// --- hueToRGB: 領域内の中間値テスト ---

void test_hue_30_is_orange(void) {
    // 30° = 領域0の中間 → R=255, G=127, B=0
    RGB c = hueToRGB(30);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(127, c.g);  // (30 % 60) * 255 / 60 = 127
    TEST_ASSERT_EQUAL(0, c.b);
}

void test_hue_359_wraps_near_red(void) {
    // 359° = 領域5の末端、ほぼ赤に戻る
    RGB c = hueToRGB(359);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(0, c.g);
    // (359 % 60) * 255 / 60 = 59 * 255 / 60 = 250
    // 255 - 250 = 5
    TEST_ASSERT_EQUAL(5, c.b);
}

// --- hueToRGB: HUE_STEPの刻み幅テスト ---

void test_hue_step_15_increments(void) {
    // 15°刻みで24ステップ、各値が0-255の範囲内であることを確認
    for (int h = 0; h < 360; h += 15) {
        RGB c = hueToRGB(h);
        TEST_ASSERT_TRUE(c.r >= 0 && c.r <= 255);
        TEST_ASSERT_TRUE(c.g >= 0 && c.g <= 255);
        TEST_ASSERT_TRUE(c.b >= 0 && c.b <= 255);
    }
}

// --- normalizeHue テスト ---

void test_normalize_positive_in_range(void) {
    TEST_ASSERT_EQUAL(0, normalizeHue(0));
    TEST_ASSERT_EQUAL(180, normalizeHue(180));
    TEST_ASSERT_EQUAL(359, normalizeHue(359));
}

void test_normalize_wraps_360(void) {
    TEST_ASSERT_EQUAL(0, normalizeHue(360));
    TEST_ASSERT_EQUAL(15, normalizeHue(375));
}

void test_normalize_negative(void) {
    TEST_ASSERT_EQUAL(345, normalizeHue(-15));
    TEST_ASSERT_EQUAL(330, normalizeHue(-30));
}

void test_normalize_large_negative(void) {
    TEST_ASSERT_EQUAL(345, normalizeHue(-375));  // -375 + 360*2 = 345
}

void test_normalize_large_positive(void) {
    TEST_ASSERT_EQUAL(15, normalizeHue(735));  // 735 % 360 = 15
}

// --- エントリーポイント ---

int main(void) {
    UNITY_BEGIN();

    // hueToRGB 境界値
    RUN_TEST(test_hue_0_is_red);
    RUN_TEST(test_hue_60_is_yellow);
    RUN_TEST(test_hue_120_is_green);
    RUN_TEST(test_hue_180_is_cyan);
    RUN_TEST(test_hue_240_is_blue);
    RUN_TEST(test_hue_300_is_magenta);

    // hueToRGB 中間値
    RUN_TEST(test_hue_30_is_orange);
    RUN_TEST(test_hue_359_wraps_near_red);
    RUN_TEST(test_hue_step_15_increments);

    // normalizeHue
    RUN_TEST(test_normalize_positive_in_range);
    RUN_TEST(test_normalize_wraps_360);
    RUN_TEST(test_normalize_negative);
    RUN_TEST(test_normalize_large_negative);
    RUN_TEST(test_normalize_large_positive);

    return UNITY_END();
}
