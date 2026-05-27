# plugins/music_player.py (网易云+酷狗)
import subprocess
import json
import urllib.parse
import urllib.request
import os
import sys
import time
import threading
import re
from collections import deque

# 获取基础路径（兼容 PyInstaller 打包）
def get_base_path():
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    else:
        return os.path.dirname(__file__)

BASE_PATH = get_base_path()
BLACKLIST_FILE = os.path.join(BASE_PATH, "blacklist.json")

def load_blacklist():
    if os.path.exists(BLACKLIST_FILE):
        try:
            with open(BLACKLIST_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                return data.get("keywords", [])
        except:
            return []
    return []

def save_blacklist(keywords):
    with open(BLACKLIST_FILE, "w", encoding="utf-8") as f:
        json.dump({"keywords": keywords}, f, indent=2, ensure_ascii=False)

def clean_singer_name(singer):
    pattern = r'[,;、，；/&及]'
    match = re.split(pattern, singer)
    if match:
        return match[0].strip()
    return singer.strip()

def clean_song_name(raw_name):
    if not raw_name:
        return ""
    name = raw_name.strip()
    match = re.search(r'(LV\.\d+|Lv\.\d+|等级\d+)', name, re.IGNORECASE)
    if match:
        name = name[:match.start()].strip()
    common_prefixes = ["玛拉妮", "八奈见", "派蒙", "刻晴", "胡桃", "钟离"]
    for prefix in common_prefixes:
        if name.startswith(prefix) and (len(name) > len(prefix) and name[len(prefix)] in (' ', '：', ':')):
            name = name[len(prefix):].strip()
            break
    if not name:
        return raw_name.strip()
    return name

def normalize_text(text):
    return re.sub(r'[^\w\u4e00-\u9fff]', '', text).lower()

def is_blocked(song_info, blacklist):
    if not blacklist:
        return False, None
    check_texts = [song_info.get('name', ''), song_info.get('singer', '')]
    for text in check_texts:
        text_lower = text.lower()
        for kw in blacklist:
            if kw.lower() in text_lower:
                return True, kw
        normalized = normalize_text(text)
        for kw in blacklist:
            normalized_kw = normalize_text(kw)
            if normalized_kw and normalized_kw in normalized:
                return True, kw
    return False, None

def register(api):
    api.log("INFO", "正在加载点歌插件（打包兼容版）...")
    config = api.get_config()

    # ---------- 1. 获取 LxMusic 可执行文件路径 ----------
    software_path = config.get("software_path", "").strip()
    lx_exe = None
    if software_path and os.path.exists(software_path):
        lx_exe = software_path
        api.log("INFO", f"✓ 使用配置路径: {lx_exe}")
    else:
        default_paths = [
            r"C:\Program Files\lx-music-desktop\lx-music-desktop.exe",
            r"C:\Program Files (x86)\lx-music-desktop\lx-music-desktop.exe",
            r"D:\Program Files\lx-music-desktop\lx-music-desktop.exe",
        ]
        for path in default_paths:
            if os.path.exists(path):
                lx_exe = path
                api.log("INFO", f"✓ 自动探测到路径: {lx_exe}")
                break

    if not lx_exe:
        api.log("ERROR", "未找到 LxMusic 可执行文件，请在 config.json 中配置 software_path")
        return

    # ---------- 2. 黑名单管理 ----------
    blacklist = load_blacklist()

    def add_to_blacklist(args):
        keyword = args.strip()
        if not keyword:
            api.send_message("用法：/音乐黑名单 歌名关键词")
            return
        if keyword in blacklist:
            api.send_message(f"关键词「{keyword}」已在黑名单中")
            return
        blacklist.append(keyword)
        save_blacklist(blacklist)
        api.send_message(f"已添加关键词「{keyword}」到黑名单")
        api.log("INFO", f"黑名单添加: {keyword}")

    def remove_from_blacklist(args):
        keyword = args.strip()
        if not keyword:
            api.send_message("用法：/移除黑名单 歌名关键词")
            return
        if keyword not in blacklist:
            api.send_message(f"关键词「{keyword}」不在黑名单中")
            return
        blacklist.remove(keyword)
        save_blacklist(blacklist)
        api.send_message(f"已从黑名单移除关键词「{keyword}」")

    def show_blacklist(args):
        if not blacklist:
            api.send_message("黑名单为空")
        else:
            msg = "黑名单关键词：" + "、".join(blacklist)
            api.send_message(msg)

    def is_input_blocked(song_name):
        song_lower = song_name.lower()
        for kw in blacklist:
            if kw.lower() in song_lower:
                return True, kw
        normalized_song = normalize_text(song_name)
        for kw in blacklist:
            if normalize_text(kw) in normalized_song:
                return True, kw
        return False, None

    # ---------- 3. 搜索函数 ----------
    def search_netease(keyword, limit=4):
        try:
            url = "https://music.163.com/api/search/get/web?csrf_token="
            data = {"s": keyword, "type": 1, "limit": limit, "offset": 0}
            headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                "Content-Type": "application/x-www-form-urlencoded",
                "Referer": "https://music.163.com/",
            }
            post_data = urllib.parse.urlencode(data).encode()
            req = urllib.request.Request(url, data=post_data, headers=headers)
            with urllib.request.urlopen(req, timeout=5) as resp:
                result = json.loads(resp.read().decode("utf-8"))
                songs = result.get("result", {}).get("songs", [])
                results = []
                for song in songs[:limit]:
                    name = song.get("name", "未知歌曲")
                    raw_singer = ", ".join([a["name"] for a in song.get("artists", [])])
                    singer = clean_singer_name(raw_singer)
                    duration = song.get("duration", 0) / 1000.0
                    song_info = {
                        "name": name,
                        "singer": singer,
                        "display": f"{name} - {singer}",
                        "duration": duration,
                        "source": "wy"
                    }
                    blocked, kw = is_blocked(song_info, blacklist)
                    if blocked:
                        continue
                    results.append(song_info)
                return results
        except Exception as e:
            api.log("ERROR", f"网易云搜索失败: {e}")
            return []

    def search_kugou(keyword, limit=4):
        try:
            url = f"http://mobilecdn.kugou.com/api/v3/search/song?format=json&keyword={urllib.parse.quote(keyword)}&page=1&pagesize={limit}"
            with urllib.request.urlopen(url, timeout=5) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                songs = data.get('data', {}).get('info', [])
                results = []
                for song in songs[:limit]:
                    name = song.get('songname', '未知歌曲')
                    singer = song.get('singername', '未知歌手')
                    duration = song.get('duration', 0)
                    if isinstance(duration, str):
                        duration = int(float(duration))
                    song_info = {
                        "name": name,
                        "singer": singer,
                        "display": f"{name} - {singer}",
                        "duration": duration,
                        "source": "kw"
                    }
                    blocked, kw = is_blocked(song_info, blacklist)
                    if blocked:
                        continue
                    results.append(song_info)
                return results
        except Exception as e:
            api.log("ERROR", f"酷狗搜索失败: {e}")
            return []

    def format_search_result(results):
        lines = []
        for i, r in enumerate(results, 1):
            lines.append(f"{i}. {r['display']}")
        return "\n".join(lines)

    # ---------- 4. 队列与播放 ----------
    song_queue = deque()
    current_song = None
    current_timer = None
    lock = threading.Lock()
    pending_search = {"keyword": None, "results": [], "expire_time": 0, "source_name": ""}
    SEARCH_TIMEOUT = 50

    def cancel_timer():
        nonlocal current_timer
        if current_timer is not None:
            current_timer.cancel()
            current_timer = None

    def ensure_lx_running():
        try:
            import psutil
            for proc in psutil.process_iter(['name']):
                if proc.info['name'] == 'lx-music-desktop.exe':
                    return True
        except:
            pass
        try:
            subprocess.Popen([lx_exe])
            api.log("INFO", "启动 LxMusic...")
            time.sleep(3)
            return True
        except:
            return False

    def play_song_by_search(song_info):
        payload = {
            "name": song_info["name"],
            "singer": song_info["singer"]
        }
        if song_info.get("source"):
            payload["source"] = song_info["source"]
        json_str = json.dumps(payload, separators=(',', ':'))
        encoded_data = urllib.parse.quote(json_str)
        lx_url = f"lxmusic://music/searchPlay?data={encoded_data}"
        api.log("DEBUG", f"协议播放 URL: {lx_url}")
        try:
            subprocess.Popen([lx_exe, lx_url])
            api.log("INFO", f"协议点歌已发送: {song_info['display']}")
            return True
        except Exception as e:
            api.log("ERROR", f"点歌失败: {e}")
            try:
                os.startfile(lx_url)
                api.log("INFO", f"点歌 fallback (os.startfile) 成功: {song_info['display']}")
                return True
            except:
                api.send_message(f"播放失败：无法调用 LxMusic 播放《{song_info['name']}》")
                return False

    def auto_next():
        nonlocal current_timer, current_song
        with lock:
            current_timer = None
            if not song_queue:
                current_song = None
                api.log("INFO", "队列为空，停止播放")
                return
        api.log("INFO", "定时器触发，自动播放下一首")
        play_next()

    def start_playback(song_info):
        nonlocal current_song, current_timer
        success = play_song_by_search(song_info)
        if success:
            cancel_timer()
            with lock:
                current_song = song_info["display"]
            duration = song_info.get("duration", 0)
            if duration <= 0:
                duration = 30
                api.log("WARNING", f"无法获取歌曲时长，使用默认 {duration} 秒")
            else:
                duration = max(0.5, duration - 0.5)
            api.log("INFO", f"正在播放: {current_song}，将在 {duration:.1f} 秒后自动切歌")
            current_timer = threading.Timer(duration, auto_next)
            current_timer.daemon = True
            current_timer.start()
            return True
        return False

    def play_next():
        nonlocal current_song
        with lock:
            if not song_queue:
                current_song = None
                api.log("INFO", "队列为空，停止播放")
                return False
            next_song = song_queue.popleft()
        api.log("INFO", f"播放队列下一首: {next_song['display']}")
        return start_playback(next_song)

    def add_to_queue(song_info):
        with lock:
            song_queue.append(song_info)
            if current_song is None:
                api.log("INFO", "当前无播放，立即播放新歌曲")
                threading.Thread(target=play_next, daemon=True).start()
                return True
            else:
                api.log("INFO", f"歌曲已加入队列: {song_info['display']}，当前排队人数: {len(song_queue)}")
                return False

    # ---------- 取消搜索 ----------
    def cancel_search(args):
        nonlocal pending_search
        if pending_search["keyword"] is not None:
            pending_search["keyword"] = None
            pending_search["results"] = []
            pending_search["expire_time"] = 0
            pending_search["source_name"] = ""
            cancel_timer()
            api.send_message("已退出当前搜索")
        else:
            api.send_message("当前没有进行中的搜索")

    # ---------- 选择、队列等通用操作 ----------
    def select_song(args):
        if pending_search["keyword"] is None:
            api.send_message("当前没有等待选择的搜索结果，请先使用 /点歌 或 /k点歌 搜索")
            return
        if time.time() > pending_search["expire_time"]:
            api.send_message("搜索结果已过期，请重新搜索")
            pending_search["keyword"] = None
            pending_search["results"] = []
            pending_search["source_name"] = ""
            return
        parts = args.strip().split()
        if not parts:
            api.send_message("用法：/选歌 序号")
            return
        try:
            idx = int(parts[0])
        except ValueError:
            api.send_message("请提供数字序号")
            return
        if idx < 1 or idx > len(pending_search["results"]):
            api.send_message(f"序号范围 1-{len(pending_search['results'])}")
            return
        selected = pending_search["results"][idx - 1]
        pending_search["keyword"] = None
        pending_search["results"] = []
        pending_search["source_name"] = ""
        immediate = add_to_queue(selected)
        if immediate:
            api.send_message(f"正在播放《{selected['display']}》")
        else:
            with lock:
                pos = len(song_queue)
            api.send_message(f"已加入队列，当前第 {pos} 位")
        api.log("INFO", f"用户选择: {selected['display']}")

    def skip_song(args):
        nonlocal current_song
        with lock:
            if current_song is None and not song_queue:
                api.send_message("当前没有播放，队列为空")
                return
        cancel_timer()
        with lock:
            current_song = None
        if play_next():
            api.send_message("已跳过，正在播放下一首")
        else:
            api.send_message("队列为空，已停止播放")

    def show_queue(args):
        with lock:
            if not song_queue:
                api.send_message("当前队列为空")
            else:
                queue_list = list(song_queue)
                msg = "排队列表：\n" + "\n".join(f"{i+1}. {s['display']}" for i, s in enumerate(queue_list))
                if len(msg) > 300:
                    msg = msg[:300] + "..."
                api.send_message(msg)

    def clear_queue(args):
        with lock:
            song_queue.clear()
            current_song = None
            cancel_timer()
        api.send_message("已清空排队队列")

    # ---------- 点歌命令 ----------
    def make_search_command(search_func, source_name):
        def command(args):
            raw_keyword = args.strip()
            if not raw_keyword:
                api.send_message(f"用法：/{source_name}点歌 歌名")
                return
            cleaned = clean_song_name(raw_keyword)
            api.log("INFO", f"原始点歌参数: {raw_keyword} -> 清洗后: {cleaned}")
            if not cleaned:
                api.send_message("歌名无效，请重新输入")
                return

            if pending_search["keyword"] is not None and time.time() < pending_search["expire_time"]:
                api.send_message(f"请先完成当前搜索，使用 /选歌 序号 或 /取消")
                return

            blocked, kw = is_input_blocked(cleaned)
            if blocked:
                api.send_message(f"点歌失败：歌曲包含黑名单关键词「{kw}」")
                return

            if not ensure_lx_running():
                api.send_message("无法启动 LxMusic")
                return

            api.log("INFO", f"{source_name}搜索歌曲: {cleaned}")
            results = search_func(cleaned, limit=4)
            if not results:
                api.send_message(f"{source_name}未找到与「{cleaned}」相关的歌曲")
                return
            pending_search["keyword"] = cleaned
            pending_search["results"] = results
            pending_search["expire_time"] = time.time() + SEARCH_TIMEOUT
            pending_search["source_name"] = source_name
            result_msg = f"找到以下歌曲（50秒内请回复 /选歌 序号）：\n{format_search_result(results)}"
            api.send_message(result_msg)
        return command

    api.register_command("点歌", make_search_command(search_netease, "网易云"))
    api.register_command("k点歌", make_search_command(search_kugou, "酷狗"))
    api.register_command("选歌", select_song)
    api.register_command("取消", cancel_search)
    api.register_command("退出", cancel_search)
    api.register_command("音乐黑名单", add_to_blacklist)
    api.register_command("移除黑名单", remove_from_blacklist)
    api.register_command("黑名单列表", show_blacklist)
    api.register_command("排队列表", show_queue)
    api.register_command("下一首", skip_song)
    api.register_command("清空队列", clear_queue)

    api.log("INFO", "点歌插件加载完成（打包兼容版）")