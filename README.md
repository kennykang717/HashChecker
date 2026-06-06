# HashCheck

一个轻量级 Windows 原生哈希校验工具，使用 WIN32 API 构建。支持拖放文件/URL、MD5/SHA256 校验、文件下载与哈希比对。采用 Deepseek + OpenCode 开发。

## 功能

- **拖放校验**：将文件或下载链接拖入悬浮窗，自动计算 MD5 和 SHA256
- **哈希比对**：粘贴目标哈希值，快速验证文件完整性
- **右键菜单**：在文件上右键 →「检查哈希」直接调出验证窗口
- **URL 下载**：从浏览器拖入下载链接，后台下载完成后自动校验
- **自定义缓存**：可在设置中修改下载文件的保存位置
- **上下文菜单**：一键注册/卸载右键菜单项
- **命令行支持**：`hashcheck.exe <文件路径>` 直接验证

## 下载

从 [Releases](https://github.com/kennykang717/HashCheck/releases) 页面下载最新版本的 `hashcheck.exe`，单文件运行，无需安装。

## 使用方法

### 悬浮窗模式

运行 `hashcheck.exe`（不带参数）启动悬浮窗：

1. 从文件管理器拖拽文件到悬浮窗，弹出验证对话框
2. 从浏览器拖拽下载链接到悬浮窗，自动下载后弹出验证对话框
3. 点击 ⚙ 打开设置

### 命令行模式

```cmd
hashcheck.exe "C:\path\to\file"
```

直接弹出该文件的哈希验证对话框。

### 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+Shift+Q | 退出程序 |

## 设置

点击悬浮窗的 ⚙ 齿轮按钮打开设置对话框：

- **添加到右键菜单**：在文件右键菜单中添加「检查哈希」选项
- **启用日志**：将运行日志写入 `hashcheck.log`
- **缓存位置**：修改下载文件的保存目录（默认为 `%LOCALAPPDATA%\cache\HashCheck`）

## 构建

### 依赖

- MinGW-w64（gcc + windres）

### 编译

```cmd
build.bat
```

或

```powershell
build.ps1
```

### 预览

<img src="https://raw.githubusercontent.com/kennykang717/HashChecker/main/res/settings.jpg" style="zoom:50%;" />

<img src="https://raw.githubusercontent.com/kennykang717/HashChecker/main/res/check.jpg" alt="检查页" style="zoom:50%;" />

### 许可

本项目基于 MIT 许可证开源。
