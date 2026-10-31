它们内置在程序中，并且在控制台、插件以及系统启动的一切程序的 PATH 中排在**最前**。**不要下载或安装任何东西**（不要 brew、apt、pip、winget，也不要从网上下载 ffmpeg）：它们已经在了。

| 工具 | 用途 | 变量 |
|---|---|---|
| `ffprobe` | 查看文件：流、编解码器、时长、音频和视频的 `start_time` | `BA_FFPROBE` |
| `ffmpeg`（9.0.2，含 x264/x265） | 转换、剪切、修正音画不同步、提取或替换音频、音量标准化 | `BA_FFMPEG` |
| `yt-dlp`（含 `deno`） | 从 YouTube 和几乎任何网页下载视频（用内置的 ffmpeg 合并音频和视频） | `BA_YTDLP` |
| `gst-launch-1.0`、`gst-discoverer-1.0`、`gst-inspect-1.0` | 用 GStreamer 做同样的事（与视频图相同的模块） | `BA_GST_LAUNCH` |

文件夹是 `BA_HERRAMIENTAS`。如果数据中有 `Herramientas/` 文件夹，其中的内容排在**前面**（例如某个网站改版后内置版本失效时，放一个更新的 yt-dlp：仅限这种情况，并告知操作员）。

## 规则
- 绝不覆盖原文件：结果放到 `Media/`（这样能在 `media_find` 中找到）或别人指定的地方。
- 长文件：在后台运行并查看进度；不要让控制台卡住几分钟却一声不吭。
- 直播时，ffmpeg 会与推流争抢 CPU：用 `-threads 2`，如果机器负载高（`system_hardware`），先问一下（`operator_ask`）。

## 做法
- **查看**：`ffprobe -v error -show_entries stream=index,codec_type,codec_name,start_time,duration -of compact "in.mp4"`。
- **音频滞后或超前**（固定偏差，凭眼测量或用拍手/打板）：音频提前 0.3 秒 -> `ffmpeg -i in.mp4 -itsoffset -0.3 -i in.mp4 -map 0:v -map 1:a -c copy out.mp4`（延后：`+0.3`）。不重新编码，瞬间完成。
- **偏差越来越大**（音频逐渐跑偏）：音频时钟有问题；用 `-af "aresample=async=1000"`，或用 `atempo` 拉伸（视频时长/音频时长）。
- **OBS/GStreamer 读不好的格式**：`ffmpeg -i in.ext -c:v libx264 -crf 18 -preset veryfast -c:a aac -b:a 192k out.mp4`。
- 不重新编码地**剪切**：`ffmpeg -ss 00:01:00 -to 00:02:30 -i in.mp4 -c copy out.mp4`。
- **音量均衡**（用于播出）：`-af loudnorm=I=-23:TP=-1:LRA=7`（EBU R128）。
- **提取音频**：`ffmpeg -i in.mp4 -vn -c:a pcm_s16le out.wav`。
- **网页上的视频**：`yt-dlp -f "bv*[height<=1080]+ba/b" --merge-output-format mp4 -o "<Media>/%(title)s.%(ext)s" URL`。只要音频：`-x --audio-format mp3`。只要一段：`--download-sections "*1:00-2:30"`。
- **用 GStreamer**：`gst-discoverer-1.0 in.mp4`（与 ffprobe 相同）；`gst-launch-1.0` 用来在放进视频图之前测试一条处理链。

之后：用 `media_find` 找到它，并在场景中使用。
