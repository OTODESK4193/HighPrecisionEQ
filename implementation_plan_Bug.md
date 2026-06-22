# HighPrecisionEQ Bug Fix Implementation Plan

イコライザー応答カーブが直線のままになる不具合、アナライザーがリアルタイムで動かない不具合、マウスドラッグ・ホイール操作が効かない不具合、およびON/OFF（バイパス）ボタンの視認性向上に対応するための実装計画です。

---

## User Review Required

> [!IMPORTANT]
> - **アナライザーエンジンの書き戻し**: 
>   現在の Burg AR（自己回帰モデルによるスペクトル推定）の実装は計算負荷が高く、動作に不具合があるため、オリジナルの `SampleChord` (LowCutPolice) で正常動作していた **SVF (State Variable Filter) フィルタバンク（240バンド並列）** に書き戻します。これにより、リアルタイムで極めて滑らかに動作するアナライザーが復元されます。
> - **GUI操作（ドラッグ＆ホイール）のパラメータ直接制御**:
>   スライダーアタッチメントの動的切り替えによる値の不整合を防ぐため、グラフ上のドラッグおよびホイール操作時に **APVTSのパラメータオブジェクトに対して直接 `setValueNotifyingHost` を実行する堅牢な方式（LowCutPoliceオリジナル仕様）** に修正します。これにより、Ctrl+ドラッグによる微調整や、Alt+ホイールでのQ値/スロープ調整、Shift+ドラッグでのSoloモニターなど、高度な操作ロジックがすべて正常に動作するようになります。

---

## Proposed Changes

### 1. DSP (Digital Signal Processing)

#### [MODIFY] [MinimumPhaseEQ.cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/DSP/MinimumPhaseEQ.cpp)
- `MinimumPhaseEQ::getMagnitudeForFrequency` において、`activeSections` ではなく `pendingSections` を `lock` 保護下で参照して計算するように修正します。これにより、DAWが再生停止中で `process` が動いていない場合（`activeSections` が未同期の場合）でも、パラメータ変更が即座にEQカーブ表示に反映されます。

#### [MODIFY] [AnalyzerDSP.h](file:///d:/VST_Project/HighPrecisionEQ/Source/DSP/AnalyzerDSP.h)
- 構造体 `AnalyzerBand` (SVFバンド) の定義を追加。
- メンバー変数をアトミックな `peaks` 配列、`ringBuffer`、`localBuf`、`attackCoef`, `releaseCoef` に変更し、Burg AR関連の変数を削除します。
- `getDetailedSpectrum` などの Burg AR 特有のメソッドを削除し、`getEnergies()` を維持します。

#### [MODIFY] [AnalyzerDSP.cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/DSP/AnalyzerDSP.cpp)
- オリジナルの `LowCutPolice` の SVF フィルタバンク（240バンドの並列処理とエンベロープフォロワーによるアトミックな `peaks` 更新）に差し替えます。
- アナライザスレッドは30ms毎に起動し、スレッド切り替えコストを最小化します。

---

### 2. GUI (Graphical User Interface)

#### [MODIFY] [FreqResponseDisplay.cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/GUI/FreqResponseDisplay.cpp)
- **描画処理**: `analyzer->getEnergies()` を取得し、240バンドの対数周波数（`fc`）位置を `logFToX(fc)` に変換してプロットするオリジナル描画ロジックに戻します。
- **VBlank同期**: `vblankUpdate()` で `analyzer->getEnergies()` の最大値が `-89.5dB` 以上のとき（何かしらのオーディオ信号が存在するとき）に `repaint()` をトリガーする仕様に戻します。
- **入力イベント (mouseDrag, mouseWheelMove等)**:
  - `editor->...Slider.setValue(...)` を経由するのではなく、`processor->apvts.getParameter(...)->setValueNotifyingHost(...)` を用いてパラメータを直接更新するように修正します。
  - Ctrl+Dragによる微調整、Shift+DragによるSoloモード切り替え、Alt+WheelでのQ値/スロープ値の増減、Command/Ctrl+WheelでのX軸ズーム、通常のWheelでのアナライザーオフセット調整など、オリジナルの高度なインタラクションをすべて復元します。

#### [MODIFY] [PhaseDisplay.cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/GUI/PhaseDisplay.cpp)
- `drawAnalyzerSpectrum` を修正し、`analyzer->getEnergies()` を用いて240バンドの対数プロットを行う描画方法に修正します。
- `vblankUpdate()` における再描画判定も、`FreqResponseDisplay` と同様に `analyzer->getEnergies()` の最大値チェックに変更します。

#### [MODIFY] [PluginEditor.cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/PluginEditor.cpp)
- `updateGraph` ラムダ式の末尾に `updateComponentColors();` を追加します。
- これにより、パラメータ変更（GUI上のドラッグ、マウスホイール、DAWのオートメーションなど）が発生した際に、ON/OFFボタンの明るさ（ON時は鮮やかなバンド固有色、OFF時は暗い消灯色）およびテキスト表示（"ON" / "OFF"）が即座かつ確実に同期して更新されるようになります。

---

## Verification Plan

### Automated Tests
- CMake でリビルドし、コンパイルエラーが無いことを確認します。
  - ビルドコマンド: `cmake --build build_msvc --config Debug`

### Manual Verification
1. **EQカーブの動作検証**:
   - プラグインを立ち上げた段階で、各バンドのノブを操作したときに、周波数応答ディスプレイ（および位相ディスプレイ）上のEQカーブがリアルタイムかつ滑らかに変化することを確認します。
2. **アナライザーの動作検証**:
   - DAW上でオーディオを再生した際、スペクトラムがリアルタイムで周波数ディスプレイ（および位相ディスプレイ）の背景に表示・追従することを確認します。
   - 再生を停止した際、滑らかに（150ms의 リリース係数に従って）減衰して消えることを確認します。
3. **GUI操作（マウスドラッグ＆ホイール）の検証**:
   - ディスプレイ上の白丸（イコライザーポイント）を左右上下にドラッグして、周波数とゲインがスムーズに同期して変化することを確認します。
   - `Ctrl`キーを押しながらドラッグした際に、動きが微調整（ファインチューニング）されることを確認します。
   - `Alt`キーを押しながら白丸上でホイールをスクロールした際、BellバンドではQ値、LowCutバンドではスロープ値（12, 24, 36, 48, ...）がスクロールに応じて変化することを確認します。
   - `Ctrl` または `Command` キーを押しながらスクロールした際、ディスプレイのX軸（周波数軸）のズーム倍率が変化することを確認します。
   - 白丸が無い場所でスクロールした際、アナライザーの表示基準オフセット（上下シフト）が変化することを確認します。
   - 白丸を `Shift`キーを押しながらクリック・ドラッグした際、一時的にそのバンドのみを際立たせるSoloモニターモードが機能することを確認します。
4. **ON/OFF（バイパス）ボタンの視認性検証**:
   - バンドごとの「ON」ボタンを押したときに、ON時はボタン全体が明るいバンド固有色で点灯（テキスト "ON"）、OFF時は暗いグレーで消灯（テキスト "OFF"）にパッと切り替わることを確認します。
