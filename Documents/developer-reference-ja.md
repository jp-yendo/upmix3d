# 開発者リファレンス

[English](developer-reference.md) | 日本語

## 必要なもの

- Visual Studio 2026（C++によるデスクトップ開発、CMakeツールを含む）
- インターネット接続（初回のVLC SDK取得時）

## ビルド

リポジトリのルートでPowerShellを開き、次を実行します。

```powershell
.\scripts\fetch-vlc.ps1
.\scripts\build.ps1 -Configuration Release
```

1行目はVLC 3.0.24のSDKを`external/`へ取得します（初回のみ）。2行目はビルドとコアテストを行い、`build/Release/libupmix3d_plugin.dll`を作ります。

Visual Studioで開く場合は`upmix3d.vcxproj`を使います。出力先は`build/vs/Release/`です。

### 既存のVLC SDKを使う場合

`build.ps1`に`-VlcRoot C:\path\to\vlc`を渡します。Visual Studioでは、環境変数`VLC_ROOT`にSDKのフォルダーを指定します。

### インターネットに接続できない場合

VLC 3.0.24 x64のポータブル版を、`external/vlc-3.0.24/`に展開して置きます。

## テスト

```powershell
# コアテストと性能測定
.\scripts\verify.ps1

# 実際のVLCと出力機器を使う結合テスト（音は出ません）
.\scripts\verify.ps1 -DeviceTests

# 2時間分の音声を処理する耐久テスト
.\scripts\verify.ps1 -LongDsp
```

結合テストには、空間サウンドと排他モードに対応し、AVアンプへビットストリーム出力ができる出力機器が必要です。ログは`build/integration/`に保存されます。
