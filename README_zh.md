# window-toggle

一个 GNOME 键盘快捷键工具，让你一键显示或隐藏任意窗口。

## 这是什么？

想要一键隐藏干扰你的窗口？**window-toggle** 为每个窗口分配一个快捷键：

- 按一下快捷键 → 隐藏窗口
- 再按一下 → 窗口重新出现
- 类似于 macOS 的"隐藏应用"，但针对单个窗口

## 使用场景

```
你打开了 3 个终端，分别放在不同工作区。
你给终端 1 分配 Ctrl+Alt+F1，F2 给终端 2，F3 给终端 3。

现在你可以：
- 按 Ctrl+Alt+F1 → 终端 1 出现（刚才隐藏了）
- 按 Ctrl+Alt+F1 → 终端 1 最小化（刚才可见）
- 在 GNOME 任意位置都能用
```

## 安装

### 编译并安装

```bash
# 设置构建目录（只需一次）
meson setup build

# 编译项目
meson compile -C build

# 安装到系统（需要 sudo）
sudo meson install -C build
```

安装后即可使用：
```bash
window-toggle --configure
```

### 修改代码后重新安装

如果你修改了源代码并想重新安装：

```bash
# 编译并安装
meson compile -C build && sudo meson install -C build
```

或分两步：
```bash
meson compile -C build
sudo meson install -C build
```

## 快速开始

### 1. 配置一个窗口

```bash
window-toggle --configure
```

程序会：
1. 让你按下快捷键（如 `Ctrl+Alt+F1`）
2. 显示打开的窗口列表，选择一个
3. 自动在 GNOME 中注册快捷键

### 2. 使用

在 GNOME 中按 `Ctrl+Alt+F1`：
- **窗口隐藏？** → 窗口出现在屏幕
- **窗口可见？** → 窗口最小化

### 3. 查看快捷键

```bash
window-toggle --show
```

## 命令

| 命令 | 说明 |
|------|------|
| `--configure` | 为窗口添加新快捷键 |
| `--run` | 切换窗口（GNOME 快捷键调用） |
| `--show` | 查看所有已配置的快捷键 |
| `--clean` | 删除 slot 和 viewer 自动注册的快捷键（不动 --bind-app 注册的）|
| `--start` | 清理并启动守护进程 |
| `--stop` | 停止守护进程 |
| `--status` | 查看守护进程是否在运行 |
| `--version` | 输出版本和本次主要更新 |
| `--key KEY` | 指定按下的键（守护进程回调命令使用） |
| `--config PATH` | 使用非默认的配置文件路径 |

## 支持的快捷键修饰符

从 v1.8 开始，同一个 Fx 键（例如 F1）可以绑定 5 种不同的修饰符组合，
每种切换一个独立窗口：

- **裸 Fx** —— 例如 `F1`
- **Ctrl+Fx** —— 例如 `Ctrl+F1`
- **Ctrl+Alt+Fx** —— 例如 `Ctrl+Alt+F1`
- **Ctrl+Shift+Fx** —— 例如 `Ctrl+Shift+F1`
- **Super+Fx** —— 例如 `Super+F1`

每个绑定都跑一次 `window-toggle --configure`，工具会写入不同的 dconf 槽位，
运行时 `--key` 参数携带修饰符参与查找：按 `Ctrl+F1` 切换 Ctrl+F1 的绑定，
按裸 `F1` 切换裸 F1 的绑定。

## 工作原理

- 使用 X11 `_NET_WM_STATE_HIDDEN` 检测窗口状态
- 发送 `_NET_ACTIVE_WINDOW` 显示窗口，`XIconifyWindow` 最小化
- 配置保存在 `/tmp/window-toggle-config.json`
- 通过 GNOME 的 `dconf` 注册快捷键

## 环境要求

- GNOME 桌面
- libX11
- xkbcommon
- GCC
- meson, ninja（编译用）

## 测试环境

- Ubuntu 24.04 with GNOME

## 应用绑定（v1.9+）

把一个修饰符快捷键绑定到「启动某个应用 + 切换这个窗口」：

```bash
# 把 Ctrl+F12 绑到 nautilus,首次启动的窗口被锚定
window-toggle --bind-app Ctrl+F12 nautilus org.gnome.Nautilus

# 查看已绑定的应用
window-toggle --show-app

# 删除绑定
window-toggle --unbind-app Ctrl+F12
```

按下快捷键时的行为：

- 锚定窗口仍在 —— 切换（最小化 / 激活），与普通 slot 完全一致。
- 锚定窗口已退出 —— `fork+execlp <cmd>` 启动命令，轮询 `WM_CLASS` 匹配的窗口，把第一个匹配项作为新锚定写入配置。新启动的窗口默认就是可见的，所以不会再 toggle。
- 多实例：用户手动开第二个相同应用时，绑定**不会**漂移到新窗口，锚定始终是首次启动那一个。

存储：仍在 `/tmp/window-toggle-config.json` 内，以 `### app_bindings ###` 行为段边界。slot 解析在该行停止，互不干扰。`--clean` 现在会清掉 `meson install` 自动注册的 viewer 快捷键（Pause / Scroll_Lock / Print）和 dconf 数组里残留的空 slot 路径。**不会**动 `--bind-app` 注册的快捷键和 `~/.config/window-toggle/bindings.json` 文件——那些由 `--unbind-app` 单独管理。

## 更新日志

### v1.9.7 (2026-07-12)
- 改动：配置文件里每个 app binding 改成一行一存。之前每个 (modifier, key) 写一个 6 行 block, 一个 app 三个 modifier 占 18 行, 五个 app 一共 91 行。现在把三个 modifier 名拼到同一行的 modifiers 字段里, 用 "|" 分隔, 一个 (app, Fx) 一个 6 行 block, 五个 app 压到 30 行左右。文件格式明确: 空字符串 = 裸 Fx, 单个名字 = 一个 modifier, 名字 | 名字 | 名字 = 多个 modifier。内存里仍按老规矩一个 (modifiers, key) 一个 AppBinding —— load 时把 "|" 串拆回多行, serialize 时再合成一行, 所以 --show-app / --bind-app / --unbind-app 行为完全不变。旧格式 (一个 modifier 一行) 继续能读。
- 修复: 合并重复行 / 删除某一行 时原来直接做结构体赋值, 几个 AppBinding 共用同一个 char* 指针, 后面 free 时二次释放, --unbind-app 一跑就把整个文件压成一行, 改不回来了。现在按字段逐个复制, 被吞掉的行的指针先清空再让 free(NULL), 调用方释放时是空操作。

### v1.9.6 (2026-07-11)
- 修复: --clean 现在也清 viewer 自动注册的快捷键 (Pause / Scroll_Lock / Print) 和 dconf 数组里残留的空 slot 路径。之前 --clean 只清 slot binding, viewer 那几条粘在上面像没清干净, dconf 数组也留着空 slot 占位。
- 修复: --clean 不再动 --bind-app 注册的快捷键。那些行落在 ~/.config/window-toggle/bindings.json, 名字是 "window-toggle-app", 由 --unbind-app 单独清, 不归 --clean 管。之前 --clean 顺带把 app binding 一起清了, 用户得重新注册。

### v1.9.5 (2026-07-11)
- 改动：`--bind-app <Fx> <cmd> <wm_class>` 现在一次注册三条 dconf 快捷键 —— `Ctrl+Fx`、`Super+Fx`、`Alt+Fx`，不再只是一条。按这三条里任意一条都 toggle 同一个窗口，不用在创建时指定修饰符。
- 改动：带显式修饰符的（像 `Ctrl+Shift+F7`）还是只注册一条，老脚本不受影响。
- 改动：`--show-app` 和 viewer 弹窗把同一个 app+Fx 的几条合并成一行显示。三件套齐全写 `Ctrl(S+A)+Fx`（Ctrl+Super+Alt），两条写 `Ctrl+S+Fx` 这种，一条时照旧写 `Ctrl+Fx`。viewer 弹窗里这一行的状态圆点按「已启动 > 已隐藏 > 未启动」取最有用那条，避免三条里活着一条也显示成「全死了」。
- 修复：启动新窗口时把同 cmd+wm_class 的另外两条 binding 的 anchor 也一起覆盖到新窗口 ID。现在三条始终指向同一个 XID，老 anchor 死了也不会留下脏数据。

### v1.9.4 (2026-07-10)
- 修复：按快捷键时区分「窗口在最顶」和「窗口 visible 但被压在别的窗口底下」两种情况。之前这两种都走最小化，所以按下去如果窗口本来就被压着，用户什么都看不到 —— mutter 只告诉窗口是不是 `_NET_WM_STATE_HIDDEN`，不告诉 z-order。现在先查一下窗口是不是普通窗口里最顶的那个：在最顶就最小化（老行为不变）；被压着就只把它抢到最前（raise），让用户看到窗口跳出来。F1/F3/F6（slot）和 Ctrl+F10/F11/F12（app binding）都按这个改。
- 修复：把窗口抢到最前时改用另一种消息（`_NET_ACTIVE_WINDOW` 的 `source=2` 也就是「桌面切换器」那种）。mutter 对普通 app 的抢焦点请求只给焦点不动 stacking；对桌面切换器那种才同时抢焦点+提到最前。改完之后按下去确实能看到窗口跳出来。

### v1.9.3 (2026-07-06)
- 修复：viewer 弹窗现在分「可见 / 已隐藏 / 已失效」三种状态显示。之前「被最小化」和「anchor 彻底死了」显示成同一种，让人按 Ctrl+Fx 把窗口关掉之后以为按键没起作用。
  - 三态：started（绿色圆点 + 不透明）、hidden（灰色圆点 + 40% 透明）、not-started（橙色圆点 + 25% 透明）
  - 弹窗打开期间每秒轮询一次状态，按一下 Ctrl+Fx 关掉窗口会立刻在弹窗里反映新状态，不用关掉再开
- 修复：`--clean` 现在也清掉 `meson install` 自动注册的 viewer binding（Pause / Scroll_Lock / Print）和 dconf 数组残留的空 slot。`--bind-app` 注册的快捷键不动——那些由 `--unbind-app` 单独管理。

### v1.9.1 (2026-07-03)
- 修复: dconf action 的参数顺序改为 `--key X --run-app`,使 dconf 回调能真正触发
- 修复: app binding 配置改为存到 `~/.config/window-toggle/bindings.json`(XDG 路径),重启后保留
- chore: 首次写入时自动创建父目录

### v1.9 (2026-07-03)
- 新增 `--bind-app <key> <cmd> <wm_class>`：把快捷键绑定到「启动应用 + 切换窗口」
- 新增 `--unbind-app <key>`、`--show-app`、`--run-app`（dconf 回调）
- 锚定语义：首次启动的窗口被记住，永不漂移
- 配置：应用绑定存储在 `### app_bindings ###` 段，与 slot 数据隔离
- `--clean` 不动 app binding；只想删单条时用 `--unbind-app <Ctrl+Fx>`
- 为失效锚定的 XID 检查增加静默 XErrorHandler

### v1.8 (2026-06-02)
- 新增 `--version` 命令，输出中英双语的版本说明
- 修饰符矩阵的核心改动见 v1.7

### v1.7 (2026-06-02)
- **5 种修饰符** 现在可以共存于同一个 Fx 键
  （裸 Fx、Ctrl+Fx、Ctrl+Alt+Fx、Ctrl+Shift+Fx、Super+Fx），
  每种切换一个独立窗口
- 新增常驻 X 连接的守护进程模式（`--start` / `--stop` / `--status`），
  解决 Chrome 多个窗口导致 X 连接耗尽的问题。
  设计文档见 `doc/IMPLEMENT_DAEMON_MODE.md`
- dconf 命令写入完整快捷键字符串（如 `--key Ctrl+F1`），
  运行时查找可还原修饰符
- 修复 `save_shortcut_mapping` 去重状态机的 bug（`modifiers:` 行尾
  的换行符被 `strcspn` 抹掉，导致配置文件损坏）
- 配置文件行缓冲从 512 字节扩大到 4096 字节，
  避免长 `window_title` 被截断

### v1.6 (2026-05-03)
- `--show` 输出中显示 `window_class`

### v1.4-v1.5 (2026)
- 守护进程模式基础设施及若干修复，完整提交列表见
  `doc/GITHUB_STATUS.md`

### v1.3 (2026-02-22)
- 修复保存快捷键时配置文件格式损坏问题
- 修复换行符丢失问题
- 支持多个独立快捷键（F1、F2 等）
- 支持快捷键覆盖（重新配置同一按键到不同窗口）
- 添加 slot_id 到配置文件便于跟踪

### v1.1 (2026-01-11)
- 添加重复快捷键覆盖支持

### v1.0 (2026-01-08)
- 初始版本发布
- 基本显示/隐藏窗口功能

## 许可证

MIT
