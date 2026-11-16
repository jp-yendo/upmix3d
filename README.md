# upmix3d

VLC 3.0系x64向けのWindows空間オーディオ出力モジュール。モノラル・ステレオ・多チャンネル音声を、最大7.1.4と後方センターへ割り当てる。マトリクス／STFT、ハイト生成、ダウンミックス、ビットストリームパススルーに対応する。

[要件定義](Documents/requirements.md)、[技術上の根拠](Documents/requirements-review.md)、[実装と検証の対応](Documents/implementation-status.md)、[内部仕様](Documents/システム仕様.md)を参照する。

## ビルド

Visual Studio 2026、v145、C++20、Windows SDKとCMakeを使用する。DLLはx64のみ。Python、pip、仮想環境は使用しない。

```powershell
.\scripts\fetch-vlc.ps1
.\scripts\build.ps1 -Configuration Release
```

公式VLC 3.0.24ポータブル版のSDKを使用する。既存SDKを使う場合は`-VlcRoot C:\path\to\vlc`を渡す。Visual Studioで`upmix3d.vcxproj`を開く場合は、MSBuildの`VlcRoot`または環境変数`VLC_ROOT`でSDKを指定する。

| ビルド | 出力DLL |
|---|---|
| CMake / build.ps1 | `build/Release/libupmix3d_plugin.dll` |
| Visual Studioプロジェクト | `build/vs/Release/libupmix3d_plugin.dll` |

MSVCランタイムはDLLに静的リンクする。VLCの`libvlccore.dll`は動的に使用する。

## 検証

```powershell
# コアテストと性能測定
.\scripts\verify.ps1

# ポータブルVLCでの無音デバイステスト
.\scripts\verify.ps1 -DeviceTests

# 2時間分のDSPを高速に処理する耐久テスト
.\scripts\verify.ps1 -LongDsp
```

デバイステストには、空間サウンド・排他PCM・圧縮音声に対応する出力機器を使用する。検証の設定とログは`build/integration/`へ保存する。Windowsの既定デバイスやインストール済みVLCのファイルは変更しない。

無音の結合テストで一時停止・再開・シーク・音量・ミュート・排出、設定保存・復元、デバイス選択を確認する。付属の無音素材はAC-3、DTS、E-AC-3、TrueHD。DTS-HD経路はDTSコアをHDMIの高ビットレート形式へ格納して検証する。

実スピーカーの位置は、明示的な再生指定で確認する。

```powershell
.\build\Release\upmix3d_speaker_check.exe --list
.\build\Release\upmix3d_speaker_check.exe --play
.\build\Release\upmix3d_speaker_check.exe --play --back-center
```

位置を表示して各チャンネルを約1秒ずつ再生する。通常チャンネルはピンクノイズ、LFEは60Hz。基準ゲインは-30dB。`--device`で列挙したID、`--layout`で構成を指定できる。

## VLCから使う

VLCを終了し、DLLを対象VLCの`plugins/audio_output/`へ配置して再起動する。出力モジュールに`upmix3d`を選択する。

```powershell
vlc.exe --aout=upmix3d --upmix3d-layout=7.1.4 movie.mkv
```

ハイトを使う場合はWindowsの空間サウンドを有効にする。出力形式とスピーカーへのレンダリングはWindowsと機器に従う。設定はVLCの「すべて」から変更する。DSP・構成はストリーム開始時、音量・ミュート・デバイス選択は再生中にも反映する。

| 設定 | 動作 |
|---|---|
| `upmix3d-layout` | 3.1 / 4.0 / 5.1 / 7.1 / 2.1.2 / 3.1.2 / 5.1.2 / 5.1.4 / 7.1.2 / 7.1.4。既定7.1.4 |
| `upmix3d-downmix` | 既定OFF。入力位置を保持する。機器の制限時はログを出して混ぜ込む |
| `upmix3d-method` | `matrix` / `frequency`。既定STFT 2048/512 |
| `upmix3d-preset` | `movie` / `music` / `wide` / `custom`。個別値の変更でカスタム化 |
| `upmix3d-preset-5-1`等 | 1.0〜8.1の各入力構成へのプリセット割り当て。`inherit`は全体設定 |
| `upmix3d-route` | ハイトなしの経路。`spatial` / `shared` / `exclusive` |
| `upmix3d-passthrough` | `0`無効 / `1`AC-3・DTSコア / `2`全形式。既定2。DSP・音量をバイパス |
| `upmix3d-top-middle` | 2ハイトを動的オブジェクトで真上へ配置 |
| `upmix3d-back-center` | 静的な後方センターがない場合の`dynamic` / `fold` |
| `upmix3d-latency` | `low` / `standard` / `quality`。低遅延はマトリクスと短いバッファ |
| `upmix3d-protection` | `fixed` / `normalize` / `limiter`。既定リミッター |
| `upmix3d-true-peak` | 既定ON。4倍補間FIRの保守的な上限、23サンプルの先読み |
| `upmix3d-threshold-db` | 既定-1。トゥルーピーク有効時はdBTP、無効時はdBFS |
| `upmix3d-transition-ms` | 再生境界の平滑化。既定5ms |
| `upmix3d-spatial-offset-ms`等 | 空間・共有・排他それぞれ-500〜+500msの補正を保存 |

正規化は瞬時ピークを対象とし、番組ラウドネスの正規化ではない。入出力が一致しゲイン0dB、リミッター非動作のPCMでは、報告した遅延を除いてサンプル値を保持する。

Atmosの受信表示、実スピーカーの定位、受信機を含む映像同期・聴感は実機で確認する。無音の自動テストと2時間分の高速DSP処理は、2時間の動画を実時間で再生して同期を確認したことを意味しない。
