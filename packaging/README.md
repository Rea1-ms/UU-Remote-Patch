# UU Remote Patch

让本地 ADB 设备出现在 UU 远程的多开管理里。仅适配 Windows x64、UU 被控端 4.46.0.4716 的指定构建，手机继续使用官方客户端和中继。

## 放进去

1. 下载 https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1 的 Windows x64 包，把里面的文件放进本目录下 `runtime/scrcpy/`。不要多套一层文件夹，确认能直接找到 `adb.exe`、`scrcpy-server`、`avcodec-63.dll` 和 `avutil-61.dll`。
2. 设备开启 ADB 调试并完成授权。USB 设备通常不必改配置；网络设备在本目录的 `config.local.json` 的 `connect` 数组中填入自己的“地址:端口”。
3. 把上一级的 `version.dll` 和本目录整个 `uu-adb-patch` 文件夹复制到 UU 的 `bin` 目录，与 `GameViewerServer.exe` 放在一起。默认位置是 `C:/Program Files/Netease/GameViewer/bin`，写入时可能需要管理员权限。
4. 重启 UU 被控端或电脑，在手机端重新打开多开管理。

注意是 `GameViewer/bin`，不是上一级 `GameViewer` 目录；放错位置时，后台不会加载补丁。

准备好依赖后的参考文件树：

```text
bin/
├─ GameViewerServer.exe       # UU 原有文件
├─ version.dll               # 补丁入口
└─ uu-adb-patch/
   ├─ config.local.json      # 设备和画面设置
   ├─ runtime/
   │  └─ scrcpy/             # scrcpy 压缩包里的文件直接放在这里
   │     ├─ adb.exe
   │     ├─ AdbWinApi.dll
   │     ├─ AdbWinUsbApi.dll
   │     ├─ avcodec-63.dll
   │     ├─ avformat-63.dll
   │     ├─ avutil-61.dll
   │     ├─ disconnected.png
   │     ├─ libusb-1.0.dll
   │     ├─ LICENSE.txt
   │     ├─ open_a_terminal_here.bat
   │     ├─ scrcpy-noconsole.vbs
   │     ├─ scrcpy-server
   │     ├─ scrcpy.exe
   │     ├─ scrcpy.png
   │     ├─ SDL3.dll
   │     └─ swresample-7.dll
   ├─ README.md
   ├─ LICENSE               # 本项目的 MIT 许可证
   ├─ THIRD_PARTY.md
   ├─ licenses/
   │  ├─ minhook.txt
   │  ├─ json.txt
   │  └─ ffmpeg.txt
   └─ manifest.json
```

scrcpy 压缩包里的文件完整保留，不要再套一层 scrcpy 版本目录。

安装目录已经有同名 DLL 或旧补丁时，先退出后台、备份旧文件，别直接覆盖。重启会断开当前远程连接，请提前安排。

## 改配置、停用

安装后修改的是 UU `bin/uu-adb-patch/config.local.json`。相对路径以配置文件所在目录为起点，也支持已有依赖的绝对路径；修改后需要重启后台。网络设备会按 `connect` 尝试重连，但仍需设备自己开启网络调试。

使用已有的依赖目录时，可以只改下面两个字段，其余设置保留：

```json
{
  "adb": "D:/Program Files/scrcpy/adb.exe",
  "scrcpy": "D:/Program Files/scrcpy"
}
```

JSON 路径建议使用 `/`。如果使用反斜杠，每个 `\` 都要写成 `\\`；单个 `\` 会被当作转义符，可能导致解析失败或路径错误。`adb` 填 `adb.exe` 的路径，`scrcpy` 填包含 `scrcpy-server` 和解码库的目录，不能填 `scrcpy.exe`。

停用时退出 UU 后台，把 `bin/version.dll` 改名为 `version.dll.disabled` 或移到安装目录外，再启动。配置和依赖可以保留；恢复时把 DLL 放回即可。关闭客户端窗口不一定退出后台，补丁不支持热卸载。

## 说明

手机端主要功能已通过用户复测，复制部署到正确目录后也已由本机用户确认有效。其他电脑尚未实测；多开预览可能短暂退回占位图，多指和长期运行缺少专项手机记录。

本包不含 scrcpy、ADB 和解码运行库，也不含开发文档与注入工具。请保留 `THIRD_PARTY.md` 和 `licenses/` 中的完整许可。`manifest.json` 记录源码版本与文件摘要。

源码及完整说明：https://github.com/Rea1-ms/UU-Remote-Patch

## License

本项目采用 [MIT](LICENSE)。第三方依赖按各自许可证使用，分发时请一并保留 `THIRD_PARTY.md` 和 `licenses/`。
