# GitHub 工作流程

## 分支策略

```
master (稳定版本) ────────────────────────────────────────────┐
    │                                                          │
    ├── feature/daemon-mode (守护进程模式)                      │
    │     │                                                      │
    │     ├── 开发/测试/验证                                     │
    │     │                                                      │
    │     └── 合并回 master                                      │
    │                                                          │
    └── bugfix/xxx (其他修复分支)                                │
          │                                                      │
          └── 修复 → 测试 → 合并                                 │
                                                              │
        合并后删除分支                                           ▼
                                                              │
                                                        master (更新版本)
```

## 常用命令

### 1. 创建新分支

```bash
# 从 master 创建功能分支
git checkout -b feature/daemon-mode

# 或者从特定 tag 创建
git checkout -b feature/daemon-mode v1.6
```

### 2. 提交代码

```bash
# 查看修改
git status

# 添加文件
git add window-toggle.c

# 提交
git commit -m "描述"

# 推送到 GitHub
git push origin feature/daemon-mode
```

### 3. 切换分支

```bash
# 切换回 master
git checkout master

# 切换到功能分支
git checkout feature/daemon-mode
```

### 4. 合并分支

```bash
# 1. 切换到 master
git checkout master

# 2. 拉取最新
git pull origin master

# 3. 合并
git merge feature/daemon-mode

# 4. 推送
git push origin master

# 5. 删除本地分支
git branch -d feature/daemon-mode
```

### 5. 删除远程分支

```bash
git push origin --delete feature/daemon-mode
```

## Tag 管理

```bash
# 创建 tag
git tag -a v1.7 -m "v1.7 - 添加守护进程模式"

# 推送 tag
git push origin v1.7

# 推送所有 tag
git push origin --tags

# 删除本地 tag
git tag -d v1.7

# 删除远程 tag
git push origin --delete v1.7
```

## 开发流程

### 场景：实现守护进程模式

```bash
# 1. 确保 master 最新
git checkout master
git pull origin master

# 2. 创建分支
git checkout -b feature/daemon-mode

# 3. 开发代码
# ... 修改 window-toggle.c ...

# 4. 本地测试
gcc -Wall -O2 -I. -o window-toggle window-toggle.c config.c window-manager.c -lX11 -lxkbcommon
./window-toggle --test

# 5. 提交
git add window-toggle.c
git commit -m "Add daemon mode - persistent X connection"

# 6. 推送到 GitHub
git push origin feature/daemon-mode

# 7. 在 GitHub 上创建 Pull Request
# 8. Code Review 通过后合并

# 9. 切换回 master 并拉取
git checkout master
git pull origin master

# 10. 创建新 tag
git tag -a v1.7 -m "v1.7 - 添加守护进程模式"
git push origin v1.7
```

## 常用 Git 操作

### 查看状态

```bash
git status          # 查看当前状态
git log --oneline   # 查看提交历史
git branch -a       # 查看所有分支
git tag             # 查看所有 tag
```

### 撤销操作

```bash
# 撤销工作区修改
git checkout -- window-toggle.c

# 撤销暂存
git reset HEAD window-toggle.c

# 撤销提交（保留修改）
git reset --soft HEAD~1

# 完全撤销（危险）
git reset --hard HEAD~1
```

### 暂存修改

```bash
# 暂存当前修改
git stash

# 恢复暂存
git stash pop

# 查看暂存列表
git stash list
```

## 分支命名规范

| 前缀 | 用途 | 示例 |
|------|------|------|
| feature/ | 新功能 | feature/daemon-mode |
| bugfix/ | bug 修复 | bugfix/clean-fix |
| hotfix/ | 紧急修复 | hotfix/crash |
| refactor/ | 重构 | refactor/x-connection |

## 最佳实践

1. **频繁提交** - 每次小改动都提交
2. **描述清晰** - commit message 说明做了什么
3. **先测后合** - 合并前确保测试通过
4. **保持同步** - 经常拉取 master 最新代码
5. **及时清理** - 合并后删除不需要的分支

---

# X 连接限制问题解决方案

## 问题背景

X11 服务器对每个客户端连接有限制（约 500-1000 个）。Chrome 使用多进程架构（每个标签页、扩展都是独立进程），182 个 Chrome 进程耗尽了 X 连接，导致快捷键注册失败。

## 三个解决方案

| 方案 | 技术路线 | 代码改动量 | 复杂度 |
|------|----------|------------|--------|
| 方案 1 | 守护进程模式 | 中等 | 低 |
| 方案 2 | XCB 替代 Xlib | 大 | 中 |
| 方案 3 | DBus 通信 | 大 | 高 |

---

# 方案 1：守护进程模式

## 核心思路

创建常驻守护进程，维护单一持久 X 连接，所有快捷键操作通过 IPC 通知守护进程处理。

## 分支设计

```
master ────────────────────────────────────────────────────────
    │
    └── feature/daemon-mode (守护进程模式)
          │
          ├── 实现步骤:
          │   1. 创建 daemon.c / daemon.h
          │   2. 实现 daemon_start() - 启动守护进程
          │   3. 实现 daemon_run() - 处理 IPC 请求
          │   4. 修改 window-toggle.c 支持 --start 和 IPC 模式
          │   5. 添加单一持久 X 连接管理
          │   6. 测试：Chrome 运行期间快捷键是否有效
          │
          └── 合并回 master
```

## GitHub 操作流程

```bash
# 1. 创建分支
git checkout -b feature/daemon-mode

# 2. 开发 - 新增文件
git add daemon.c daemon.h

# 3. 开发 - 修改文件
git add window-toggle.c

# 4. 提交
git commit -m "feat: Add daemon mode with persistent X connection"

# 5. 推送
git push origin feature/daemon-mode

# 6. 创建 PR 或直接合并
git checkout master
git merge feature/daemon-mode
git tag -a v1.8 -m "v1.8 - 添加守护进程模式"
git push origin v1.8

# 7. 删除分支
git branch -d feature/daemon-mode
git push origin --delete feature/daemon-mode
```

---

# 方案 2：XCB 替代 Xlib

## 核心思路

使用更轻量的 XCB 库替代 Xlib，减少每个连接的内存占用，从而支持更多连接。

## 分支设计

```
master ────────────────────────────────────────────────────────
    │
    └── refactor/xcb-migration (XCB 重构)
          │
          ├── 阶段 1: 核心替换
          │     ├── 替换 XOpenDisplay → xcb_connect
          │     ├── 替换 XCloseDisplay → xcb_disconnect
          │     └── 替换 XGrabKeyboard → xcb_grab_key
          │
          ├── 阶段 2: API 适配
          │     ├── xcb_intern_atom() 替代 XInternAtom
          │     ├── xcb_send_event() 替代 XSendEvent
          │     └── xcb_flush() 替代 XFlush
          │
          ├── 阶段 3: 测试验证
          │     └── 确保所有功能与 Xlib 版本一致
          │
          └── 合并回 master
```

## GitHub 操作流程

```bash
# 1. 创建重构分支
git checkout -b refactor/xcb-migration

# 2. 阶段 1: 核心替换
git add xcb-core.c xcb-core.h
git commit -m "refactor: Add XCB core wrapper layer"

# 3. 阶段 2: API 适配
git add window-manager-xcb.c
git commit -m "refactor: Port window-manager.c to XCB"

# 4. 阶段 3: 测试验证
git add test-xcb.c
git commit -m "test: Add XCB compatibility tests"

# 5. 推送并合并
git push origin refactor/xcb-migration
git checkout master
git merge refactor/xcb-migration
git tag -a v2.0 -m "v2.0 - XCB 重构版本"
git push origin v2.0

# 6. 删除分支
git branch -d refactor/xcb-migration
git push origin --delete refactor/xcb-migration
```

---

# 方案 3：DBus 通信

## 核心思路

不依赖 X 连接，通过 DBus 进程间通信机制处理窗口操作，完全绕过 X 连接限制。

## 分支设计

```
master ────────────────────────────────────────────────────────
    │
    └── feature/dbus (DBus 通信)
          │
          ├── 实现 DBus 接口定义
          ├── 实现 Service 端
          ├── 实现客户端调用
          └── 测试完整流程
```

## GitHub 操作流程

```bash
# 1. 创建分支
git checkout -b feature/dbus

# 2. 开发 DBus 服务和客户端
git add window-toggle-service.c window-toggle-dbus.c
git commit -m "feat: Add DBus communication support"

# 3. 推送并合并
git push origin feature/dbus
git checkout master
git merge feature/dbus
git tag -a v2.0 -m "v2.0 - DBus 通信版本"
git push origin v2.0

# 4. 删除分支
git branch -d feature/dbus
git push origin --delete feature/dbus
```

---

## 方案对比总结

| 方面 | 方案 1 守护进程 | 方案 2 XCB | 方案 3 DBus |
|------|-----------------|------------|-------------|
| **代码改动量** | 中等 (新增 daemon.c) | 大 (重写 X 调用) | 大 (新增服务+接口) |
| **架构复杂度** | 低 | 中 | 高 |
| **依赖** | 只需 libX11 | 需 libxcb | 需 libdbus |
| **向后兼容** | 完全兼容 | 需要保留 Xlib fallback | 不兼容，需重写客户端 |
| **维护成本** | 低 | 中 | 高 |
| **推荐度** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |

## 推荐开发顺序

```
1. 先实现方案 1 (守护进程模式) - 最快解决问题
2. 如果方案 1 不足，再考虑方案 2 (XCB) - 可选优化
3. 方案 3 (DBus) - 适合长期架构重构
```
