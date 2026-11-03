# Polarity — 频率反相效果器

一个 VST3 效果器：按 **L / ST(Stereo) / R** 绘制一条以频率为 x 轴、以“向该频段添加多少反相信号”为 y 轴的曲线。它不是普通 EQ 的 gain 曲线，而是零相位幅度加权：

```
Y_c(f) = (1 − A_c(f)) · X_c(f)        A_c(f) ∈ [0, 2]
```

| A | 听感/效果 | 等效幅度 |
|---|---|---|
| 0 | 原样 | +1 |
| 0–1 | 部分抵消 | 1−A |
| 1 | 该频段完全抵消（NULL） | 0 |
| 1–2 | 开始反向 | 1−A（负） |
| 2 | 该频段完全极性反转（INVERT） | −1 |

与普通 EQ 的区别：普通 EQ 的 cut 是最小相位滤波，cut 后的信号与原信号相加不会抵消；这里用零相位 FIR 实现 `1−A` 的幅度加权，`A=1` 是真正的频带 null，`A=2` 是真正的极性反转。L/ST/R 底层只存 `A_L`、`A_R` 两条曲线；编辑 ST 时同时写入两侧。不提供 Mid/Side 编辑层，但会显示 M/S 电平与相关度作为警告（单侧反相会把能量推入 Side）。

## 当前进度

- [x] DSP 数学验证（Python 参考实现）
- [x] C++ DSP 核心：CurveModel（PCHIP）、Fft、FirDesigner、InversionEngine
- [x] JUCE 8 插件工程：CMake + VST3 + Standalone 构建通过
- [x] AudioProcessor 集成：2048 样本延迟、mix/bypass、PDC
- [x] M2：曲线编辑器（L/ST/R、DRAW/NODE/ERASE、undo/redo）、每通道曲线、
      实时换核 + 60ms 交叉淡化、曲线随工程/预设保存
- [x] M3：M/S 电平表 + 相关度、A/B 曲线槽、文件预设栏
- [x] 画布叠加：输入/输出频谱、反相量热力图（SPECTRUM / POLARITY 开关）
- [x] 波形对比：输入/输出填充、RMS dB 包络、冻结、6 档时间窗口
- [x] 浅色纸感主题：暖纸底 + 墨色 + 朱红强调；自定义扁平 LookAndFeel
- [x] 输出 NORM / DELTA 双模式；波形 RMS / PEAK 指标可选
- [x] 质量档：2048 / 4096 / 8192 taps（切换延迟与频率分辨率，不重新分配内存）
- [x] 宿主集成：A/B、QUALITY、EDGE 成为 VST 参数；完整参数文本/单位/gesture；
      宿主自动化在线程安全的消息线程应用中；旧工程状态自动迁移
- [x] 离线测试：DSP 自检 + Processor 集成测试 + A/B/预设/计量/分析/质量档/宿主参数 + 编辑器 smoke 全部 PASS
- [x] CPU 优化：SSE2 浮点卷积（分块 double 累加）、频谱 FFT 限到约 30Hz、裸指针音频循环
- [ ] M4 剩余：采样率矩阵、pluginval、打包

当前插件默认曲线是**平直（A=0，透明）**。用 DRAW 在画布上画出反相量；ST 层会同时写入 L 和 R。

## 构建

```bash
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release --target Polarity_VST3 Polarity_Standalone PolarityTests --parallel 4
```

产物：

- VST3：`Build/Polarity_artefacts/Release/VST3/Polarity.vst3`
- Standalone：`Build/Polarity_artefacts/Release/Standalone/Polarity.exe`
- 集成测试：`Build/PolarityTests_artefacts/Release/PolarityTests.exe`

`COPY_PLUGIN_AFTER_BUILD` 目前为 `FALSE`，避免写入系统 VST3 目录需要管理员权限。在 DAW 里把插件扫描路径指向上面的 VST3 文件夹，或手动复制到用户级 `%APPDATA%\VST3`。

## 使用

- 默认曲线平直，插件透明。
- **DRAW**：按住拖动，在光标频率直接写入反相量。
- **EDGE**（底栏 FIR 组，与 QUALITY 同组）：Off / 1 bin / 2 bins / 4 bins（默认 2 bins）。在 FIR 设计时对 H(f) 过渡做频域平滑，缓解陡峭曲线边界的振铃/预回声；A=0/A=2 的平坦区域仍然精确。
- **QUALITY**（底栏 FIR 组）：2048 / 4096 / 8192 taps，选项文本显示当前采样率下的实际延迟（48kHz 时约 21.3 / 42.7 / 85.3 ms）。taps 越大，频率分辨率越高、深 null 越容易做出来，但 PDC 延迟越大；切换时插件会上报新的延迟，宿主可能重新同步播放位置。默认 4096。
- **NODE**：拖动控制点；双击空白加点；右键点击控制点删除。
- **ERASE**：在局部把曲线擦回 0。
- **L / ST / R**：L 只编辑左通道，R 只编辑右通道，ST 同时写入两侧。
- y 轴：0 = 原样，1 = NULL（完全抵消），2 = INVERT（极性反转）；x 轴 20Hz–20kHz。
- UNDO / REDO 撤销重做；MIX 干湿；BYPASS 保持延迟对齐。
- **A / B**：两套独立曲线槽，用与 L/ST/R 相同的分段按钮切换；两套曲线都随工程保存。
- 右侧 **M/S** 表：显示处理后的 Mid/Side 电平与 L/R 相关度；相关度为负时变红，提示单声道兼容风险。
- **画布叠加**（`SPECTRUM` / `POLARITY` 两个开关）：
  - SPECTRUM：灰蓝线 = 输入（已按插件延迟对齐），墨绿主线 = 输出；纵轴 −100…0 dB，刻度在画布右侧。
  - 分析：16384 点 FFT、512 个对数频带、±2 bin 三角窗功率加权；SMOOTH 可选 Off / 1/12 / 1/6 / 1/3 oct（默认 1/12）；TIME 可选 50 / 100 / 200 / 400 ms（默认 200ms）；画布 30Hz 定时刷新，不依赖鼠标悬停。
  - 右侧带 dBFS 刻度（0 / −20 / … / −100）；SLOPE 可选 Flat / 3 / 4.5 / 6 dB/oct（默认 4.5），用于和 Pro-Q 的 Analyzer Slope 对齐；TIME 控制频谱跟随速度。
  - 与 Pro-Q 对比时看输出（亮色）曲线，并把 Pro-Q 的分析器放在 Polarity 之后、斜率设成相同值；两者的 FFT 窗口、立体声求和方式不同，不会逐点一致。
  - POLARITY：频谱与 x 轴之间的面积按当前编辑层的 A(f) 着色；画布底部另有 L/R 两行极性条。
  - 颜色：浅蓝 0 → 钴蓝 0.5 → 沙色 NULL → 橙 1.5 → 朱红 INV 2。
  - 关掉 SPECTRUM 后 POLARITY 仍会显示底部极性条；两个都关则完全隐藏。
- 底部 **WAVEFORM**：灰蓝 = 输入，墨绿 = 输出；RMS 包络与零轴之间填充。
  - 头部 **NORM / DELTA** 输出模式：NORM 为正常处理；DELTA 实际输出 **input − output**（被移走/反相的部分）。波形/频谱/M-S 的 OUT 都跟随实际监听输出。
  - 面板头部右侧的 `WAVE` 下拉：**500 ms / 1 s / 2 s / 3 s / 5 s**，默认 **1 s**。
  - 波形只保留 **IN / OUT** 两条；同一行的 `METRIC` 可选 **RMS / PEAK**（Peak = 每个时间桶的 max(|min|,|max|)，RMS 更平滑）。
  - 面板左侧有 0 / −20 / −40 / −60 dB 刻度和水平网格，底部有时间轴（−1 s … 0）；面板高度 170–210px。
  - **单击波形区域冻结当前快照，再单击恢复实时**；冻结时标题和提示变黄，并保持冻结时的窗口毫秒数。
  - **切换 WAVE 时间窗会自动解除冻结**，应用新窗口；当前窗口由 WAVE 下拉本身显示。
  - 指标选择存进插件状态（非自动化参数），工程/预设恢复时保留。
  - 频谱与波形使用 L/R 单声道和；左右差异大时结合右侧 M/S 表判断。
- **PRESET**：顶部输入名字后点 SAVE；下拉选择即加载。预设保存在
  `%APPDATA%\Polarity\Presets`（XML），包含 A/B 两套曲线、当前槽、mix/bypass。
- 曲线随工程/预设保存；宿主恢复状态时编辑器自动刷新。
- 拖动曲线时后台实时重建 FIR，引擎用 60ms 交叉淡化换核，不会爆音。

## 宿主集成（FL Studio）

FL 的“编辑参数”、自动化和提示面板只认真正的 VST 参数。插件暴露：

| 参数 | 文本 | 自动化 |
|---|---|---|
| Mix | `0%…100%` | 是 |
| Bypass | `On` / `Off` | 是 |
| Delta Out | `NORM` / `DELTA` | 是 |
| A/B Slot | `A` / `B` | 是 |
| Quality | `2048 taps` / `4096 taps` / `8192 taps` | **否**（改变 PDC） |
| Edge Smoothing | `Off` / `1 bin` / `2 bins` / `4 bins` | 是 |

- 所有参数都带 VST3 的 title / value text / units，FL 参数列表与提示面板显示的是这些文本。
- NORM/DELTA、A/B、QUALITY、EDGE 的控件用 `ParameterAttachment` / `ComboBoxParameterAttachment` 驱动，选择时自动发送 `beginEdit → performEdit → endEdit`，因此 FL 的撤销/重做能覆盖这些编辑；MIX/BYPASS 由 JUCE attachment 处理。
- 宿主在音频线程改参数时，处理器只写原子 pending 值并 `triggerAsyncUpdate()`；真正的换核、`setLatencySamples` 和状态写入都在消息线程完成，连续自动化会被合并。
- **插件内悬停提示**：鼠标在任意控件（含曲线、波形、M/S 面板）停留约 500ms，会弹出与纸感主题一致的 tooltip，说明该控件的作用。宿主提示栏仍只在“触碰/编辑参数”时提示，因为 VST3 没有 hover 接口；tooltip 不参与自动化、撤销或状态保存。
- **限制**：自由绘制的曲线、SPECTRUM/POLARITY、SLOPE/SMOOTH/TIME、WAVE/METRIC 不是宿主参数，因此不会出现在 FL 的自动化/提示面板里，曲线仍用插件内 UNDO/REDO。这是 VST3 没有通用“内部状态撤销/提示”通道导致的。
- QUALITY 在 FL 中可能显示为不可自动化；手动修改会改变延迟，宿主可能重新同步播放位置。旧工程加载时会把原来的 `quality` 状态属性与 `CURVES.active` 迁移到新参数。

## 界面

设计方向是 **纸 / 墨 / 朱红**：暖白纸底、墨色主操作、朱红强调与左侧 2px 边条，刻意避开 soothe2 的冷蓝灰。控件无边框，只用填充色区分；主面板用柔和阴影分层，没有渐变或拟物旋钮。

- 模式按钮放在浅沙色底板里：未选中透明、选中墨底纸字；`SAVE` 为朱红底纸字；Undo/Redo 为幽灵文字。
- 字体层级统一为四级：品牌 26 / 主要 13（副标题、鼠标读数）/ 控件 12（按钮、下拉、字段名、预设、滑杆数值）/ 注释 11（频率/反相量/dBFS 刻度、M/S、波形标题/提示、警告）；最小字号不低于 11。
- 布局原则是“设置就近”：分析器行只放副标题与频谱的 SLOPE/SMOOTH/TIME；编辑行的顺序是 A|B → L/ST/R → 工具 → `SPECTRUM|POLARITY` → Undo/Redo，`POLARITY` 按钮右缘与频谱绘图区右缘（dBFS 刻度槽左侧）对齐，Undo/Redo 位于其后、贴内容右缘；主区曲线 + 瘦身 M/S；波形面板头部放 WAVE/METRIC；底栏 MIX 左，QUALITY/EDGE 收进右侧 FIR 分组底板。
- 窗口默认 960×720，最小 940×640；最小宽度保证 13pt 副标题与频谱簇不重叠，同时保持开关组与频谱绘图区对齐。
- **统一左基准线**：标题、副标题、A/B、曲线 20Hz Y 轴、波形面板左缘、MIX 都落在同一条 40px 竖线上；左侧 40px 作为品牌斜杠 + INV/1.5/NULL/0.5 刻度槽。
- **统一右基准线**：WAVEFORM 面板右缘与 M/S 面板右缘对齐，外框和内绘图区都参与对齐。
- L = 钴蓝，R = 朱红，ST = 紫；频谱输入灰蓝、输出墨绿；热力图浅蓝 → 钴蓝 → 沙色 → 橙 → 朱红。
- M/S 表：M 钴蓝、S 紫；波形输入灰蓝、输出墨绿。
- 主题色集中在 `Source/ui/PolarityTheme.h`，按钮/下拉/滑杆/开关由 `PolarityLookAndFeel` 统一绘制。

## 测试

```bash
# 纯 C++ DSP 自检（numpy 参考实现的等价物，可用 MinGW 或 MSVC 编译）
g++ -std=c++17 -O3 -I Source Source/dsp/CurveModel.cpp Source/dsp/Fft.cpp \
    Source/dsp/FirDesigner.cpp Tests/dsp_tests.cpp -o Build/dsp_tests.exe
PATH="/c/mingw64/bin:$PATH" ./Build/dsp_tests.exe

# 真实 AudioProcessor 离线测试（JUCE）
./Build/PolarityTests_artefacts/Release/PolarityTests.exe
```

实测结果（48kHz，4096 taps）：

```
DSP:
  identity       : max error 0.000e+00
  full invert    : max error 0.000e+00
  band null      : 400Hz -82.2dB / 800Hz -83.2dB / 1200Hz -84.6dB / 1600Hz -86.7dB
  latency        : 2048 samples (42.7 ms)
  resolution     : 11.72 Hz

Processor:
  latency          : 2048 samples
  L 400 Hz null    : -96.2 dB   (L 曲线 null，R 曲线平直，EDGE=2 bins)
  R 400 Hz pass    : -0.0 dB
  L 5 kHz pass     : -0.0 dB
  mix=0 dry error  : 0.000e+00
  bypass error     : 0.000e+00
  crossfade maxstep: 0.0653      (等于 1kHz 正弦自身的每样本斜率，无换核爆音)
  crossfade tail   : -97.8 dB    (换核后新曲线确实生效)
  A/B slots        : PASS        (两套曲线 + 当前槽随状态保存/恢复)
  preset roundtrip : PASS        (临时目录保存/加载往返)
  meters corr/anti : +1.00 / -1.00  (side -72.0 / mid -72.0 dB)
  meters test      : PASS
  analysis f/n     : -18.3/-18.3 -> -18.0/-114.5 dB  wave 0.0000
  analysis test    : PASS        (1kHz 正弦：输入/输出频谱、NULL 后输出与波形)
  delta normal L/R : -96.2 / -0.0 dB
  delta L/R        : -0.0 / -300.0 dB
  delta A=2 L      : 6.0 dB
  delta test       : PASS
  quality 2048     : lat 1024  null -85.1 dB  dry 0.0e+00
  quality 4096     : lat 2048  null -96.2 dB  dry 0.0e+00
  quality 8192     : lat 4096  null -138.3 dB  dry 0.0e+00
  quality test     : PASS        (三档延迟、null、旁通延迟对齐、状态保存)
  benchmark        : 18.5x / 12.7x / 7.7x realtime (2048/4096/8192 taps)
  host params      : meta PASS slot PASS quality PASS edge PASS state PASS legacy PASS
  host params test : PASS        (参数元数据、宿主自动化、状态往返、旧状态迁移)
  editor smoke     : PASS        (构造浅色主题 UI)
  PROCESSOR TEST   : PASS
```

## 试听参考实现

`Demo/` 下是参考实现生成的 8 秒对数扫频素材（`*-dry.wav` 为干声）：

- `st-null.wav`：200Hz–2kHz 完全抵消（与当前插件的固定曲线一致）
- `split.wav`：L 抵消 200–800Hz、R 抵消 3–8kHz
- `shelf-invert.wav`：200–800Hz 极性反转（A=2）
- `identity.wav`：A=0，完全透明

重新生成：

```bash
python Tools/reference_render.py --preset split --out Demo/split.wav --compensate
python Tools/reference_render.py --self-test
```

## 设计要点

- **零相位 FIR**：在 N/2+1 个频点采样 `H=1−A`，补共轭对称谱，IFFT 后循环右移 N/2。延迟 `N/2` 样本，频率分辨率 `fs/N`。
- **不加窗**：加窗会破坏 `A=0`/`A=2` 的精确性。频域采样的 Dirichlet 插值纹波是固有代价，用 taps 和曲线平滑度管理。
- **质量档 2048 / 4096 / 8192 taps**（默认 4096）：48kHz 下延迟 21.3 / 42.7 / 85.3 ms，分辨率 23.4 / 11.7 / 5.9 Hz。引擎始终按 8192 分配环形缓冲与核槽，切换只改活动核长度和 `setLatencySamples`，音频线程无分配。
- **卷积优化**：核与历史改为 float，内层用 SSE2 4-wide 点积（`_mm_loadu_ps`），每 256 个抽头把 float 部分和汇入 double，兼顾速度与空域精度（8192 taps 实测 null −138 dB）。48kHz/512 块下 4096 taps 约 12.7× 实时（单核），8192 taps 约 7.7×。
- **分析优化**：频谱 FFT（16384 点 ×2）从每块计算改为约 30Hz 更新一次（按采样率/块大小自适应），时间平滑系数按实际更新间隔换算；波形仍每 4 块更新。
- **曲线模型**：控制点 + PCHIP（log 频率），20Hz/20kHz 端点固定为 0。
- **宿主参数**：`mix`、`bypass`、`deltaOut`、`slot`、`quality`、`edge` 都是 APVTS 参数，带可读名称、数值文本与 gesture；`quality` 改变 PDC，标记为不可自动化。
- **宿主状态**：曲线仍整体存插件状态（`CURVES`），A/B 槽通过 `slot` 参数同步；曲线编辑不进宿主撤销历史。

完整方案见：`C:\Users\lenovo\.claude\plans\vst-left-stereo-right-eq-mid-side-mid-s-shimmering-deer.md`。

## 工程结构

```
CMakeLists.txt
Source/
  PluginProcessor.h/.cpp        # APVTS、A/B 曲线槽、计量、总线、延迟、dry/wet、PDC
  PluginEditor.h/.cpp           # 主界面、预设栏、A/B、层/工具按钮、MIX/BYPASS
  ui/
    CurveEditor.h/.cpp          # 画布、L/ST/R、工具、undo、频谱/热力图叠加
    MeterPanel.h/.cpp           # M/S 电平与相关度表
    WaveformPanel.h/.cpp        # 输入/输出波形、填充、速度窗口、冻结
    PolarityLookAndFeel.h/.cpp  # 扁平浅色控件外观
    PolarityTheme.h             # 纸 / 墨 / 朱红调色板
    PolarityType.h              # 26 / 13 / 12 / 11 字号层级
  state/
    PresetManager.h/.cpp        # 文件预设（用户目录 XML）
  dsp/
    CurveModel.h/.cpp           # PCHIP 曲线
    Fft.h/.cpp                  # radix-2 FFT
    FirDesigner.h/.cpp          # 频域采样 -> 线性相位 FIR
    InversionEngine.h/.cpp      # 每通道 FIR + 4 缓冲换核 + 交叉淡化
Tests/
  dsp_tests.cpp                 # 纯 DSP 自检
  processor_tests.cpp           # AudioProcessor 集成测试
Tools/reference_render.py       # numpy/scipy 参考实现
ThirdParty/JUCE-8.0.15/         # 本地 vendored JUCE（不入库）
Demo/                           # 试听 WAV（可重新生成，不入库）
```

## 已解决的构建前提

- Visual Studio Community 2022（D 盘），MSVC 14.43，Windows SDK 10.0.22621。
- JUCE 8.0.15 已解压到 `ThirdParty/JUCE-8.0.15/JUCE`。
- 本机 CMake 4.0.0-rc4 实测可用。
