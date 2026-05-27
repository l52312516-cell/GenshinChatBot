# 原神聊天机器人 - 派蒙助手

基于图像识别（EasyOCR）和智谱AI大模型的《原神》游戏内自动聊天机器人。支持自动回复、点歌排队、插件扩展、系统托盘等功能。

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)

## ✨ 功能特性

- 🎮 **全自动聊天**：实时监控游戏聊天框，自动回复玩家消息（需唤醒词“派蒙萌萌萌”或 / 指令）。
- 🤖 **AI 智能回复**：接入智谱AI `glm-4-flash` 模型，支持多轮对话记忆。
- 🎵 **点歌排队**：支持 `/点歌` (网易云) 和 `/k点歌` (酷狗)，自动排队、自动切歌。
- 🛡️ **黑名单管理**：可添加黑名单关键词，禁止播放指定歌曲。
- 🎨 **图形界面**：深色主题 GUI，支持系统托盘、插件管理、设置保存。
- 📦 **插件系统**：可动态加载插件，已提供点歌插件示例。

## 📸 截图

（你可以在这里放几张软件界面的截图）

## 🚀 快速开始

### 环境要求

- Windows 10/11（其他系统需自行测试）
- Python 3.10 或更高版本（如果使用 exe 版本则无需安装 Python）
- 原神游戏（窗口化模式，推荐 1920×1080）

### 方式一：使用预编译 EXE（推荐）

1. 从 [Releases](https://github.com/l52312516-cell/GenshinChatBot/release) 下载最新版的 `GenshinChatBot.zip`。
2. 解压到任意目录，双击 `GenshinChatBot.exe` 运行。
3. 首次运行会自动检测依赖（实际上 exe 已包含所有依赖，无需安装）。
4. 按照界面提示填写智谱AI API Key 和 LxMusic 路径。

### 方式二：从源码运行

1. 克隆仓库：
   ```bash
   git clone https://github.com/l52312516-cell/GenshinChatBot.git
   cd GenshinChatBot
   
   2.创建虚拟环境（推荐）：
   python -m venv venv
   venv\Scripts\activate

3.安装依赖：
 pip install -r requirements.txt -i https://mirrors.aliyun.com/pypi/simple/

4.编辑 config.json，填入你的智谱AI API Key。

5.运行：
 python gui.py

点歌功能注意事项
 需要安装 LxMusic 音乐播放器，并在设置中启用开放 API 服务（端口 23330）。

 首次使用点歌时，LxMusic 会自动启动，并搜索播放你选择的歌曲。

 支持排队和多用户点歌，当前播放结束后自动播放下一首。

🛠️ 插件开发
 请参考 plugins/music_player.py 编写自己的插件。插件需要提供 register(api) 函数，通过 api.register_command 注册命令。

❓ 常见问题
1. 机器人没有反应
 确保以管理员身份运行（keyboard 和 pydirectinput 需要管理员权限）。

 检查游戏窗口是否为窗口化模式，且聊天框坐标是否正确（可在设置中调整）。

 查看日志窗口是否有错误信息。

2. 点歌搜索失败
 检查网络连接，确保可以访问网易云或酷狗 API。
 尝试使用 /k点歌 作为备选搜索源。

📜 免责声明
 本项目仅供学习和研究使用，不得用于商业用途。用户在使用本软件时，应遵守相关法律法规和游戏服务条款。本软件不修改游戏内存和网络数据，仅模拟人类输入，但使用者需自行承担使用风险。项目中的音乐搜索功能依赖第三方 API，版权归原始权利人所有。

📞 联系方式
 作者 B站： 悠闲的梅兹特利[https://space.bilibili.com/3493108868188977]
 
 企鹅交流群（偏日常，可从此途径联系作者）1092593216
 
 项目主页： [https://github.com/l52312516-cell/GenshinChatBot.git]
