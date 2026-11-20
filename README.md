# M5Stack Baby Log

M5Stack Core2を使った、赤ちゃんの育児記録端末です。

授乳・睡眠・おしっこ・うんちを、画面のボタンをタッチするだけで簡単に記録できます。

## Features

- 授乳の開始・左右の授乳時間を記録
- 睡眠・起床を記録
- おしっこ・うんちを記録
- 前回の授乳からの経過時間を表示
- 記録履歴の一覧表示
- 直前の記録を取り消し
- 電源を切っても記録を保持
- 最大100件の記録を保存（古い記録から自動削除）

## Hardware

- M5Stack Core2

## Development environment

- Arduino IDE
- C++
- M5Unified

## Setup

### 1. Required libraries

Arduino IDEのライブラリマネージャーから、以下をインストールしてください。

- M5Unified

### 2. Wi-Fi settings

`secrets.example.h` をコピーして、ファイル名を `secrets.h` に変更してください。

`secrets.h` に使用するWi-FiのSSIDとパスワードを設定します。

```cpp
#pragma once

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASSWORD = "your-password";
```

### 3. Upload

1. Arduino IDEで `BabyLog_Public.ino` を開きます。
2. 使用するM5Stack Core2を選択します。
3. M5Stack Core2にプログラムを書き込んでください。

## Notes

- Wi-Fi接続は時刻取得（NTP）のために使用します。
- 記録データはM5Stack Core2内のPreferencesに保存されます。
- 本プログラムは、記録をM5Stack単体で管理する構成です。
- スマートフォンアプリへの自動転送機能はありません。

## License

This project is licensed under the MIT License. See the LICENSE file for details.
