# 技術上の前提と根拠

| 対象 | 採用する仕様 | 根拠 |
|---|---|---|
| VLCの基準 | 公式3.0.24 x64 SDKと3.0系ABIを使用する | [公式配布](https://download.videolan.org/pub/videolan/vlc/3.0.24/win64/)、[プラグイン定義](https://github.com/videolan/vlc/blob/3.0.x/include/vlc_plugin.h) |
| コンパイラー | VS 2026、v145、C++20、静的MSVCランタイム | [Microsoftの説明](https://devblogs.microsoft.com/cppblog/upgrading-c-projects-to-visual-studio-2026/) |
| メモリ所有権 | VLCの文字列はlibvlccoreが使う関数で解放する。空文字列をMSVCのfreeで解放するSDKインライン関数を避ける | [VLCの変数API](https://github.com/videolan/vlc/blob/3.0.x/include/vlc_variables.h)、同梱SDK |
| インポート | 同梱libvlccore.libを使用。公開名の調査はDLLのdumpbin /exports | [Microsoft /EXPORTS](https://learn.microsoft.com/en-us/cpp/build/reference/exports) |
| 構成表示 | 5.1.2は計8ch。位置が違う7.1と同じ構成とは表現しない | [オブジェクト位置](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/ne-spatialaudioclient-audioobjecttype) |
| 台詞保護 | 相関・帯域による減衰。声だけの完全な除去は保証しない | 同じ帯域の声と楽器を区別する情報がない方式の制約 |
| 形式交渉 | 対応形式を照会し、共有移行時には表現できない位置を混ぜ込む | [IsFormatSupported](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-isformatsupported) |
| 空間API | 静的マスクと動的上限を照会し、イベント待機は100msで区切る | [Spatial Sound](https://learn.microsoft.com/en-us/windows/win32/coreaudio/spatial-sound)、[レンダリング例](https://learn.microsoft.com/en-us/windows/win32/coreaudio/render-spatial-sound-using-spatial-audio-objects) |
| クロック・音量 | 空間・WASAPIのサービスからIAudioClockとIAudioStreamVolumeを取得 | [GetService](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/nf-spatialaudioclient-ispatialaudioobjectrenderstreambase-getservice) |
| IEC 61937 | 元の形式、Windowsキャリア、VLCのDTS-HD変換周期を区別する | [MicrosoftのIEC形式](https://learn.microsoft.com/en-us/windows/win32/coreaudio/representing-formats-for-iec-61937-transmissions)、[VLCパッキング](https://github.com/videolan/vlc/blob/3.0.x/modules/audio_filter/converter/tospdif.c) |
| 圧縮音声の時間 | E-AC-3は入力レートの4倍、HBRは48kHz系と44.1kHz系に対応するキャリアを使う。バースト量と元のフレーム時間から算出する | [VLCのバースト生成](https://github.com/videolan/vlc/blob/3.0.x/modules/audio_filter/converter/tospdif.c)、[FFmpegのTrueHD時間・DTS-HD周期](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/spdifenc.c) |
| ピーク | dBFSとdBTPを区別する。4倍補間FIR上限の保護を使い、番組ラウドネス正規化とは区別する | [EBU R 128](https://tech.ebu.ch/publications/r128/)、[EBU Tech 3343](https://tech.ebu.ch/docs/tech/tech3343.pdf) |

オブジェクト数とエンコード形式は機器とWindows設定に従う。同じ公開名だけで全VLC更新版の互換性を保証せず、配布対象で読み込みを確認する。プリセット値は受信機と部屋に応じて調整する。