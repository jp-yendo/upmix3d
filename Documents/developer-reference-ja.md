# 開発者リファレンス

[English](developer-reference.md) | 日本語

## ビルド

Visual Studio 2026、v145、C++20、Windows SDKとCMakeを使用する。DLLはx64のみ。Python、pip、仮想環境は使用しない。

### VLC SDKの準備

VLC SDKはGit管理対象外の`external/`へ取得する。リポジトリのルートでPowerShellを開き、次を実行する。

```powershell
.\scripts\fetch-vlc.ps1
.\scripts\build.ps1 -Configuration Release
```

`fetch-vlc.ps1`は公式VLC 3.0.24 x64アーカイブをダウンロードし、SHA256を照合して`external/`へ展開する。インターネット接続、`curl.exe`、7z形式を展開できる`tar.exe`を使用する。準備後の主な配置は次のとおり。

```text
external/
  vlc-3.0.24/
    libvlc.dll
    libvlccore.dll
    plugins/
    sdk/
      include/vlc/plugins/vlc_aout.h
      lib/libvlccore.lib
```

インターネットに接続できない環境では、取得済みのVLCフォルダーを`external/vlc-3.0.24/`へ配置する。`tar.exe`が7z形式に対応しない場合は、スクリプトが取得・照合した`external/vlc-3.0.24-win64.7z`を7-Zip等で`external/`へ展開し、上記の配置にする。ビルドには`sdk/`、付属の結合テストにはVLC本体と`plugins/`も必要になる。

### SDKの指定とビルド出力

公式VLC 3.0.24ポータブル版のSDKを使用する。既存SDKを使う場合は`-VlcRoot C:\path\to\vlc`を渡す。Visual Studioで`upmix3d.vcxproj`を開く場合は、MSBuildの`VlcRoot`または環境変数`VLC_ROOT`でSDKを指定する。

| ビルド | 出力DLL |
| --- | --- |
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

無音の結合テストで一時停止・再開・シーク・音量・ミュート・排出、設定保存・復元、デバイス選択、スピーカーテストを確認する。付属の無音素材はAC-3、DTS、E-AC-3、TrueHD。DTS-HD経路はDTSコアをHDMIの高ビットレート形式へ格納して検証する。
