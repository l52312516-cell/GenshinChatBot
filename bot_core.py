# bot_core.py (EasyOCR 版本，支持自定义唤醒词)
import time
import textwrap
import json
import os
import sys
import traceback
import shutil
import pygetwindow as gw
from PIL import ImageGrab
import numpy as np
import pyperclip
import keyboard
import pydirectinput
from zhipuai import ZhipuAI
from rich.console import Console
from rich.panel import Panel
from plugin_manager import load_plugins
from collections import deque
import easyocr

console = Console()

class GenshinBot:
    def __init__(self, config_path='config.json'):
        self.log_callback = None
        if getattr(sys, 'frozen', False):
            base_path = os.path.dirname(sys.executable)
            self.config_path = os.path.join(base_path, config_path)
        else:
            self.config_path = config_path

        self.log_file_path = "bot_debug.log"
        self.open_log_file()
        self.load_config()
        self.init_ocr()
        self.init_ai()
        self.last_sent_message = ""
        self.last_send_time = 0
        self.cooldown_seconds = 3
        self.running = True
        self.message_count = 0
        self.start_time = time.time()
        self.global_history = deque(maxlen=20)
        self.pending_stop = False
        self.pending_reset = False
        self.last_heartbeat = time.time()
        self.ocr_error_count = 0

        self.last_reply_text = ""
        self.ignore_similar_until = 0

        self.clean_msg_threshold = 30
        self.last_clean_time = time.time()

        self.plugins_config = self.config.get("plugins", {}).get("config", {})
        self.plugin_commands = {}
        self.loaded_plugins = []
        self.current_plugin_name = None
        self.init_plugins()

        self.message_queue = deque()
        self.is_sending = False
        self.send_timestamps = deque()
        self.max_send_per_30s = 6
        self.rate_limit_window = 30

        self.paused = False

    def set_log_callback(self, callback):
        self.log_callback = callback

    def open_log_file(self):
        if os.path.exists(self.log_file_path):
            size = os.path.getsize(self.log_file_path)
            if size > 5 * 1024 * 1024:
                backup = self.log_file_path + ".old"
                if os.path.exists(backup):
                    os.remove(backup)
                shutil.move(self.log_file_path, backup)
        self.log_file = open(self.log_file_path, "a", encoding="utf-8")

    def log(self, level, msg):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        if level == "INFO":
            console.print(f"[bold green][INFO][/bold green] {msg}")
        elif level == "WARNING":
            console.print(f"[bold yellow][WARNING][/bold yellow] {msg}")
        elif level == "ERROR":
            console.print(f"[bold red][ERROR][/bold red] {msg}")
        else:
            console.print(f"[bold cyan][{level}][/bold cyan] {msg}")
        try:
            self.log_file.write(f"{timestamp} [{level}] {msg}\n")
            self.log_file.flush()
            if os.path.getsize(self.log_file_path) > 5 * 1024 * 1024:
                self.log_file.close()
                self.open_log_file()
        except:
            pass
        if self.log_callback:
            self.log_callback(level, f"{timestamp} [{level}] {msg}")

    def load_config(self):
        if not os.path.exists(self.config_path):
            self.log("WARNING", f"配置文件 {self.config_path} 不存在，生成默认配置...")
            default_config = {
                "game": {
                    "window_title": "原神",
                    "resolution": [1920, 1080],
                    "chat_box": [150, 720, 1150, 950]
                },
                "bot": {
                    "check_interval": 1,
                    "max_chars": 35,
                    "blacklist": [],
                    "wake_word": "派蒙萌萌萌"
                },
                "ai": {
                    "api_key": "你的智谱AI-API-Key",
                    "model": "glm-4-flash",
                    "personality": "你叫派蒙，是旅行者最好的伙伴。你可爱、活泼、贪吃，偶尔爱吐槽，但永远忠诚。请用原神派蒙的语气和简短的话回复。"
                },
                "plugins": {
                    "enabled": [],
                    "config": {}
                }
            }
            with open(self.config_path, 'w', encoding='utf-8') as f:
                json.dump(default_config, f, indent=2, ensure_ascii=False)
            self.log("INFO", "已生成默认配置文件，请修改 api_key 后重新运行")
            input("按回车键退出...")
            sys.exit(0)

        with open(self.config_path, 'r', encoding='utf-8') as f:
            self.config = json.load(f)
        self.window_title = self.config["game"]["window_title"]
        self.chat_box = tuple(self.config["game"]["chat_box"])
        self.check_interval = self.config["bot"].get("check_interval", 1)
        self.max_chars = self.config["bot"].get("max_chars", 35)
        self.blacklist = self.config["bot"].get("blacklist", [])
        self.wake_word = self.config["bot"].get("wake_word", "派蒙萌萌萌")
        self.ai_api_key = self.config["ai"]["api_key"]
        self.ai_model = self.config["ai"]["model"]
        self.ai_personality = self.config["ai"]["personality"]

    def save_config(self):
        with open(self.config_path, 'w', encoding='utf-8') as f:
            json.dump(self.config, f, indent=2, ensure_ascii=False)
        self.log("INFO", "配置文件已保存")

    def init_ocr(self):
        self.log("INFO", "初始化 EasyOCR...")
        try:
            self.reader = easyocr.Reader(['ch_sim', 'en'], gpu=False)
            self.log("INFO", "EasyOCR 初始化完成")
        except Exception as e:
            self.log("ERROR", f"EasyOCR 初始化失败: {e}")
            self.reader = None

    def init_ai(self):
        self.log("INFO", "初始化智谱AI客户端...")
        self.client = ZhipuAI(api_key=self.ai_api_key)

    def init_plugins(self):
        from plugin_manager import load_plugins
        self.loaded_plugins = load_plugins(self)
        if self.plugin_commands:
            self.log("INFO", f"已加载插件命令: {list(self.plugin_commands.keys())}")

    def get_chat_text(self):
        try:
            wins = gw.getWindowsWithTitle(self.window_title)
            if not wins:
                return ""
            win = wins[0]
            if win.isMinimized:
                win.restore()
                time.sleep(0.5)
            win.activate()
            time.sleep(0.2)
            left, top = win.left, win.top
            bbox = (left + self.chat_box[0], top + self.chat_box[1],
                    left + self.chat_box[2], top + self.chat_box[3])
            img = ImageGrab.grab(bbox=bbox)
            img_np = np.array(img)

            if self.reader is None:
                self.log("ERROR", "OCR 未初始化")
                return ""

            result = self.reader.readtext(img_np, detail=0)
            text = " ".join(result).strip()
            text = ' '.join(text.split())
            if text:
                self.log("DEBUG", f"完整OCR文本: {text[:200]}")
                self.log("INFO", f"识别到: {text[:80]}...")
                self.ocr_error_count = 0
            else:
                self.ocr_error_count += 1
                if self.ocr_error_count > 5:
                    self.log("WARNING", "连续多次未识别到文本，请检查聊天框坐标")
            return text
        except Exception as e:
            self.log("ERROR", f"OCR异常: {e}\n{traceback.format_exc()}")
            return ""

    def extract_command(self, text):
        known_commands = ['help', 'status', 'reset', 'reset_confirm', 'stop', 'stop_confirm', 'cancel', 'plugins']
        known_commands.extend(self.plugin_commands.keys())

        # 特殊矫正
        if '1g点歌' in text:
            corrected = text.replace('1g点歌', '/q点歌 ', 1)
            self.log("INFO", f"指令矫正: {text} -> {corrected}")
            return corrected
        if '/g点歌' in text:
            corrected = text.replace('/g点歌', '/q点歌 ', 1)
            self.log("INFO", f"指令矫正: {text} -> {corrected}")
            return corrected
        if '1k点歌' in text:
            corrected = text.replace('1k点歌', '/k点歌 ', 1)
            self.log("INFO", f"指令矫正: {text} -> {corrected}")
            return corrected
        if '1m点歌' in text:
            corrected = text.replace('1m点歌', '/m点歌 ', 1)
            self.log("INFO", f"指令矫正: {text} -> {corrected}")
            return corrected

        idx = text.find('/')
        if idx != -1:
            remaining = text[idx+1:].strip()
            matched_cmd = None
            matched_len = 0
            for cmd in known_commands:
                if remaining.startswith(cmd):
                    if len(cmd) > matched_len:
                        matched_cmd = cmd
                        matched_len = len(cmd)
            if matched_cmd:
                args = remaining[matched_len:].strip()
                if args:
                    corrected = '/' + matched_cmd + ' ' + args
                else:
                    corrected = '/' + matched_cmd
                self.log("INFO", f"指令矫正: {text[idx:]} -> {corrected}")
                return corrected
            first_word = remaining.split()[0] if remaining else ''
            if first_word in known_commands:
                return '/' + first_word

        for prefix in ('I', 'l', '1', '|', '¡'):
            for cmd in known_commands:
                candidate = prefix + cmd
                if candidate in text:
                    start = text.find(candidate)
                    if start != -1:
                        remainder = text[start + len(candidate):].strip()
                        corrected = '/' + cmd
                        if remainder:
                            corrected += ' ' + remainder
                        self.log("INFO", f"指令矫正: {text[start:start+len(candidate)]}{remainder} -> {corrected}")
                        return corrected
        return None

    def is_own_message(self, text):
        if not self.last_sent_message:
            return False
        if text == self.last_sent_message:
            self.log("INFO", "检测到完全相同的自身消息")
            return True
        own = self.last_sent_message
        if len(own) >= 10:
            for i in range(len(own) - 9):
                fragment = own[i:i+10]
                if fragment in text:
                    self.log("INFO", f"检测到自身消息片段(10字符): '{fragment}'")
                    return True
        else:
            if own in text:
                self.log("INFO", f"检测到自身消息完整包含: '{own}'")
                return True
        if "派蒙" in text and abs(len(text) - len(own)) < 5:
            self.log("INFO", "疑似自身消息（包含派蒙且长度接近），忽略")
            return True
        return False

    def chat_with_ai(self, user_message):
        try:
            history = list(self.global_history)[-10:]
            messages = [{"role": "system", "content": self.ai_personality}]
            messages.extend(history)
            messages.append({"role": "user", "content": user_message})
            response = self.client.chat.completions.create(
                model=self.ai_model,
                messages=messages,
            )
            reply = response.choices[0].message.content
            self.global_history.append({"role": "user", "content": user_message})
            self.global_history.append({"role": "assistant", "content": reply})
            self.log("INFO", f"AI回复: {reply}")
            return reply
        except Exception as e:
            self.log("ERROR", f"AI调用失败: {e}")
            return "派蒙好像迷路了..."

    def _send_single_chunk(self, chunk):
        pyperclip.copy(chunk)
        wins = gw.getWindowsWithTitle(self.window_title)
        if not wins:
            return
        win = wins[0]
        win.activate()
        time.sleep(0.3)
        pydirectinput.press('enter')
        time.sleep(0.5)
        keyboard.press_and_release('ctrl+v')
        time.sleep(0.1)
        pydirectinput.press('enter')
        self.log("INFO", f"已发送: {chunk}")

    def send_message(self, msg):
        if not msg:
            return
        if any(bad in msg for bad in self.blacklist):
            self.log("WARNING", f"敏感词过滤: {msg}")
            return

        now = time.time()
        while self.send_timestamps and self.send_timestamps[0] < now - self.rate_limit_window:
            self.send_timestamps.popleft()
        if len(self.send_timestamps) >= self.max_send_per_30s:
            wait_time = self.rate_limit_window - (now - self.send_timestamps[0]) + 1
            self.log("WARNING", f"发送频率过高，等待 {wait_time:.1f} 秒")
            time.sleep(wait_time)
            self.send_message(msg)
            return

        if len(msg) > self.max_chars:
            self.log("INFO", f"消息过长({len(msg)}字)，自动拆分")
        chunks = textwrap.wrap(msg, width=self.max_chars, break_long_words=True)
        for i, chunk in enumerate(chunks):
            self._send_single_chunk(chunk)
            if i < len(chunks) - 1:
                time.sleep(0.8)
        self.last_sent_message = msg
        self.last_reply_text = msg
        self.last_send_time = time.time()
        self.send_timestamps.append(time.time())
        self.ignore_similar_until = time.time() + 8

    def process_queue(self):
        if not self.message_queue or self.is_sending:
            return
        item = self.message_queue.popleft()
        msg_type, content = item
        self.is_sending = True
        try:
            if msg_type == "command":
                self.handle_command(content)
            else:
                self.message_count += 1
                reply = self.chat_with_ai(content)
                self.send_message(reply)
        except Exception as e:
            self.log("ERROR", f"处理队列消息出错: {e}")
        finally:
            self.is_sending = False

    def toggle_pause(self):
        self.paused = not self.paused
        status = "暂停" if self.paused else "恢复"
        self.log("INFO", f"机器人已{status} (Ctrl+0 可切换)")

    def handle_command(self, cmd_text):
        parts = cmd_text.split(maxsplit=1)
        full_cmd = parts[0].lower()
        if full_cmd.startswith('/'):
            cmd = full_cmd[1:]
        else:
            cmd = full_cmd
        args = parts[1] if len(parts) > 1 else ""

        if cmd in self.plugin_commands:
            self.plugin_commands[cmd](args)
            return

        if cmd == 'help':
            self.show_help()
        elif cmd == 'status':
            self.show_status()
        elif cmd == 'reset':
            self.reset_context()
        elif cmd == 'reset_confirm':
            self.reset_context_confirm()
        elif cmd == 'plugins':
            self.show_plugins()
        elif cmd == 'persona':
            self.update_persona(args)
        elif cmd == 'stop':
            self.handle_stop()
        elif cmd == 'stop_confirm':
            self.handle_stop_confirm()
        elif cmd == 'cancel':
            self.cancel_pending()
        else:
            self.log("WARNING", f"未知指令: {cmd_text}")
            self.send_message("未知指令，可用 /help 查看帮助。")

    def show_help(self):
        help_text = """可用指令：
/help      - 显示帮助
/status    - 查看机器人状态
/reset     - 重置对话历史（需二次确认）
/plugins   - 列出已加载插件
/persona <新的人设> - 修改AI人设"""
        self.send_message(help_text)
        self.log("INFO", "已发送帮助信息")

    def show_status(self):
        uptime = int(time.time() - self.start_time)
        hours = uptime // 3600
        minutes = (uptime % 3600) // 60
        seconds = uptime % 60
        ctx_len = len(self.global_history) // 2
        status = (f"运行时间: {hours}小时{minutes}分{seconds}秒\n"
                  f"处理消息数: {self.message_count}\n"
                  f"当前上下文轮数: {ctx_len}\n"
                  f"AI模型: {self.ai_model}\n"
                  f"当前人设: {self.ai_personality[:50]}...")
        self.send_message(status)
        self.log("INFO", "已发送状态信息")

    def reset_context(self):
        self.pending_reset = True
        self.send_message("确认重置对话记忆吗？请发送 /reset_confirm 确认，或 /cancel 取消。")
        self.log("INFO", "请求重置确认")

    def reset_context_confirm(self):
        if self.pending_reset:
            self.global_history.clear()
            self.pending_reset = False
            self.log("INFO", "对话历史已重置")
            self.send_message("对话记忆已清空，我们重新开始吧~")
        else:
            self.send_message("没有待确认的重置请求。")

    def show_plugins(self):
        if not self.loaded_plugins:
            self.send_message("当前没有加载任何插件")
        else:
            msg = "已加载插件：" + ", ".join(self.loaded_plugins)
            self.send_message(msg)
            self.log("INFO", f"插件列表: {self.loaded_plugins}")

    def update_persona(self, new_persona):
        if not new_persona:
            self.send_message("请提供新的人设文本，例如：/persona 你叫派蒙，可爱又贪吃")
            return
        old = self.ai_personality
        self.ai_personality = new_persona
        self.config["ai"]["personality"] = new_persona
        self.save_config()
        self.log("INFO", f"AI人设已更新: {old[:30]}... -> {new_persona[:30]}...")
        self.send_message(f"人设已更新为：{new_persona[:50]}...")

    def handle_stop(self):
        if not self.pending_stop:
            self.pending_stop = True
            self.log("INFO", "收到停止指令，进入确认等待状态")
            self.send_message("确认要停止机器人吗？请发送 /stop_confirm 确认，或 /cancel 取消。")
        else:
            self.send_message("已有停止确认请求等待中，请发送 /stop_confirm 或 /cancel。")

    def handle_stop_confirm(self):
        if self.pending_stop:
            self.log("INFO", "停止确认已收到，机器人即将关闭...")
            self.send_message("派蒙要休息了，再见旅行者~")
            time.sleep(1)
            self.running = False
        else:
            self.send_message("没有待确认的停止请求。")

    def cancel_pending(self):
        if self.pending_stop or self.pending_reset:
            self.pending_stop = False
            self.pending_reset = False
            self.log("INFO", "已取消当前操作")
            self.send_message("已取消操作。")
        else:
            self.send_message("当前没有待取消的操作。")

    def run(self):
        console.print(Panel.fit("原神聊天机器人 - 派蒙助手 (EasyOCR)", style="bold cyan", border_style="cyan"))
        console.print(f"窗口标题: {self.window_title}", style="dim")
        console.print(f"聊天框坐标: {self.chat_box}", style="dim")
        console.print(f"检测间隔: {self.check_interval}秒", style="dim")
        console.print(f"单条最大字数: {self.max_chars}", style="dim")
        console.print(f"发送后冷却: {self.cooldown_seconds}秒", style="dim")
        console.print(f"频率限制: {self.max_send_per_30s}条/{self.rate_limit_window}秒", style="dim")
        console.print(f"唤醒词: {self.wake_word}", style="dim")
        console.print("按 Ctrl+0 可暂停/恢复机器人", style="bold yellow")
        console.print("按 Ctrl+C 停止机器人\n", style="bold yellow")

        keyboard.add_hotkey('ctrl+0', self.toggle_pause)

        self.log("INFO", "机器人主循环启动，等待聊天消息...")

        while self.running:
            try:
                if time.time() - self.last_heartbeat > 60:
                    self.log("DEBUG", "心跳: 主循环运行中")
                    self.last_heartbeat = time.time()

                if self.paused:
                    time.sleep(0.5)
                    continue

                self.process_queue()

                if time.time() - self.last_send_time < self.cooldown_seconds:
                    time.sleep(0.5)
                    continue

                if time.time() < self.ignore_similar_until:
                    time.sleep(0.5)
                    continue

                raw_text = self.get_chat_text()
                if raw_text:
                    cmd = self.extract_command(raw_text)
                    if cmd:
                        self.message_queue.append(("command", cmd))
                        self.log("INFO", f"指令加入队列: {cmd}")
                    else:
                        if self.is_own_message(raw_text):
                            time.sleep(self.check_interval)
                            continue
                        # 使用自定义唤醒词
                        if self.wake_word in raw_text or f"@{self.wake_word}" in raw_text:
                            user_msg = raw_text[:45]
                            self.message_queue.append(("message", user_msg))
                            self.log("INFO", f"唤醒，消息加入队列: {user_msg}")
                        else:
                            self.global_history.append({"role": "user", "content": raw_text[:100]})
                            self.log("INFO", f"未唤醒，仅记录上下文: {raw_text[:50]}...")

                time.sleep(0.5)

            except KeyboardInterrupt:
                self.running = False
                self.log("INFO", "用户中断，机器人停止")
                break
            except Exception as e:
                self.log("ERROR", f"主循环异常: {e}\n{traceback.format_exc()}")
                time.sleep(5)

        self.log_file.close()

def start_bot(config_path='config.json'):
    bot = GenshinBot(config_path)
    bot.run()

if __name__ == "__main__":
    start_bot()