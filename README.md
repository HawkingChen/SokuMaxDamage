# SokuMaxDamage · 什么最痛

面向《东方非想天则 / 東方非想天則》(TH12.3) **v1.10a** 的**本地只读伤害 HUD MOD**。独立项目，不依赖 SokuMoba。

MOD 在游戏窗口下方绘制一个伴随面板，读取运行时状态做**参考性估算**：不修改伤害、不写游戏内存、不触碰网络包与 RNG，也不向游戏窗口注入任何操作。

> 当前状态：功能已成型，并已根据用户在 Steam Deck / Proton 上的实机截图完成多轮调整。联网对战、观战、回放等场景仍待真机验收，详见「验证状态与已知限制」。

![WhatHurtsMost HUD 面板实机截图](docs/hud-panel.png)

*实机截图，仅截取 MOD 面板部分：左侧「伤害影响 · 总修正」，中间「最痛必杀技」与「最痛记录」，底部为当前手牌，右下角是 CN / JP / EN / 收起控制区。*

---

## 面板内容

### 伤害影响 · 总修正

- 大号百分比 = 自身 `attackPower` × 对手 `defensePower` × 100%，使用连续热度渐变着色（100% 沿用原来的暖金发光）。
- 旁边灰色小号是**根性补正**；必杀技 / SC 的**类型倍率单独列出**，不混进大百分比。
- 首击增伤只在来源、连击状态、对手受身、攻击确认等条件全部满足时**保守计入**大百分比，并以「首击」标注；**整招伤害估算不会乘这个倍率**。
- 下方因素卡堆（制御棒、人偶、根性、CH、天气、反射等）**只解释当前生效因素，不把显示的倍率再乘一遍**。天气文字仅在天气有效、未封印且确实能影响伤害时出现。

### 招式与手牌

- 「最痛必杀技」只从必杀技中挑选，不把普通技算进来；「最痛记录」是本场暂定的最大伤害记录。
- 底部按**实际手牌顺序**显示最多 5 张 SC，双方视角使用同一固定排布，不添加游戏内选卡操作。
- 估算值带 `≈`：静态参考数据叠加运行时有效倍率，**不是逐帧伤害模拟**。

### 交互

- 右下角 2×2 控制区：`CN` / `JP` / `EN` 切换界面语言，`↑` 收起面板。
- 折叠后只剩一个小 `＋`，仍服从战斗场景与前台窗口判断，不会变成常驻置顶的桌面悬浮窗。
- 语言与折叠偏好保存在 `WhatHurtsMost.ini`，换一场对战不会重置。
- 窗口会恢复被游戏隐藏或裁剪的系统光标，点击在**鼠标按下**时生效且不抢键盘焦点；系统光标仍不可见时，面板会自己画一个箭头作为兜底。

### 本地 / 联网角色

普通 1v1 联网时只显示**本机控制的角色**：`VSCLIENT` 取 P1 / 左侧，`VSSERVER` 取 P2 / 右侧；对手只提供估算我方输出所需的防御侧修正。观战与回放不会把远端玩家当成自己。面板由独立 UI 线程维护，使用复制出的快照绘制，非置顶，失焦或非对战场景自动隐藏。

## 数据与记法

| 文件 | 内容 |
| --- | --- |
| `config/FrameData.txt` | 1,372 条 [Cleista](https://soku.cleista.cc/framedata) 静态参考记录，覆盖 20 名角色，逐条保留来源链接 |
| `config/MoveNames.tsv` | 1,602 条中 / 英 / 日名称，按角色、卡号、记法索引 |
| `config/MoveProfiles.txt`、`config/WikiMaxProfiles.txt` | 手工与 Wiki 最大伤害补充数据 |

- 语言切换按 `character / card / notation` 匹配，**不拿本地化显示文本当主键**；缺译回退正式名或记法，不暴露 `CARD#` 一类内部编号。
- 记法统一规范化：`j.2A → J2A`、`f.5A → F5A`。普通招式的名称若本身就是记法，只显示**一次全大写**的 `J2A`，不会出现 `j.2A · J2A`。
- 界面固定文案走三语言字典；资料名走 `MoveNames.tsv`。两处倍率显示共用同一个格式化函数，避免顶部与卡堆出现 0.01 的舍入差。

## 安装（Steam Deck / Proton）

1. 从 [Releases](https://github.com/HawkingChen/SokuMaxDamage/releases/tag/v0.4.0) 下载 `SokuMaxDamage-v0.4.0-win32.zip`，解压到任意目录。
2. 桌面模式打开 Konsole，进入解压目录执行：

   ```bash
   bash install-steamdeck.sh /run/media/deck/share/fxtz/th123
   ```

   脚本先整目录备份 `modules/WhatHurtsMost/` 到 `~/Backups/SokuMaxDamage/`，再只替换 6 个文件：`WhatHurtsMost.dll`、`FrameData.txt`、`MoveProfiles.txt`、`WikiMaxProfiles.txt`、`WhatHurtsMost.ini`、`MoveNames.tsv`。
3. **首次安装**还需在实际使用的 SWRSToys / SokuModLoader 模块配置里启用 DLL：

   ```ini
   WhatHurtsMost=modules/WhatHurtsMost/WhatHurtsMost.dll
   ```

4. 启动游戏，建议先用 VS Player 或练习模式确认面板显示正常、进出对战不卡死。

补充说明：

- 脚本会检查游戏 / 启动器是否仍在运行并要求先退出；在 Steam Deck 上也可以不退出，直接替换上述 6 个文件，**重启游戏后生效**（此时请自行按同样规则先备份）。
- 脚本**不会**删除或覆盖已有的 `icons/`、`art/`，也不改其它 MOD、启动器与 SWRSToys 配置。
- 发布包**不含卡牌 PNG 素材**（本地游戏资源不随公开仓库分发）。想看到完整卡图请保留此前安装好的 `icons/`；可选 `art/panel-bg.png` 作为自定义背景，没有时面板自绘夜景。缺图时绘制占位图标。
- 回滚：把脚本打印的备份目录内容复制回 `modules/WhatHurtsMost/` 即可。

## 配置

`WhatHurtsMost.ini`：

| 键 | 说明 |
| --- | --- |
| `X` / `Y` / `Width` / `Height` | 640×480 基准下的坐标与尺寸参数 |
| `Language` | `CN` / `JP` / `EN`，由右下控制区写入 |
| `Collapsed` | `1` 为折叠，由右下控制区写入 |
| `EnableExperimentalFrameProbe` | 是否显示带 `~` 的近似直接攻击帧数据 |

## 构建

需要 Windows、Visual Studio 2022 C++ 桌面工具、CMake、Git，并使用 **x86 / Win32** 目标：

```powershell
cmake -S . -B build -A Win32 -DWHM_BUILD_TESTS=ON
cmake --build build --config Release --target WhatHurtsMost whm_tests whm_effects_tests --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix staging
```

CMake 会拉取固定版本的 [SokuLib](https://github.com/SokuDev/SokuLib) 与 [ModTemplate](https://github.com/SokuDev/ModTemplate)。MSVC 必须启用 `/utf-8`（仓库内有中文宽字面量断言）。窗口由独立 UI 线程维护——游戏渲染线程没有标准 Win32 消息循环。

[GitHub Actions](https://github.com/HawkingChen/SokuMaxDamage/actions/workflows/build.yml) 每次推送都会执行 Win32 构建、`whm_tests` / `whm_effects_tests`、PE32 头校验、`SHA256SUMS.txt` 生成与 ZIP 打包。

## 验证状态与已知限制

**已验证**：Windows x86 编译 / 链接、可移植纯逻辑测试、Windows 效果快照测试、PE32 与发布包哈希校验；面板已按用户在 Steam Deck / Proton 上的实机截图完成多轮调整。

**仍未验证 / 已知限制**：

- 联网对战主客机、观战与回放的完整路径；在线对局仍需双方实测。
- 每个技能各等级的最大伤害覆盖不全，缺失值保持未知；天气与角色专属效果、首击字段归属、`attackPower` 是否已含该状态、天气封印字段都有待真机核实。
- 暂定的 HP 差值记录无法可靠归因延迟弹幕与所有多段招式；CH / 反射按条件单独标注，不乘进整招参考值。
- 角色专属 Buff/Debuff 图标不完整；无卡图的普通技使用绘制的占位图。
- 640 宽度下密集因素卡堆的可读性、三语言字形、折叠 `＋` 随窗口缩放的锚点、模式与前后台切换仍需在实机逐项确认。
- 所有倍率都是参考估算，不是逐帧伤害模拟。

## 代码结构

| 路径 | 作用 |
| --- | --- |
| `src/core/` | 可移植纯逻辑：招式表、Wiki 最大值、伤害估算、记录、配色、文案、首击判定 |
| `src/mod/` | SWRSToys 1.10a DLL、运行时快照采集、Win32/GDI 面板绘制与交互 |
| `config/` | 配置样例与带来源的静态数据 |
| `deploy/` | Steam Deck 安装脚本与说明 |
| `docs/` | README 使用的面板截图 |
| `tests/` | 可移植核心测试与 Windows 效果快照测试 |
| `.github/workflows/build.yml` | Windows x86 CI |

私有设计决策与交接记录另行维护；本公开仓库只放源码、测试与可公开的技术说明。

许可：MIT，见 `LICENSE`。
