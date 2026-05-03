# 方案 1：守护进程模式 - 详细实现方案

## 目标

创建一个常驻守护进程，维护单一持久 X 连接，所有快捷键操作通过 IPC 通知守护进程处理，避免 X 连接耗尽。

## 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                      守护进程 (Daemon)                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │
│  │ X 连接管理   │  │  IPC 处理    │  │  窗口状态管理    │   │
│  │ (持久连接)   │  │  (Socket)   │  │                 │   │
│  └─────────────┘  └─────────────┘  └─────────────────┘   │
│         │                                      │            │
│         └──────────────┬─────────────────────┘            │
│                        │                                    │
│                   单一 X 连接                                │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │
┌─────────────────────────────────────────────────────────────┐
│                      客户端 (Client)                          │
│                                                              │
│   window-toggle --configure  →  配置快捷键 (守护进程处理)      │
│   window-toggle --run        →  切换窗口 (发 IPC 请求)          │
│   window-toggle --show       →  显示配置                      │
│   window-toggle --start      →  启动守护进程                   │
│   window-toggle --stop      →  停止守护进程                   │
└─────────────────────────────────────────────────────────────┘
```

## 文件结构

```
window-toggle/
├── window-toggle.c      # 主程序 + CLI 解析
├── config.c             # 配置文件读写
├── config.h
├── window-manager.c     # X11 窗口操作
├── window-manager.h
├── daemon.c             # 新增：守护进程核心
├── daemon.h             # 新增：守护进程头文件
└── ipc.c                # 新增：IPC 通信层
└── ipc.h
```

## 实现步骤

### 步骤 1：创建 daemon.h - 定义守护进程接口

**目的**：定义守护进程相关的结构体、常量和函数声明

**内容**：
- 守护进程状态结构体 `DaemonState`
- PID 文件路径 `/tmp/window-toggle-daemon.pid`
- Unix socket 路径 `/tmp/window-toggle-daemon.sock`
- 函数声明：
  - `daemon_start()` - 启动守护进程
  - `daemon_stop()` - 停止守护进程
  - `daemon_is_running()` - 检查是否运行中
  - `daemon_send_request()` - 发送 IPC 请求

### 步骤 2：创建 ipc.h / ipc.c - IPC 通信层

**目的**：封装进程间通信机制

**通信方式**：Unix Domain Socket（可靠、本地通信）

**内容**：

**ipc.h 定义**：
- 请求类型枚举：
  - `IPC_TOGGLE_WINDOW` - 切换窗口
  - `IPC_GET_WINDOW_STATE` - 获取窗口状态
  - `IPC_SCAN_WINDOWS` - 扫描窗口
- 请求结构体 `IPCRequest`
- 响应结构体 `IPCResponse`
- 函数声明

**ipc.c 实现**：
- `ipc_create_socket()` - 创建 Unix socket
- `ipc_connect()` - 客户端连接守护进程
- `ipc_send_request()` - 发送请求
- `ipc_recv_response()` - 接收响应
- `ipc_close()` - 关闭连接

### 步骤 3：创建 daemon.c - 守护进程核心

**目的**：实现守护进程的主循环和 X 连接管理

**daemon.c 内容**：

**3.1 初始化阶段**：
- 检查是否已有实例运行
- fork() 创建后台进程
- 写入 PID 文件
- 建立持久 X 连接（只建一次）
- 创建 Unix socket 监听

**3.2 主循环**：
- 使用 select() 或 epoll() 监听 socket
- 接收客户端请求
- 处理请求（调用 window-manager.c）
- 发送响应

**3.3 信号处理**：
- SIGTERM / SIGINT - 优雅退出
- SIGCHLD - 处理子进程
- SIGHUP - 重新读取配置（可选）

**3.4 清理阶段**：
- 关闭 X 连接
- 删除 PID 文件
- 关闭 socket

### 步骤 4：修改 window-toggle.c - 添加新模式

**目的**：集成守护进程支持，修改 CLI 解析

**修改内容**：

**4.1 新增命令行参数**：
- `--start` - 启动守护进程
- `--stop` - 停止守护进程
- `--status` - 查看守护进程状态

**4.2 新增模式处理函数**：
- `start_mode()` - 调用 daemon_start()
- `stop_mode()` - 调用 daemon_stop()

**4.3 修改 run_mode()**：
- 原逻辑：直接操作 X 连接
- 新逻辑：连接守护进程，发送 IPC 请求

**4.4 修改 configure_mode()**：
- 如果守护进程未运行，先启动
- 配置操作仍由守护进程处理

### 步骤 5：修改 window-manager.c - 适配守护进程

**目的**：使窗口操作可以被守护进程调用

**可能需要调整**：
- `toggle_window()` 函数保持不变（守护进程调用）
- `scan_all_windows()` 保持不变
- 确保所有 X 操作使用传入的 Display 指针

### 步骤 6：修改 config.c - 添加进程相关配置

**目的**：支持守护进程的配置管理

**可能需要添加**：
- 守护进程状态文件路径
- 配置变更通知机制（可选）

## IPC 协议设计

### 请求格式

```
┌──────────┬──────────┬──────────────┐
│  type    │  size    │   data       │
│ (4 bytes)│ (4 bytes)│  (variable)  │
└──────────┴──────────┴──────────────┘
```

### type 类型

| 值 | 类型 | 说明 |
|----|------|------|
| 1 | IPC_TOGGLE_WINDOW | 切换窗口 |
| 2 | IPC_GET_WINDOW_STATE | 获取窗口状态 |
| 3 | IPC_SCAN_WINDOWS | 扫描所有窗口 |

### IPC_TOGGLE_WINDOW 请求

```
type: 1
size: 8 + window_id_size
data: window_id (8 bytes, uint64_t)
```

### IPC_TOGGLE_WINDOW 响应

```
type: 1
size: 4
data: result (4 bytes, int32_t)  // 0=成功, -1=失败
```

## 客户端调用流程

### 启动守护进程

```
用户执行 window-toggle --start
    │
    ▼
检查 /tmp/window-toggle-daemon.pid
    │
    ├── 存在 → 检查进程是否存活
    │         │
    │         ├── 存活 → 输出"守护进程已在运行"
    │         │
    │         └── 不存活 → 删除 PID 文件，启动守护进程
    │
    └── 不存在 → 启动守护进程
              │
              ▼
          daemon_start()
              │
              ├── fork()
              │
              ├── 子进程：
              │   ├── 建立 X 连接
              │   ├── 创建 socket 监听
              │   ├── 写入 PID 文件
              │   └── 进入主循环
              │
              └── 父进程：
                  └── 输出"守护进程已启动"
```

### 切换窗口

```
用户执行 window-toggle --run
    │
    ▼
run_mode_with_path()
    │
    ▼
检查守护进程是否运行
    │
    ├── 未运行 → 启动守护进程
    │
    └── 已运行 → 继续
              │
              ▼
          ipc_connect()
              │
              ▼
          发送 IPC_TOGGLE_WINDOW 请求
              │
              ▼
          接收响应
              │
              ▼
          关闭连接，输出结果
```

## 配置管理

### PID 文件

路径：`/tmp/window-toggle-daemon.pid`

内容：守护进程 PID（纯文本）

用途：
- 检查守护进程是否运行
- 发送信号停止守护进程

### Socket 文件

路径：`/tmp/window-toggle-daemon.sock`

用途：客户端与守护进程的通信通道

### 配置变更同步

当 `--configure` 修改配置时：
- 直接修改配置文件 `/tmp/window-toggle-config.json`
- 守护进程定期检查配置文件变化
- 或者：通过 IPC 通知守护进程重新加载

## 错误处理

### 守护进程崩溃

- X 连接断开 → 守护进程退出
- 客户端检测连接失败 → 尝试重启守护进程

### 客户端超时

- 连接 socket 超时 → 报告错误
- IPC 请求超时 → 报告错误

### 端口/文件冲突

- socket 文件已存在 → 删除旧文件后创建
- PID 文件已存在 → 检查进程是否存活

## 测试计划

### 单元测试

1. **daemon.c 测试**：
   - PID 文件创建和删除
   - 守护进程启动和停止
   - 信号处理

2. **ipc.c 测试**：
   - socket 创建和销毁
   - 请求发送和接收
   - 多客户端并发

3. **integration 测试**：
   - 启动守护进程
   - 执行 --run
   - 验证窗口切换
   - 停止守护进程

### 压力测试

1. Chrome 运行期间（180+ X 连接）
2. 多次快速切换快捷键
3. 长时间运行稳定性

### 边界测试

1. 守护进程未启动时直接 --run
2. 重复启动守护进程
3. 强制杀死守护进程后重启

## 用户体验

### 首次使用

```bash
window-toggle --configure
# 自动启动守护进程（如果未运行）
# 然后引导用户配置快捷键
```

### 日常使用

```bash
# 按快捷键 → 自动通过守护进程切换窗口
Ctrl+Alt+F1

# 查看状态
window-toggle --status
# 输出: 守护进程运行中 (PID: 12345)

# 手动启动
window-toggle --start

# 手动停止
window-toggle --stop
```

### 与原流程兼容

- 原有的 `--configure`, `--show`, `--clean` 保持不变
- 守护进程在后台自动处理
- 用户无需感知守护进程存在

## 实现 Checklist

- [ ] 创建 daemon.h
- [ ] 创建 ipc.h / ipc.c
- [ ] 创建 daemon.c
- [ ] 修改 window-toggle.c 添加 --start, --stop, --status
- [ ] 修改 run_mode 使用 IPC
- [ ] 修改 configure_mode 使用 IPC
- [ ] 添加单元测试
- [ ] 压力测试（Chrome 运行期间）
- [ ] 更新文档

## 时间估算

| 步骤 | 工作量 |
|------|--------|
| daemon.h / ipc.h/ipc.c | 1-2 天 |
| daemon.c | 1-2 天 |
| 修改 window-toggle.c | 0.5-1 天 |
| 测试 | 1 天 |
| 文档更新 | 0.5 天 |
| **总计** | **4-6 天** |
