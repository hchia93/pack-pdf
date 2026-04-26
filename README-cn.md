# PackPDF

[English](README.md) | **中文**

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Platform: Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)
![Version: v0.1](https://img.shields.io/badge/Version-v0.1-orange.svg)

<div align="center">

![GUI](doc/screenshots/gui.png)

</div>

一个 Windows 桌面工具，用时间线的方式拼装 PDF。拖文件进窗口，调整顺序，按行设置页面范围或图片选项，点 Pack。完全离线运行，未来计划提供 CLI，方便 AI 代理调用同一套流水线。

## 它解决了什么

现有工具至少都中招一项：要上传、要订阅、不支持时间线、或不可脚本化。

<div align="center">

| 工具          | 离线   | 免费  | 时间线 | 备注                              |
|---------------|--------|-------|--------|-----------------------------------|
| Smallpdf      | ✗      | 受限  | ✗      | 上传文件，每天 2 任务，20 文件上限 |
| iLovePDF      | ✗      | 受限  | ✗      | 上传文件，25 文件上限              |
| PDFsam Basic  | ✓      | ✓     | ✗      | 逐文件对话框                      |
| PDF24 Creator | ✓      | ✓     | ✗      | 逐文件对话框                      |
| Stirling-PDF  | 自托管 | ✓     | 部分   | 服务器架构，不是桌面应用          |
| Adobe Acrobat | ✓      | ✗     | ✓      | 合并在订阅后面                    |
| **PackPDF**   | **✓**  | **✓** | **✓**  | Windows，免安装 exe，可被 AI 调用  |

</div>

典型场景：`A.pdf{1-7}` → 截图 → `A.pdf{12-14}` → 两张横屏照片堆到同一张 A4。一屏完成。

## 给谁用

- **频繁合并 PDF 的人。** 每周要拼报表、扫描表单、案件材料的人，那种逐文件弹窗的操作模式很快就会让人厌倦。PackPDF 把整个组装过程当作一份可编辑的清单
- **手里有敏感文件的人。** 任何不想上传的内容，合同、病历、财务报表、内部文档，都必须留在本地。浏览器工具和 SaaS 上传服务因为政策或者本能就被排除了，PackPDF 完全不联网
- **被付费墙挡住的人。** 合并几个 PDF 加几张截图，在任何合理的产品里都不该是会员功能，但好几个主流工具偏偏卡在那里限频或诱导付费。PackPDF 就是一个 exe
- **想交给 AI 代理处理的人。** 你可以让大模型帮你合并 PDF，但每轮 "不对，第 5 页和第 6 页对调，第 12 页删掉" 都消耗上下文和时间，靠对话也很难精确控制顺序。计划中的 PackPDF CLI（见路线图）就是这条流水线的代理可调用版本：确定的语法、一条命令、不需要往返

## GUI 用法

1. **把文件拖进窗口。** 支持 PDF、JPEG、PNG。每个文件成为时间线上的一行，带一个彩色徽章（`PDF` / `JPG` / `PNG`）
2. **调整顺序。** 用每行的上下箭头按钮，或 X 按钮移除该行。输出顺序就是行顺序
3. **按行设置选项。**
   - PDF 行：在 `All`（全部）、`Range [a-b]`（范围）、`Exclude [a-b]`（排除）三种页面选择中选一种（1 起始，闭区间）
   - 图片行：行内选 `Portrait`（纵向）或 `Landscape`（横向），点齿轮图标打开 popup 设置 `Reverse 180°`（180° 翻转，处理倒拍照片）、`Padding`（0.5 英寸白边）、`Scale`（`Fit page` 默认值，小图放大铺满 A4，`Original size` 保持原始像素尺寸，A4 留白即天然边距）、`Auto Merge`（仅横向有效，把两张横向图片堆叠到同一张 A4 纵向上）。任一选项偏离默认值时齿轮右上角会亮起一个小色点
4. **设置输出文件夹和文件名。** 直接读写 `<exe>/userdata/config.ini`（仓库里就有一份基盘，跟着 release zip 一起发出去）
5. **点 Pack。** 成功后会出现一个通知框，带 `Open` 按钮可直接打开生成的文件
6. **顶部主题菜单**：Photoshop Dark、Walnut、Monokai、ImGui Dark，也会持久化

合成阶段为图片生成 A4 纵向页面，把超大 PDF 页面缩到 A4 范围内，最终通过 PDFium 写出一个文件。

## CLI 用法（计划，v0.5）

本节描述**目标语法**。CLI 在 v0.5 路线图上，PackPDF 当前是 v0.1，仅提供 GUI。下面这些命令暂时还不存在。

```
packpdf add A.pdf{1-7,9,12-15}      # 一次添加多个不重叠的范围
packpdf add B.png                    # 把图片当作一页
packpdf remove A.pdf{2-3}            # 从已添加的段中扣掉若干页
packpdf list                         # 显示当前计划
packpdf compose -o out.pdf           # 写出输出文件

# 适合 AI / 脚本的无状态一次性形式：
packpdf compose "A.pdf{1-7},B.png,A.pdf{8-10}" -o out.pdf
```

范围语法 `{a-b,c,d-e}`，全部 1 起始，单个段内的范围不允许重叠。校验在解析时完成，写错语法在读任何文件之前就会被拒掉。

无状态形式才是代理使用的关键：大模型可以根据用户的需求拼出逗号分隔的计划，一次调用 `packpdf compose` 就能完成，不依赖 shell 会话也不需要逐步累积状态。同一份计划可以重现，也可以做 diff。

## 构建

### 先决条件

- **Git**
- **Visual Studio 2026** 带 C++ 工作负载（用其内置 CMake，不需要单独装）。VS 2022 也可以
- **vcpkg** 放在任意路径，通过 `VCPKG_ROOT` 环境变量暴露。如果你还没有：
  ```bat
  git clone https://github.com/microsoft/vcpkg.git <path>\vcpkg
  <path>\vcpkg\bootstrap-vcpkg.bat -disableMetrics
  setx VCPKG_ROOT <path>\vcpkg
  ```

### 首次构建 / 干净环境

在 **Visual Studio Developer Command Prompt** 中运行 `generate.bat`。它会删掉 `.vs/`、`.vscode/`、`build/`，然后跑 `cmake --preset windows-x64`。任何时候想重置构建状态都可以再跑一次。

### Visual Studio

`File` → `Open` → `Folder...` → 选仓库根目录。在启动项下拉里选 `pack-pdf.exe`，然后按 **F5**。VS 读 `CMakePresets.json`，构建产物全在 `build/windows-x64/` 之下，不污染源码树。

### 命令行

```bat
cmake --build --preset debug
cmake --build --preset release
```

可执行文件在 `build/windows-x64/bin/<Config>/pack-pdf.exe`（旁边会有 `pdfium.dll` 和 `glfw3.dll`）。

### 输出布局

所有生成内容都在 `build/` 下（已 gitignore，GNU 风格），由 `CMakePresets.json` 驱动：

```
build/
├── windows-x64/   # CMake cache + .vcxproj + bin/Debug/ + bin/Release/   (binaryDir)
├── fetched/       # ImGui 源码 + PDFium 预编译                            (FETCHCONTENT_BASE_DIR)
└── vcpkg/         # vcpkg_installed                                      (VCPKG_INSTALLED_DIR)
```

`fetched/` 和 `vcpkg/` 与 per-preset CMake 目录是兄弟关系，所以删掉 `build/windows-x64/` 不会重新下载 PDFium（约 10 MB）也不会重装 vcpkg 端口。彻底清空：跑 `generate.bat`（或者手动 `rm -rf build/`）。

## 依赖

- [Dear ImGui](https://github.com/ocornut/imgui)，UI（FetchContent，无需安装）
- [GLFW3](https://www.glfw.org/)，窗口系统（vcpkg）
- [PDFium](https://pdfium.googlesource.com/pdfium/)，PDF 读 / 渲染 / 写，通过 [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) 预编译版本（FetchContent，不从源码构建）
- [stb_image](https://github.com/nothings/stb)，图片段和悬停预览的 JPEG / PNG 解码

## 路线图

- v0.1：窗口、拖拽接收、时间线列表、按行 PDF 范围 / 图片选项、PDF + JPEG + PNG 合成通道、输出文件夹 / 文件名、主题菜单、配置持久化 **（当前）**
- v0.5：CLI，与 GUI 共用同一份 `Composer` 引擎，通过无状态命令驱动：`packpdf compose "A.pdf{1-7},B.png,A.pdf{8-10}" -o out.pdf`
- v1.0：portable zip 发布（pack-pdf.exe + pdfium.dll + glfw3.dll，解压即用，不带安装器）

## 许可证

[MIT](LICENSE)。© 2026 Hyrex Chia。
