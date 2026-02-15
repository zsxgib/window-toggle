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

```bash
# 编译并安装到 /usr/local/bin/
meson setup build
meson install -C build

# 之后可以在任意目录运行
window-toggle --configure
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
| `--clean` | 删除所有快捷键 |
| `--start` | 清理并重新开始 |

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

## 许可证

MIT
