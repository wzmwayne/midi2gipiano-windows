# midi2gipiano-windows

一个通过键盘模拟在原神中自动弹奏风物之诗琴的 Windows 程序。

将 MIDI 文件转换为键盘按键，实时模拟演奏。支持暂停、移调、八度调整。

## 功能

- 解析标准 MIDI 文件 (`.mid` / `.midi`)
- 自动映射音符到风物之诗琴的 21 键布局（三行：QWE… / ASD… / ZXM…）
- 键盘模拟按键（`SendInput`），不遮挡游戏窗口
- 实时进度显示、当前音符显示、时间进度
- 暂停/继续、停止控制
- 基准八度与移调调整
- 全局热键 `Ctrl+Shift+1` 切换播放/暂停
- 窗口置顶，方便操作

## 使用方法

1. 下载 [最新 Release](https://github.com/wzmwayne/midi2gipiano-windows/releases) 中的 `midi2gipiano.exe`
2. 双击运行
3. 点击 **浏览** 选择 MIDI 文件
4. 调整基准八度（默认 4）和移调（默认 0）
5. 切换到原神窗口（风物之诗琴界面）
6. 点击 **播放** 或按 `Ctrl+Shift+1` 开始自动演奏

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Shift+1` | 播放 / 暂停切换 |
| 点击 **▶ 播放** | 开始演奏 |
| 点击 **⏸ 暂停** | 暂停演奏 |
| 点击 **⏹ 停止** | 停止演奏 |

## 构建

需要 MinGW-w64 交叉编译工具链（POSIX 线程）。

```bash
make clean && make
```

输出：`midi2gipiano.exe`

## MIDI 映射说明

风物之诗琴使用 21 个按键，排列为三行：

```
Q W E R T Y U    ← 基准八度 (do re mi fa so la si)
A S D F G H J    ← 基准八度 +1
Z X C V B N M    ← 基准八度 +2
```

程序自动将 MIDI 音符映射到对应按键，超出范围时循环到下一行。

## License

MIT
