# ミニマムフェーズEQ 開発計画 (Implementation Plan)

`SampleChord` をベースとしつつ、自己回帰（AR）モデル、ベイズ推論、Gaborフレームを用いた超高解像度非FFTアナライザーを搭載し、位相にじみとポストエコーを極限まで抑えた「最強のミニマムフェーズ（最小位相）EQ」を開発するための計画です。

---

## 1. 開発の全体目標と基本アーキテクチャ

`SampleChord` の「往復型ゼロ位相IIR（filtfilt）」を廃止し、**「極小（ゼロ）レイテンシーの最小位相（ミニマムフェーズ）IIR EQ」**へと移行します。
物理特性（クラマース・クローニッヒの関係式）により特定の急峻なカットで局所的な位相回転とポストエコーは避けられませんが、以下の技術的アプローチにより「原音の音楽的要素に一切影響を与えない」レベルまで抑え込みます。

```mermaid
graph TD
    Input[Audio Input] --> DSP[Minimum Phase Cascade EQ]
    DSP --> Output[Audio Output]
    Input --> DryDelay[Dry Delay Buffer]
    Output --> Diff[Diff Switch: Dry - Wet]
    DSP --> AnalyzerThread[Analyzer Thread]
    
    subgraph EQ Engine (AVX2 Optimised)
        DSP --> TPT[64-bit TPT SVF Cascade]
        TPT --> ZPOpt[Polar/Zero Optimization]
    end

    subgraph High-Res Analyzer
        AnalyzerThread --> AR[Burg AR Model Spectrum Estimation]
        AnalyzerThread --> Gabor[Gabor Frame / Sparse Representation]
        AnalyzerThread --> Bayes[Bayesian Parameter / Gibbs Sampler]
        AR & Gabor & Bayes --> UI[GUI spectral rendering]
    end
```

---

## 2. 主要機能と設計方針

### ① ミニマムフェーズIIR EQエンジン
- **フィルター構造**: 従来の `ZeroPhaseFilter`（先読みOLA＋逆方向フィルタ）を廃止し、**Topology-Preserving Transform (TPT) State Variable Filter (SVF)** の直列カスケード接続に変更。
- **高音質化と誤差排除**:
  - 急峻なカット（最大Q=120）や超低域での極値配置における丸め誤差・リミットサイクルを排除するため、フィルター内部演算を完全64ビット（`double`）浮動小数点で実行。
  - TPT構造はアナログの積分器モデルを忠実に再現するため、高Q値でも数値的に極めて安定し、にじみや発振を防ぎます。
- **相互干渉補正（極零点のリアルタイム最適化）**:
  - 各バンド（LowCut/HighCut、4つのベル型EQ）が近接した際に発生するカーブの干渉と、それに伴う不要な位相回転を動的に補正。目標とする全体の振幅特性から数学的最小群遅延（Minimum Group Delay）となる極・零点パラメータの微修正アルゴリズム（軽量な極零点フィッティング）を実装します。

### ② 0.02Hz超高解像度・非FFTアナライザー
- **自己回帰（AR）モデルの導入**:
  - 入力信号に対して、定常フレーム単位（数十ms）で高次AR（自己回帰）モデル（Burg法等）を適用。
  - AR係数からスペクトルエンベロープを解析的に直接プロットすることで、窓長の制限を受けずに、低域から高域まで任意の超高解像度（0.02Hz〜1Hz単位）で鋭い周波数応答を抽出します。
- **ピクセル解像度に合わせた適応的周波数プロット**:
  - 表示領域のピクセル幅（例: 2000px）に対応し、**「低域（〜1kHz）は1Hz以下（ズーム時は0.02Hz）の極密ピッチで計算し、高域に向かうにつれて対数スケールに合わせて計算間隔を広げる」**周波数ワーピングを適用。これにより全帯域をカバーしつつCPU負荷を最小化します。
- **ズーム時の動的レンダリング**:
  - ユーザーが超低域（例: 10Hz〜50Hz）をズームした際に、その表示範囲に対してのみ、0.02Hz刻みの演算をSIMD (AVX2) を用いてバックグラウンドスレッドで集中的に実行。
- **Gaborフレームによる信号分解とベイズ推論（MCMC）**:
  - 信号を時間-周波数のGabor原子に分解し、背景雑音と目的音（トランジェント、音楽成分）を分離。
  - アナライザスレッド内で、ギブス・サンプリング（MCMC）等の確率モデル推定を行い、ノイズに埋もれた微細な調波構造（倍音）を正確に描き出します。

### ③ AVX2 SIMD最適化
- **ステレオ処理の最適化**: `float` / `double` のAVX2ベクトル化を行い、L/chとR/chの処理、および複数IIRセクションの演算を同時に実行。
- **非FFTアナライザーの高速化**: AR係数抽出（自己相関・Burg演算）のループ処理をAVX2でベクトル化し、CPU負荷を大幅に削減。

### ④ JUCE 8.0.2 / Intel GPU 対応
- JUCE 8の新しいグラフィックスエンジン（Direct2D/Vulkan）に対応し、GPU描画を最適化。
- **GPUメモリ（VRAM）共有環境のボトルネック解消**:
  - Intel GPU（iGPU）はメインメモリを共有するため、CPUとのテクスチャ同期が重くなりやすい問題を回避。
  - JUCEの描画バッファに対して `setBackupEnabled(false)` を設定し、CPU側にコピーを持たない**「完全GPUオンリーテクスチャ」**としてアナライザーの波形画像を描画。これにより、CPUスパイクを起こすことなく滑らかな60fps表示を実現します。

---

## 3. ユーザーレビューが必要な項目

> [!IMPORTANT]
> **ゼロ位相から最小位相（ミニマムフェーズ）への仕様変更**
> `SampleChord` のゼロ位相（先読みが発生し、一定のレイテンシーが表示される代わりに位相がフラット）から、今回の設計では「レイテンシーほぼゼロ（数サンプル程度、DAWのバッファサイズに依存しない極小遅延）で動作するが、特定の周波数を急峻にカットした際には局所的な位相回転が発生する」仕様になります。この方向性で進めてよろしいでしょうか？

> [!TIP]
> **非FFTアナライザーの適応的解像度とズーム設計**
> 全帯域を一律で0.02Hzで常時計算するのではなく、通常時は対数スケールの表示ピクセルに合わせた適応的計算を行い、低域ズーム時にのみ自動的に0.02Hzの超高解像度モードへ切り替わる「動的レンダリング」の採用を提案しています。これにより、計算リソースを最適化しつつ、求められる超高解像度測定を実現します。

---

## 4. 提案する具体的な変更コード

### [NEW] [MinimumPhaseEQ.h/cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/DSP/MinimumPhaseEQ.h)
- ゼロ位相処理バッファ（OLA）を廃し、TPT IIRフィルターを直列カスケード接続したゼロレイテンシーEQモジュール。AVX2最適化コードを配置します。

### [MODIFY] [AnalyzerDSP.h/cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/DSP/AnalyzerDSP.h)
- 従来の240バンドIIRフィルタバンク方式から、Burg法ARモデル係数抽出器、ベイズ推定スレッド、およびGaborフレーム分解エンジンへ再構築。

### [MODIFY] [PluginProcessor.h/cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/PluginProcessor.cpp)
- パラメータ設定を「LowCut/HighCut、4バンドBell、Diff（Listen Diff）、Bypass」に変更。
- レイテンシーをゼロ（またはアナライザーバッファ用の極小サンプル）に更新。

### [MODIFY] [FreqResponseDisplay.h/cpp](file:///d:/VST_Project/HighPrecisionEQ/Source/GUI/FreqResponseDisplay.cpp)
- 超高解像度アナライザーから得られた高密度スペクトルデータを滑らかにプロットするための描画ロジックの改善。
- `setBackupEnabled(false)` などのIntel GPU向け描画最適化。

---

## 5. 検証計画

### ① 音響・位相特性の測定 (PluginDoctor等)
- DAWまたはPluginDoctorを通し、振幅特性（ユーザーのEQ設定）が目的通り再現されているか検証。
- 最小位相特性および群遅延（遅延時間の最小化）が達成されているか、位相にじみが局所化されているかを検証。

### ② 計算負荷とAVX2の最適化検証
- `SampleChord` の元の処理と、本EQエンジン（AVX2有効）の処理において、音声処理スレッドのCPU負荷をプロファイラで測定。
- 0.02Hz非FFTアナライザーのバックグラウンド処理が、オーディオ処理スレッドおよびUI描画（GUIフレームレート）に影響を与えないことを検証。
