# 第三方依赖与参考

项目没有包含远程软件、模拟器或手机客户端的原始二进制。以下源码依赖随仓库保留相应许可证；本机分析产物、安卓安装包、设备配置和转储不提交。

| 内容 | 固定版本或提交 | 来源 | 许可与用途 |
|---|---|---|---|
| MinHook（函数挂接库） | 1.3.4 | https://github.com/TsudaKageyu/minhook | 两条款宽松许可，原文在 third_party/minhook/LICENSE.txt（许可证）；用于函数入口挂接。 |
| nlohmann/json（结构化数据解析库） | 3.12.0 | https://github.com/nlohmann/json | MIT（宽松许可），原文在 third_party/json/LICENSE.MIT（许可证）；用于本地配置和设置格式。 |
| FFmpeg（多媒体解码组件）公共头文件 | cd4e96eb314630788b7ff2f9820c395788c023e2（固定提交） | https://github.com/FFmpeg/FFmpeg | LGPL 2.1 或更高版本（较宽松通用公共许可），原文在 third_party/ffmpeg/COPYING.LGPLv2.1（许可证）；仅公共结构与常量，实际动态库来自用户本机投屏组件目录。 |

third_party/ffmpeg/libavutil/avconfig.h（平台配置头）为本项目生成的六十四位小端平台配置。首次调查留下的少量未使用头文件保留在仓库中，不视为额外使用的内部接口。

运行时还需要用户已有的 ADB（安卓调试桥）、scrcpy（安卓投屏组件）及配套动态库；源码包不重新分发这些二进制。本地协议参考使用投屏组件 5.0.1 的控制消息和视频帧头，下载在被忽略的分析目录中。

公开项目 uu-enhance（远程增强补丁）用于参考系统版本库代理加载思路，better-gameviewer（远程输入修复项目）用于了解已有补丁方向；本项目没有复制它们的源码。相关链接和调查范围见接入调查.md（调查记录）。
