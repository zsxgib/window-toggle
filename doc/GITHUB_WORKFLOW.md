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
