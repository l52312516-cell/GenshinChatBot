# plugin_manager.py
import os
import sys
import importlib.util

class PluginAPI:
    def __init__(self, bot):
        self.bot = bot
        self.command_handlers = {}

    def log(self, level, msg):
        self.bot.log(level, f"[插件] {msg}")

    def send_message(self, msg):
        self.bot.send_message(msg)

    def register_command(self, cmd_name, handler):
        self.command_handlers[cmd_name] = handler
        self.log("INFO", f"注册命令: /{cmd_name}")

    def get_config(self):
        plugin_name = getattr(self.bot, 'current_plugin_name', '')
        if plugin_name:
            return self.bot.plugins_config.get(plugin_name, {})
        return {}

    def keyboard_press(self, key):
        import pydirectinput
        pydirectinput.press(key)

    def keyboard_write(self, text, interval=0.05):
        import pydirectinput
        pydirectinput.write(text, interval=interval)

    def keyboard_hotkey(self, *keys):
        import pydirectinput
        pydirectinput.hotkey(*keys)

    def mouse_click(self, x=None, y=None, button='left'):
        import pydirectinput
        if x is not None and y is not None:
            pydirectinput.moveTo(x, y)
        pydirectinput.click(button=button)

    def find_window(self, title_keywords):
        import pygetwindow as gw
        for kw in title_keywords:
            wins = gw.getWindowsWithTitle(kw)
            if wins:
                win = wins[0]
                if win.isMinimized:
                    win.restore()
                win.activate()
                return win
        return None

def get_base_path():
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    else:
        return os.path.dirname(__file__)

def load_plugins(bot, plugins_dir="plugins"):
    loaded = []
    base_path = get_base_path()
    plugins_dir = os.path.join(base_path, plugins_dir)
    if not os.path.exists(plugins_dir):
        os.makedirs(plugins_dir)
        return loaded

    enabled = bot.config.get("plugins", {}).get("enabled", [])
    if not enabled:
        bot.log("INFO", "未启用任何插件")
        return loaded

    if plugins_dir not in sys.path:
        sys.path.insert(0, plugins_dir)

    for plugin_name in enabled:
        plugin_file = os.path.join(plugins_dir, f"{plugin_name}.py")
        if not os.path.exists(plugin_file):
            bot.log("WARNING", f"插件文件不存在: {plugin_file}")
            continue

        try:
            spec = importlib.util.spec_from_file_location(plugin_name, plugin_file)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            api = PluginAPI(bot)
            bot.current_plugin_name = plugin_name

            if hasattr(module, 'register'):
                module.register(api)
                bot.plugin_commands.update(api.command_handlers)
                loaded.append(plugin_name)
                bot.log("INFO", f"插件 {plugin_name} 加载成功")
            else:
                bot.log("WARNING", f"插件 {plugin_name} 没有 register 函数")
        except Exception as e:
            bot.log("ERROR", f"加载插件 {plugin_name} 失败: {e}")

    bot.current_plugin_name = None
    return loaded