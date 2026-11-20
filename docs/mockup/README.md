# Ethereal 主题 + 设置页 · 效果评估

> **已落地（第一版）**：三档主题（纯文字 / 轻卡片 / Ethereal）、壁纸取色、不透明度与圆角、
> 显示内容开关、自绘设置窗口（外观 / 显示内容 / 关于三页）都已在 `hspec.cpp` 实现。
> 本文保留原始评估与实现要点，作为后续改动的依据。
>
> **2026-11-15 样张重制**：样张现由 `src/render_mockups.py` 按
> [`docs/design-spec.md`](../design-spec.md)（唯一样式依据）渲染，与
> 「无粗体 + 大字号」改版后的实现 1:1 对齐。布局数值直接取自
> `hspec.cpp` 的 `PageLook` / `PageContent` / `SettingsDraw`（卡片圆角 14、
> 卡间距 14、滑杆行距 38、页标题 24 / 节标题 18 / 行名 15 / 说明 13.5）。
> 含三张**新设计**样张（面板行为页、面板可点击交互、面板视觉升级）。

样张用 Edge 无头截图渲染（2x，oklch 与 MiSans Medium 直出），不是程序截图
（程序截图见 `docs/preview.png`）。重新生成：

```
python docs/mockup/src/render_mockups.py        # 全部
python docs/mockup/src/render_mockups.py theme  # 按名字筛选
```

依赖：`build/font-backup/MiSans-Medium.ttf`（不入库，见 `tools/subset_font.py`）
与 Edge（`C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`）。
HTML 中间产物在 `src/_build/`，改样式改 `render_mockups.py` 里的参数。

| 文件 | 内容 |
| :--- | :--- |
| `panel-light.png` | 浅色壁纸：纯文字 vs Ethereal 卡片 |
| `panel-dark.png` | 深色壁纸：同上 |
| `panel-busy.png` | **复杂壁纸**：纯文字在明暗交界处丢字，卡片把对比度钉死 |
| `settings-theme.png` | 设置页 · 外观（三档主题缩略图 / 主色 / 面板），浅 + 深 |
| `settings-content.png` | 设置页 · 显示内容（六项开关），浅 + 深 |
| `settings-behavior.png` | **设置页 · 面板行为（新设计）**：位置四角 / 缩放 / 刷新间隔，浅 + 深 |
| `about.png` | 关于页，浅 + 深 |
| `panel-interaction.png` | **面板可点击交互状态（新设计）**：默认 / hover / 点击复制反馈 + 状态机 |
| `panel-upgrade.png` | **面板视觉升级方向（新设计）**：现状 vs 升级态 + 四个升级点 |

---

## 结论

**值得做，但当成第三档主题落地，不要替换现在的纯文字版。** 理由：纯文字版的价值是"和壁纸融为一体、零存在感"，这在 PE 和极简桌面里是优点；卡片版的价值是"可读性不再依赖壁纸"，并且是**后续放交互元素的唯一容器**（没有卡片底，按钮和分隔线没法画）。

设置页的真正价值不是"让用户调参数"，而是：**给后面要加的功能一个落脚点**，同时逼出第一套自绘交互层（命中区、hover、键盘），这套东西以后面板要支持点击时可以直接复用。

关于页零风险，纯静态，可以第一批做。

---

## 一、主题怎么落

Ethereal 的变量体系是 `--hue` 驱动全色板的 oklch。面板只需要其中 6 个 token，不要整套照搬：

| token | oklch（hue 250） | sRGB | 面板里用在哪 |
| :--- | :--- | :--- | :--- |
| `primary` | `oklch(.7 .14 h)` | `#53A3F2` | 强调条、开关、选中环 |
| `btn-regular-bg` | `oklch(.95 .025 h)` | `#E2F0FF` | 图标 chip 底色 |
| `btn-content` | `oklch(.55 .12 h)` | `#3275B4` | 图标线条 |
| `deep-text` | `oklch(.25 .02 h)` | `#1A222B` | 标题 |
| `sub-text` | `oklch(.5 .02 h)` | `#5B646F` | 明细 |
| `card-bg` | `white` / `oklch(.23 .015 h)` | `#FFFFFF` / `#181E24` | 卡片底 |

几何：卡片 480×N dp、圆角 16、内边距 16/18、图标 chip 30dp 圆角 9、title 与明细行距不变。**刻意不改字号**——改了就不是"同一个面板换皮"，而是换了产品。

深色只要换 6 个值，其余（布局、图标、文案）完全不动。

---

## 二、四个真正的难点（都不在"画不出来"，在"画法要变"）

### 1. 明暗判定的对象变了
现在 `UpdateTheme()` 按壁纸 100 点平均亮度（阈值 118）决定 `g_lightText`，然后 `ApplyPalette()` 在三组固定色里挑一组。有卡片后，**文字色应该由卡片决定**（浅卡片永远深字），壁纸亮度只决定"用浅卡片还是深卡片"。要拆成两层：`sheet = f(wallpaperLuma)`、`text = f(sheet)`。改动不大（约 60 行），但不拆就会出现在浅色壁纸上「白卡片 + 白字」。

### 2. 文本 alpha 反推对中浅色不友好
`DrawTextGdiLine()` 的做法是：先用 GDI ClearType 在纯色底上画一遍，再按 `a = (bg − lum) × 255 / (bg − colorLum)` 反推 alpha。**文字色越接近底色，分母越小，误差越大**。现在明细色是 `#3A3A3A`（亮度≈58，分母 197），换成 `sub-text #5B646F`（亮度≈100，分母 155）之后边缘会软一点，100% DPI 下能看出来。

结论：明细色**不要比 L≈0.45 更浅**；真要更浅，得给这条路径换个按色度反推的实现。这不是审美问题，是现有管线的一条硬约束。

### 3. 背景模糊没有 API，但这里恰好不需要
GDI+ 没有 backdrop blur。但面板的"背景"就是壁纸——`UpdateTheme()` 本来就 `Gdiplus::Bitmap img(wallpaperPath)` 打开了整张壁纸。在采样区做一次降采样→模糊→上采样，把结果贴进卡片区域即可，**只在壁纸变化时算一次**，不进渲染循环。

代价说清楚：**桌面图标不会被模糊**（图标在壁纸之上、面板之下）。所以卡片不透明度不能低于 ~0.80，否则图标会从卡片里透出来。样张里用的是 84%。

### 4. 阴影要画进同一张 layered 位图
`UpdateLayeredWindow` 只能整块提交，阴影必须自己画（模糊圆角矩形）。窗口尺寸要留出 ~24dp 的透明边距——现在 `PositionWindow()` 的 18/22dp 外边距正好可以吸收，但 `g_winW` 的 480 里要重新分配。

---

## 三、设置窗口：为什么必须另开一个 HWND

面板是 `WS_EX_TRANSPARENT`（点击穿透），**不可能**在里面放控件。三条路：

| 方案 | 代价 |
| :--- | :--- |
| 原生控件（BUTTON / STATIC） | 长得不像 Ethereal，还得逐个 owner-draw，等于自绘 + 兼容坑 |
| **自绘单窗口（推荐）** | 一个 `WM_PAINT` 全画完；但要自己写命中区表、hover、Tab/Esc、DPI 重排 |
| WebView2 | 违背"零第三方依赖" |

自绘要多写的东西列清楚：命中区数组（rect + 动作，布局时重建）、`WM_MOUSEMOVE` hover、`WM_LBUTTONUP` 派发、`WM_KEYDOWN` 的 Tab/Enter/Esc、`WM_DPICHANGED` 重排、光标形状。粗估 **+700 ~ 900 行**，是全项目最大的一块新增代码。

结构建议 4 页：

1. **外观** — 主题三选一（缩略图）、主色（跟随壁纸 / 手调 hue）、不透明度、圆角
2. **显示内容** — CPU / 主板 / 内存 / 硬盘 / 显卡 / 网卡 开关（要动 `BuildModel()`，关掉的项连采集都跳过）
3. **面板行为** — 位置四角、缩放、刷新间隔
4. **关于**

MVP 可以先做 **外观 + 显示内容 + 关于**，"面板行为"继续留在托盘菜单里，等真有人要再搬。

---

## 四、关于页

纯静态，复用同一个 shell，约 60 行。信息项建议选「许可 / 界面字体 / 数据来源 / 运行方式」这种能回答"这程序靠不靠谱"的，比堆版本号有用。版本号从 `hspec.rc` 的 `VS_VERSION_INFO` 读，不要写死。

---

## 五、预览图：程序化绘制，不要内嵌 PNG

| 方案 | 问题 |
| :--- | :--- |
| 内嵌 PNG 资源 | 每个主题 +30~60 KB；改主色就要重出图；和真实渲染容易脱节 |
| **程序化绘制（推荐）** | `DrawThemeThumb(g, rect, hue)` 每档 20~40 行，零资源，跟着 hue 自动变 |

尺寸两档：主题卡 161×84（设置页）、功能行 68×40。**缩略图里不要画真字**——161×84 下 12px 中文 AA 之后糊成一团，只画色块和条；顺带省了字体子集。

这也直接回答"后面加功能要有预览图"：每个新功能配一个 thumb 函数，设置页自动长出来，不需要维护一堆 png。

---

## 六、两个容易漏的隐性成本

**1. 字体子集必须重跑。** 新增中文文案（外观、主色、不透明度、圆角、显示内容、面板行为、关于、许可、界面字体、数据来源、运行方式、复制版本信息……）不在现有 MiSans 子集里就会**整行回退雅黑**，字重和字面都不一样，肉眼立刻能看出来。改完跑 `python tools/subset_font.py --check`，需要的话把词加进 `tools/hardware_words.txt`。

**2. 配置存哪里。** README 现在写着"不写注册表、不开机自启"。加设置就得落盘：
- `HSpec.ini` 放 exe 同目录（便携，PE 里若介质只读则退化为内存内配置 + 托盘提示）
- 或 `%APPDATA%\HSpec\config.ini`

建议前者 + 同步把 README 改成"默认不写注册表，配置保存在程序目录的 HSpec.ini"。

---

## 七、分期建议

| 阶段 | 内容 | 风险 |
| :--- | :--- | :--- |
| 1 | 关于页 + 外观页（主题切换 + 主色）+ 程序化缩略图 | 低，能先把设计基调定死 |
| 2 | 卡片渲染层（palette 拆分、壁纸模糊、阴影）+ 三档主题 | 中，动到 `UpdateTheme` / `RenderInternal` |
| 3 | 显示内容页 | 中，动到 `BuildModel()` 的采集路径 |
| 4 | 面板行为 / 面板可点击 | 高，摘 `WS_EX_TRANSPARENT` 是结构变化，单独讨论 |

代码量粗估：主题渲染 +120、palette 拆分 +60、壁纸模糊 +50、设置 shell 与控件 +700~900、缩略图 +120、配置读写 +120，另加重跑字体子集与 README。


---

## 实现记录（第一版）

| 项 | 落地方式 |
| :--- | :--- |
| 主题 | `g_cfg.theme` 0/1/2；`ComputeLayout()` 按主题换 `g_winW`（480 / 526 dp）与卡片内边距，保证文字列宽与纯文字版一致（不会新出现省略号） |
| 调色板 | `MakePalette(darkCard, alpha)`，`OklchColor()` 把 oklch 直接算成 sRGB；深色卡片由壁纸亮度决定 |
| 壁纸模糊 | `RefreshWallpaperCache()` + `DrawPanelCard()`：缓存 1280 宽壁纸副本，卡片区域降采样 1/7 再放大，只在壁纸变化时重算 |
| 主色 | `UpdateTheme()` 在采样的 100 个点里按 OKLCH 色度加权求圆均值 hue（`SrgbToOklchHue`） |
| 设置窗口 | 独立 `WS_EX_LAYERED` 自绘窗口 + 命中区表；`WM_DPICHANGED` 重排；文本复用 `DrawTextGdiLine`（同一套 MiSans 与灰度抗锯齿→alpha 管线） |
| 缩略图 | `DrawThemeThumb()` 程序化绘制，跟随当前主色，零图片资源 |
| 配置 | `HSpec.ini`（`GetPrivateProfileInt` / `WritePrivateProfileString`），程序目录优先，不可写退到 `%APPDATA%\HSpec` |
| 调试 | `--dump <png>`、`--dump-settings <png> [--page N]`、`--theme N`、`--settings` |

样例（本次实施后实测）：
- `--page 0` 外观三页、`--page 1` 显示内容、`--page 2` 关于；
- 面板 `--theme 2` 在 175% DPI 下输出 921×943（=526dp），文字列不截断。

### 样式约定（沿用主题的 oklch 令牌）

| token | 浅色 | 深色 | 用在哪 |
| :--- | :--- | :--- | :--- |
| primary | `oklch(.7 .14 h)` | `oklch(.75 .14 h)` | 强调条、开关、选中环、滑块 |
| btn-regular-bg | `oklch(.95 .025 h)` | `oklch(.33 .035 h)` | 图标 chip、侧栏选中底 |
| btn-content | `oklch(.55 .12 h)` | `oklch(.75 .10 h)` | chip 里的图标线条 |
| deep-text | `oklch(.25 .02 h)` | `oklch(.88 .01 h)` | 标题 |
| sub-text | `oklch(.42 .02 h)` | `oklch(.70 .015 h)` | 明细（**不要更浅**，见下方 alpha 反推约束） |
| card-bg | `white` | `oklch(.23 .015 h)` | 卡片底 |

### 字体

`tools/subset_font.py` 重新生成的子集是「旧 861 字 + 47 个新增界面字」，**没有丢字**：

```
old 861  new 908   added 47: ©·…、与个从仓依兜册再决变圆套它层嵌库掉染桌渲物现画界的目磁种纯纸表观许赖跟近这透随零（），
removed 0
```

新增的这 47 个正是「外观 / 纯文字 / 跟随壁纸取色 / 卡片不透明度 / 圆角 / 渲染层 / 桌面」这些设置页文案用到的字，补齐后不会再整行回退雅黑。

### 加粗：全文不用 Bold 字重

界面里**没有任何粗体**。两条路都走过、都不采用：

1. **GDI 合成加粗**（Medium 子集 + `FW_BOLD`）：笔画被人工描粗，再经过 `DrawTextGdiLine` 的亮度→alpha 反推，边缘发灰发虚——用户原话「发虚」「就不能不加粗吗」。
2. **内嵌真 MiSans Bold 面**：渲染是锐利了，但用户明确不要 Bold 字重，已整体移除（`git log` 里有 `Embed real MiSans Bold face for bold text` 及其回退，需要时可参考）。

层级现在只靠**字号 + 颜色**区分（标题 `pal.title` / 明细 `pal.detail`，页标题 17px、节标题 13px、正文 11.5~12.5px）。`tools/subset_font.py` 的 docstring 里也写明了这条约束：新增文本一律 `FW_NORMAL`。
