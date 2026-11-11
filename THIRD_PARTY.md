# 第三方依赖与参考

项目没有包含远程软件、模拟器或手机客户端的原始二进制。以下源码依赖随仓库保留相应许可证；本机分析产物、安卓安装包、设备配置和转储不提交。

本项目自有代码采用 [MIT](LICENSE)。以下第三方依赖仍按各自许可证使用。

| 内容 | 固定版本或提交 | 来源 | 许可与用途 |
|---|---|---|---|
| MinHook（函数挂接库） | 1.3.4 | https://github.com/TsudaKageyu/minhook | 两条款宽松许可，原文在 third_party/minhook/LICENSE.txt（许可证）；用于函数入口挂接。 |
| nlohmann/json（结构化数据解析库） | 3.12.0 | https://github.com/nlohmann/json | MIT（宽松许可），原文在 third_party/json/LICENSE.MIT（许可证）；用于本地配置和设置格式。 |
| FFmpeg（多媒体解码组件）公共头文件 | cd4e96eb314630788b7ff2f9820c395788c023e2（固定提交） | https://github.com/FFmpeg/FFmpeg | LGPL 2.1 或更高版本（较宽松通用公共许可），原文在 third_party/ffmpeg/COPYING.LGPLv2.1（许可证）；仅公共结构与常量，实际动态库来自用户本机投屏组件目录。 |

third_party/ffmpeg/libavutil/avconfig.h（平台配置头）为本项目生成的六十四位小端平台配置。首次调查留下的少量未使用头文件保留在仓库中，不视为额外使用的内部接口。

运行时还需要用户已有的 ADB（安卓调试桥）、scrcpy（安卓投屏组件）及配套动态库；源码包不重新分发这些二进制。本地协议参考使用投屏组件 5.0.1 的控制消息和视频帧头，下载在被忽略的分析目录中。

公开项目 uu-enhance（远程增强补丁）用于参考系统版本库代理加载思路，better-gameviewer（远程输入修复项目）用于了解已有补丁方向；本项目没有复制它们的源码。相关链接和调查范围见接入调查.md（调查记录）。

[Antigravity-Proxy](https://github.com/yuaotian/antigravity-proxy) 用于参考直接复制 DLL 与配置目录的部署方式，以及 README 的组织写法。本轮没有复制该项目源码或资源。

## 二进制分发时保留什么

MinHook（函数挂接库）的第二条要求是：“Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.”（二进制再分发必须在随附文档或其他材料中重现版权声明、许可条件和免责声明。）完整原文同时包含反汇编引擎的声明，分发包保留整份 licenses/minhook.txt（挂接库许可），不能只给上游链接。

nlohmann/json（结构化数据解析库）的 MIT（宽松许可）要求：“The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.”（所有软件副本或实质部分须包含上述版权和许可声明。）分发包保留完整的 licenses/json.txt（数据解析库许可）。

FFmpeg（多媒体解码组件）的 LGPL 2.1（较宽松通用公共许可）第六节要求声明使用该库，并随附许可副本。项目使用其公共头文件，运行时通过可配置目录动态加载用户已有的库；本包不包含这些运行库。分发包仍保留 licenses/ffmpeg.txt（解码组件许可）和本文说明。源码内原有的各作者声明保持不变；对应头文件及本项目的六十四位平台配置见 https://github.com/Rea1-ms/UU-Remote-Patch/tree/main/third_party/ffmpeg ，上游完整源码见 https://github.com/FFmpeg/FFmpeg/tree/cd4e96eb314630788b7ff2f9820c395788c023e2 。精确补丁源码版本由包内摘要清单的源码地址给出。

当前程序没有显示版权声明的交互界面；上述文本随安装包保留即可，不要求另加启动弹窗。若以后界面显示运行时版权信息，应同时检查所使用库的界面声明条款。

这些许可材料与开发文档分开处理：运行包不包含详细实现文档或源码归档，但仍包含简短使用说明、本文和三份完整许可证。若日后合并分发安卓调试桥、投屏服务或解码运行库，需要按所用构建的实际许可保留声明并处理相应源码义务，不能把当前未捆绑运行库的结论直接沿用。
