# gui.py (修复 KeyError)
import sys
import os
import json
import subprocess
import threading
import webbrowser
from PySide6.QtWidgets import *
from PySide6.QtCore import Qt, QThread, Signal, QUrl
from PySide6.QtGui import QFont, QTextCursor, QIcon, QPalette, QColor, QAction
from PySide6.QtWidgets import QSystemTrayIcon

try:
    from bot_core import start_bot, GenshinBot
except ImportError:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from bot_core import start_bot, GenshinBot

def resource_path(relative_path):
    if hasattr(sys, '_MEIPASS'):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath("."), relative_path)


class BotWorker(QThread):
    log_signal = Signal(str, str)
    finished = Signal()

    def __init__(self, config_path="config.json"):
        super().__init__()
        self.config_path = config_path
        self.bot = None

    def run(self):
        self.bot = GenshinBot(self.config_path)
        self.bot.set_log_callback(self.on_log)
        self.bot.run()
        self.finished.emit()

    def on_log(self, level, msg):
        self.log_signal.emit(level, msg)

    def stop(self):
        if self.bot:
            self.bot.running = False
            if self.bot.log_file:
                self.bot.log_file.close()

    def toggle_pause(self):
        if self.bot:
            self.bot.toggle_pause()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.worker = None
        self.tray_minimize = False
        self.init_ui()
        self.apply_dark_theme()
        if not getattr(sys, 'frozen', False):
            self.check_environment()
        self.load_config_to_ui()
        self.refresh_plugin_list()
        self.load_tray_settings()
        self.setup_tray_icon()

    def init_ui(self):
        self.setWindowTitle("原神聊天机器人 - 派蒙助手 (EasyOCR)")
        self.setMinimumSize(1100, 700)
        icon_path = resource_path("icon.ico")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout(central_widget)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        self.sidebar = QListWidget()
        self.sidebar.addItems(["控制台", "插件管理", "设置", "关于"])
        self.sidebar.setFixedWidth(150)
        self.sidebar.setStyleSheet("""
            QListWidget {
                background-color: #2d2d2d;
                color: #ffffff;
                border: none;
                font-size: 14px;
                outline: none;
            }
            QListWidget::item {
                padding: 12px 10px;
                border-bottom: 1px solid #3c3c3c;
            }
            QListWidget::item:selected {
                background-color: #4a6ea5;
                border-left: 4px solid #61afef;
            }
            QListWidget::item:hover {
                background-color: #3a3a3a;
            }
        """)
        self.sidebar.currentRowChanged.connect(self.switch_page)
        main_layout.addWidget(self.sidebar)

        self.stacked_widget = QStackedWidget()
        self.stacked_widget.setStyleSheet("background-color: #1e1e1e;")
        main_layout.addWidget(self.stacked_widget, 1)

        # 控制台页面
        console_page = QWidget()
        console_layout = QVBoxLayout(console_page)
        console_layout.setContentsMargins(10, 10, 10, 10)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Consolas", 10))
        self.log_text.setStyleSheet("""
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
                border-radius: 5px;
            }
        """)
        console_layout.addWidget(self.log_text)

        btn_layout = QHBoxLayout()
        self.start_btn = QPushButton("启动机器人")
        self.stop_btn = QPushButton("停止机器人")
        self.pause_btn = QPushButton("暂停/恢复")
        self.clear_log_btn = QPushButton("清空日志")
        for btn in [self.start_btn, self.stop_btn, self.pause_btn, self.clear_log_btn]:
            btn.setFixedHeight(32)
            btn.setStyleSheet("""
                QPushButton {
                    background-color: #3c3c3c;
                    color: #ffffff;
                    border: none;
                    border-radius: 4px;
                    padding: 5px 15px;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #4a6ea5;
                }
                QPushButton:pressed {
                    background-color: #2d5a88;
                }
                QPushButton:disabled {
                    background-color: #2a2a2a;
                    color: #7a7a7a;
                }
            """)
            btn_layout.addWidget(btn)
        btn_layout.addStretch()
        console_layout.addLayout(btn_layout)
        self.stacked_widget.addWidget(console_page)

        # 插件管理页面
        plugin_page = QWidget()
        plugin_layout = QVBoxLayout(plugin_page)
        plugin_layout.setContentsMargins(10, 10, 10, 10)

        self.plugin_table = QTableWidget()
        self.plugin_table.setColumnCount(3)
        self.plugin_table.setHorizontalHeaderLabels(["插件名称", "状态", "操作"])
        self.plugin_table.horizontalHeader().setStretchLastSection(True)
        self.plugin_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.plugin_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.plugin_table.setStyleSheet("""
            QTableWidget {
                background-color: #2d2d2d;
                color: #ffffff;
                gridline-color: #3c3c3c;
            }
            QTableWidget::item {
                padding: 5px;
            }
        """)
        plugin_layout.addWidget(self.plugin_table)

        plugin_btn_layout = QHBoxLayout()
        self.refresh_plugin_btn = QPushButton("刷新列表")
        self.save_plugin_btn = QPushButton("保存启用状态")
        self.refresh_plugin_btn.setStyleSheet(self.start_btn.styleSheet())
        self.save_plugin_btn.setStyleSheet(self.start_btn.styleSheet())
        plugin_btn_layout.addWidget(self.refresh_plugin_btn)
        plugin_btn_layout.addWidget(self.save_plugin_btn)
        plugin_btn_layout.addStretch()
        plugin_layout.addLayout(plugin_btn_layout)
        self.stacked_widget.addWidget(plugin_page)

        # 设置页面
        settings_page = QWidget()
        settings_layout = QFormLayout(settings_page)
        settings_layout.setLabelAlignment(Qt.AlignRight)
        settings_layout.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)
        settings_layout.setSpacing(15)
        settings_layout.setContentsMargins(30, 30, 30, 30)

        self.api_key_edit = QLineEdit()
        self.api_key_edit.setPlaceholderText("输入你的智谱AI API Key")
        settings_layout.addRow("智谱AI API Key:", self.api_key_edit)

        self.check_interval_edit = QLineEdit()
        self.check_interval_edit.setPlaceholderText("秒")
        settings_layout.addRow("检测间隔(秒):", self.check_interval_edit)

        self.max_chars_edit = QLineEdit()
        self.max_chars_edit.setPlaceholderText("字数")
        settings_layout.addRow("单条最大字数:", self.max_chars_edit)

        self.lx_path_edit = QLineEdit()
        self.lx_path_edit.setPlaceholderText("D:\\lx-music-desktop\\lx-music-desktop.exe")
        settings_layout.addRow("LxMusic 路径:", self.lx_path_edit)

        self.wake_word_edit = QLineEdit()
        self.wake_word_edit.setPlaceholderText("例如：派蒙萌萌萌")
        settings_layout.addRow("自定义唤醒词:", self.wake_word_edit)

        self.tray_checkbox = QCheckBox("关闭窗口时最小化到系统托盘")
        self.tray_checkbox.setChecked(False)
        settings_layout.addRow("", self.tray_checkbox)

        self.save_config_btn = QPushButton("保存配置")
        self.save_config_btn.setFixedSize(120, 32)
        self.save_config_btn.setStyleSheet(self.start_btn.styleSheet())
        settings_layout.addRow("", self.save_config_btn)

        self.stacked_widget.addWidget(settings_page)

        # 关于页面
        about_page = QWidget()
        about_layout = QVBoxLayout(about_page)
        about_layout.setAlignment(Qt.AlignTop)
        about_layout.setContentsMargins(30, 30, 30, 30)

        self.about_text = QTextBrowser()
        self.about_text.setReadOnly(True)
        self.about_text.setStyleSheet("background-color: #2d2d2d; color: #ffffff; border: none;")
        self.about_text.setHtml("""
        <h2>原神聊天机器人 - 派蒙助手 (EasyOCR)</h2>
        <p><b>作者：</b>B站：<a href='https://space.bilibili.com/3493108868188977'>悠闲的梅兹特利</a></p>
        <p><b>版本：</b>4.0</p>
        <p><b>GitHub 仓库：</b><a href='https://github.com/l52312516-cell/GenshinChatBot'>https://github.com/l52312516-cell/GenshinChatBot</a></p>
        <p><b>简介：</b>基于图像识别（OCR）和智谱AI大模型的《原神》游戏内自动聊天机器人。支持自动回复、点歌排队、插件扩展等功能。</p>
        <p><b>使用教程：</b></p>
        <ol>
            <li>首次运行请确保原神为窗口化模式（推荐1920×1080），聊天框置于左下角默认位置。</li>
            <li>在设置中填写智谱AI的API Key（获取地址：<a href='https://open.bigmodel.cn/'>https://open.bigmodel.cn/</a>）。</li>
            <li>配置LxMusic音乐软件路径（点歌功能需要），下载地址：<a href='https://lxmusic.toside.cn/download'>https://lxmusic.toside.cn/download</a>。</li>
            <li>启动机器人后，在游戏中发送“派蒙萌萌萌”或“/”开头的指令唤醒机器人（可在设置中修改唤醒词）。</li>
            <li>支持指令：/help, /status, /reset, /plugins, /persona, /点歌 歌名, /k点歌 歌名, /排队列表, /下一首, /清空队列, /取消, /音乐黑名单 等。</li>
        </ol>
        <p><b>注意事项：</b></p>
        <ul>
            <li>必须以管理员身份运行（部分模拟输入需要）。</li>
            <li>首次启动会自动下载EasyOCR模型文件（约50MB），请耐心等待。</li>
            <li>点歌功能依赖LxMusic音乐软件，请确保开放API服务已启用（设置→网络设置→开放API）。</li>
        </ul>
        <p><b>项目主页：</b><a href='https://github.com/l52312516-cell/GenshinChatBot'>https://github.com/l52312516-cell/GenshinChatBot</a></p>
        """)
        self.about_text.setOpenExternalLinks(True)
        about_layout.addWidget(self.about_text)

        self.stacked_widget.addWidget(about_page)

        self.status_bar = self.statusBar()
        self.status_bar.setStyleSheet("QStatusBar { background-color: #2d2d2d; color: #aaa; }")
        self.status_bar.showMessage("就绪")

        self.start_btn.clicked.connect(self.start_bot)
        self.stop_btn.clicked.connect(self.stop_bot)
        self.pause_btn.clicked.connect(self.toggle_pause)
        self.clear_log_btn.clicked.connect(self.clear_log)
        self.save_config_btn.clicked.connect(self.save_config)
        self.refresh_plugin_btn.clicked.connect(self.refresh_plugin_list)
        self.save_plugin_btn.clicked.connect(self.save_plugin_enabled)

        self.stop_btn.setEnabled(False)
        self.pause_btn.setEnabled(False)

    def apply_dark_theme(self):
        app = QApplication.instance()
        palette = QPalette()
        palette.setColor(QPalette.Window, QColor(30, 30, 30))
        palette.setColor(QPalette.WindowText, QColor(212, 212, 212))
        palette.setColor(QPalette.Base, QColor(25, 25, 25))
        palette.setColor(QPalette.AlternateBase, QColor(53, 53, 53))
        palette.setColor(QPalette.ToolTipBase, QColor(25, 25, 25))
        palette.setColor(QPalette.ToolTipText, QColor(212, 212, 212))
        palette.setColor(QPalette.Text, QColor(212, 212, 212))
        palette.setColor(QPalette.Button, QColor(60, 60, 60))
        palette.setColor(QPalette.ButtonText, QColor(212, 212, 212))
        palette.setColor(QPalette.BrightText, Qt.red)
        palette.setColor(QPalette.Highlight, QColor(42, 130, 218))
        palette.setColor(QPalette.HighlightedText, Qt.black)
        app.setPalette(palette)

    def switch_page(self, index):
        self.stacked_widget.setCurrentIndex(index)

    def append_log(self, level, message):
        color_map = {"ERROR": "red", "WARNING": "orange", "INFO": "lightgreen", "DEBUG": "gray"}
        color = color_map.get(level.split()[0], "white")
        self.log_text.append(f'<font color="{color}">{message}</font>')
        cursor = self.log_text.textCursor()
        cursor.movePosition(QTextCursor.End)
        self.log_text.setTextCursor(cursor)

    def clear_log(self):
        self.log_text.clear()
        self.status_bar.showMessage("日志已清空")

    def check_environment(self):
        if sys.version_info < (3, 10):
            QMessageBox.critical(self, "环境错误", "需要 Python 3.10 或更高版本")
            sys.exit(1)

        required_packages = [
            "easyocr", "torch", "torchvision", "opencv-python",
            "pygetwindow", "Pillow", "numpy", "pyperclip",
            "keyboard", "pydirectinput", "zhipuai", "rich",
            "psutil", "PySide6"
        ]
        missing = []
        for pkg in required_packages:
            try:
                __import__(pkg.replace('-', '_'))
            except ImportError:
                missing.append(pkg)

        if missing:
            msg = "检测到以下 Python 库未安装：\n\n" + "\n".join(f"• {pkg}" for pkg in missing) + \
                  "\n\n是否使用阿里云镜像一键安装？"
            reply = QMessageBox.question(self, "缺少依赖", msg, QMessageBox.Yes | QMessageBox.No, QMessageBox.Yes)
            if reply == QMessageBox.Yes:
                self.status_bar.showMessage("正在安装依赖，请稍候...")
                for pkg in missing:
                    self.append_log("INFO", f"正在安装 {pkg}...")
                    try:
                        subprocess.run(
                            [sys.executable, "-m", "pip", "install", pkg,
                             "-i", "https://mirrors.aliyun.com/pypi/simple/"],
                            check=True, capture_output=True, timeout=180
                        )
                        self.append_log("INFO", f"成功安装 {pkg}")
                    except Exception as e:
                        self.append_log("ERROR", f"安装 {pkg} 失败: {e}")
                QMessageBox.information(self, "安装完成", "依赖安装完成，请重启程序。")
                sys.exit(0)
            else:
                QMessageBox.warning(self, "警告", "部分依赖缺失，机器人可能无法正常运行")

    def refresh_plugin_list(self):
        plugins_dir = "plugins"
        if not os.path.exists(plugins_dir):
            os.makedirs(plugins_dir)
        plugin_files = [f[:-3] for f in os.listdir(plugins_dir) if f.endswith('.py') and f != '__init__.py']
        config_path = "config.json"
        enabled_plugins = []
        if os.path.exists(config_path):
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    config = json.load(f)
                enabled_plugins = config.get("plugins", {}).get("enabled", [])
            except:
                pass

        self.plugin_table.setRowCount(len(plugin_files))
        for i, name in enumerate(plugin_files):
            self.plugin_table.setItem(i, 0, QTableWidgetItem(name))
            cb = QCheckBox()
            cb.setChecked(name in enabled_plugins)
            self.plugin_table.setCellWidget(i, 1, cb)
            info_btn = QPushButton("查看详情")
            info_btn.setFixedWidth(80)
            info_btn.clicked.connect(lambda checked, n=name: self.show_plugin_info(n))
            self.plugin_table.setCellWidget(i, 2, info_btn)

        self.plugin_table.resizeColumnsToContents()
        self.status_bar.showMessage(f"扫描到 {len(plugin_files)} 个插件")

    def show_plugin_info(self, plugin_name):
        plugin_path = os.path.join("plugins", f"{plugin_name}.py")
        info_text = f"插件名称: {plugin_name}\n"
        if os.path.exists(plugin_path):
            try:
                with open(plugin_path, "r", encoding="utf-8") as f:
                    content = f.read()
                lines = content.split('\n')
                desc = ""
                for line in lines[:10]:
                    if line.strip().startswith('#'):
                        desc += line.strip('#').strip() + "\n"
                    elif line.strip().startswith('"""') or line.strip().startswith("'''"):
                        desc += line.strip()[3:] + "\n"
                if desc:
                    info_text += f"描述:\n{desc}"
                else:
                    info_text += "描述: 无详细说明"
            except:
                info_text += "描述: 无法读取文件"
        else:
            info_text += "描述: 插件文件不存在"

        QMessageBox.information(self, "插件信息", info_text)

    def save_plugin_enabled(self):
        config_path = "config.json"
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                config = json.load(f)
        except:
            config = {"game": {}, "bot": {}, "ai": {}, "plugins": {"enabled": [], "config": {}}}

        enabled = []
        for i in range(self.plugin_table.rowCount()):
            name = self.plugin_table.item(i, 0).text()
            cb = self.plugin_table.cellWidget(i, 1)
            if cb.isChecked():
                enabled.append(name)
        config["plugins"]["enabled"] = enabled
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        QMessageBox.information(self, "成功", "插件启用状态已保存，重启机器人后生效")
        self.status_bar.showMessage("插件配置已保存")

    def load_tray_settings(self):
        config_path = "config.json"
        if os.path.exists(config_path):
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    config = json.load(f)
                self.tray_minimize = config.get("gui", {}).get("tray_minimize", False)
                self.tray_checkbox.setChecked(self.tray_minimize)
            except:
                self.tray_minimize = False
                self.tray_checkbox.setChecked(False)

    def load_config_to_ui(self):
        config_path = "config.json"
        if os.path.exists(config_path):
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    config = json.load(f)
                self.api_key_edit.setText(config.get("ai", {}).get("api_key", ""))
                self.check_interval_edit.setText(str(config.get("bot", {}).get("check_interval", 1)))
                self.max_chars_edit.setText(str(config.get("bot", {}).get("max_chars", 35)))
                self.lx_path_edit.setText(config.get("plugins", {}).get("config", {}).get("music_player", {}).get("software_path", ""))
                self.wake_word_edit.setText(config.get("bot", {}).get("wake_word", "派蒙萌萌萌"))
            except:
                pass

    def save_config(self):
        config_path = "config.json"
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                config = json.load(f)
        except:
            config = {"game": {}, "bot": {}, "ai": {}, "plugins": {"config": {}}}
        config["ai"]["api_key"] = self.api_key_edit.text().strip()
        config["bot"]["check_interval"] = int(self.check_interval_edit.text())
        config["bot"]["max_chars"] = int(self.max_chars_edit.text())
        config["bot"]["wake_word"] = self.wake_word_edit.text().strip()
        config["plugins"]["config"]["music_player"] = config["plugins"]["config"].get("music_player", {})
        config["plugins"]["config"]["music_player"]["software_path"] = self.lx_path_edit.text().strip()
        # 确保 gui 字段存在
        if "gui" not in config:
            config["gui"] = {}
        self.tray_minimize = self.tray_checkbox.isChecked()
        config["gui"]["tray_minimize"] = self.tray_minimize
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        QMessageBox.information(self, "成功", "配置已保存，重启机器人后生效")

    def setup_tray_icon(self):
        icon_path = resource_path("icon.ico")
        if not os.path.exists(icon_path):
            icon_path = None
        self.tray_icon = QSystemTrayIcon(self)
        if icon_path:
            self.tray_icon.setIcon(QIcon(icon_path))
        else:
            self.tray_icon.setIcon(QIcon())
        self.tray_icon.setToolTip("原神聊天机器人 - 派蒙助手")
        tray_menu = QMenu()
        show_action = QAction("显示主窗口", self)
        show_action.triggered.connect(self.show_normal)
        quit_action = QAction("退出", self)
        quit_action.triggered.connect(self.quit_app)
        tray_menu.addAction(show_action)
        tray_menu.addAction(quit_action)
        self.tray_icon.setContextMenu(tray_menu)
        self.tray_icon.activated.connect(self.on_tray_activated)
        self.tray_icon.show()

    def on_tray_activated(self, reason):
        if reason == QSystemTrayIcon.DoubleClick:
            self.show_normal()

    def show_normal(self):
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def quit_app(self):
        if self.worker and self.worker.isRunning():
            self.worker.stop()
            self.worker.wait()
        self.tray_icon.hide()
        QApplication.quit()

    def closeEvent(self, event):
        if self.tray_minimize:
            event.ignore()
            self.hide()
            self.tray_icon.showMessage("派蒙助手", "程序已最小化到系统托盘", QSystemTrayIcon.Information, 1000)
        else:
            reply = QMessageBox.question(self, "确认退出", "确定要退出程序吗？", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if reply == QMessageBox.Yes:
                if self.worker and self.worker.isRunning():
                    self.worker.stop()
                    self.worker.wait()
                event.accept()
            else:
                event.ignore()

    def start_bot(self):
        if self.worker and self.worker.isRunning():
            QMessageBox.warning(self, "提示", "机器人已在运行中")
            return
        if not os.path.exists("config.json"):
            QMessageBox.critical(self, "错误", "配置文件 config.json 不存在")
            return
        with open("config.json", "r", encoding="utf-8") as f:
            config = json.load(f)
        if not config.get("ai", {}).get("api_key", "").strip():
            reply = QMessageBox.question(self, "警告", "API Key未设置，是否现在去设置？", QMessageBox.Yes | QMessageBox.No)
            if reply == QMessageBox.Yes:
                self.stacked_widget.setCurrentIndex(2)
            return
        self.worker = BotWorker("config.json")
        self.worker.log_signal.connect(self.append_log)
        self.worker.finished.connect(self.on_bot_finished)
        self.worker.start()
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.pause_btn.setEnabled(True)
        self.status_bar.showMessage("机器人已启动")

    def stop_bot(self):
        if self.worker and self.worker.isRunning():
            self.worker.stop()
            self.worker.wait()
        self.status_bar.showMessage("机器人已停止")
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.pause_btn.setEnabled(False)

    def on_bot_finished(self):
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.pause_btn.setEnabled(False)
        self.status_bar.showMessage("机器人已退出")

    def toggle_pause(self):
        if self.worker and self.worker.isRunning():
            self.worker.toggle_pause()
            self.status_bar.showMessage("已切换暂停/恢复")


def main():
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()