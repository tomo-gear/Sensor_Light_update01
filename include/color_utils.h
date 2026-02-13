#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

// RGB値を保持する構造体
struct RGB {
    int r;
    int g;
    int b;
};

// HSV色相(0-359°)をRGBに変換（整数演算のみ）
// 色相を60°ごとに6領域に分割し、各領域内で線形補間
inline RGB hueToRGB(int hue) {
    int region = hue / 60;
    int t = (hue % 60) * 255 / 60;
    switch (region) {
        case 0:  return {255, t, 0};
        case 1:  return {255 - t, 255, 0};
        case 2:  return {0, 255, t};
        case 3:  return {0, 255 - t, 255};
        case 4:  return {t, 0, 255};
        default: return {255, 0, 255 - t};
    }
}

// 色相を0-359°の範囲に正規化
inline int normalizeHue(int hue) {
    return (hue % 360 + 360) % 360;
}

#endif
