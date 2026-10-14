# 额外资源文件全链路指南 —— 放置、配置、代码使用（以 data/emoji 为例）

> 本文回答一个问题：**我有一批静态资源（GIF/字体/图片/配置等），如何让设备上的程序在运行时读得到？**
> 全程以表情包资源 `vendor/hisilicon/hispark_taurus/data/emoji/` 为实例，每一步都对应本仓库真实文件。

## 0. 全链路总览

```
【源码仓库】                       【构建输出】                【设备】
vendor/hisilicon/hispark_taurus/
  data/emoji/*.gif
        │  ① BUILD.gn copy("emoji_assets")
        ▼
out/<product>/data/emoji/*.gif
        │  ② fs.yml userfs 镜像段: source_dir data → target_dir data
        ▼
userfs.img (vfat, 50MB 分区镜像)
        │  ③ HiTool 烧录 userfs 分区
        ▼
设备 eMMC userfs 分区 ──(liteos_a 挂载到 /storage)──►
/storage/data/emoji/*.gif
        │  ④ 代码以绝对路径读取
        ▼
ui_display.cpp: EmojiDisplay_Init("/storage/data/emoji")
```

一句话：**资源 = 源码里的普通文件 → 构建时拷进 out 的 `data/` 目录 → 打包进 userfs 分区 → 设备上出现在 `/storage/data/` 下 → 代码用绝对路径读**。

---

## 1. 放置：资源放哪里

### 本例的放置

```
vendor/hisilicon/hispark_taurus/data/emoji/
├── angry.gif      (21 个 240x240 GIF89a)
├── neutral.gif
├── happy.gif
└── ...            共 21 个
```

### 为什么放 vendor

- `vendor/<厂商>/<产品>/data/` 是**产品级数据资产的约定位置**：换一块板（如 hispark_pegasus）时，SoC 侧代码可以不动，只引用各自产品的 vendor 路径。
- 资产属于"产品定义"而非"驱动代码"，与 `device/board`、`device/soc` 解耦。

> 放在 `device/soc/.../mytest/ui_display/` 目录内也可以（BUILD.gn 的 sources 路径改成相对路径即可），原理完全相同。选 vendor 的好处是资产随产品走。

---

## 2. 配置（一）：BUILD.gn 把资源拷进构建输出目录

文件：`device/soc/hisilicon/common/platform/mytest/ui_display/BUILD.gn:32`

```gn
# GIF assets packaged into userfs data partition (out/.../data -> userfs data/)
copy("emoji_assets") {
  sources = [
    "//vendor/hisilicon/hispark_taurus/data/emoji/angry.gif",
    "//vendor/hisilicon/hispark_taurus/data/emoji/confident.gif",
    # ... 逐文件列出，共 21 个 ...
  ]
  outputs = [ "$root_out_dir/data/emoji/{{source_file_part}}" ]
}
```

三个要点：

1. **`copy` 目标必须被依赖才会执行**。它挂在可执行目标的 deps 里（BUILD.gn:27）：
   ```gn
   executable("ui_display") {
     ...
     deps = [
       "//foundation/arkui/ui_lite:ui_lite",
       ...
       ":emoji_assets",     # ← 漏了这行，资源永远不会被拷贝
     ]
   }
   ```
2. **`$root_out_dir/data/...`**：输出到 out 目录的 `data/` 子目录。这个目录名不是随便取的——它必须与下一步 fs.yml 中 userfs 镜像段的 `source_dir: data` 对应。
3. **`{{source_file_part}}`** 是 GN 内建变量，表示"只取文件名"（如 `angry.gif`），因此拷贝后保持 `data/emoji/angry.gif` 的结构。如需保留子目录层级，改用 `{{source_file_part}}` 逐文件写，或用相对路径列出。

> `copy()` 只适用于少量文件；文件很多时可考虑 `ohos_copy` 或打包脚本，但小型系统（build/lite）用 `copy()` 是标准做法。

---

## 3. 配置（二）：fs.yml 把 out 的 data/ 目录打进 userfs 分区

文件：`vendor/hisilicon/hispark_taurus/fs.yml`（`userfs` 段，fs.yml:185）

```yaml
- fs_dir_name: userfs          # 生成 userfs.img
  fs_dirs:
    -
      source_dir: obj/base/security/services/app_verify/config
      target_dir: data/verify
    -
      source_dir: storage/etc
      target_dir: etc
    -
      source_dir: data         # ← out 目录下的 data/（① 拷出来的东西在这里）
      target_dir: data         # ← 打进镜像的 data/ 子目录
    -
      target_dir: app
    -
      target_dir: data/system/param
  fs_make_cmd:
    - ${root_path}/build/lite/make_rootfs/rootfsimg_liteos.sh ${fs_dir} vfat 52428800
```

对应关系：

| 构建输出（out/…） | 镜像内路径 | 设备路径 |
|---|---|---|
| `out/…/data/emoji/angry.gif` | `data/emoji/angry.gif` | `/storage/data/emoji/angry.gif` |

要点：

- `source_dir: data` 是相对 out 产品目录的路径，**正是第 2 步 `outputs` 写入的 `$root_out_dir/data`**。两处名字必须一致。
- userfs 是**独立于 rootfs 的第二个分区**（vfat，上限 50MB），专门放可读写的用户数据与资产；rootfs（fs.yml:98 `rootfs` 段）里没有 emoji。
- 分区名 `userfs` 会出现在烧录布局（HiTool 的分区表）里，烧录时必须勾选 userfs 镜像。

---

## 4. 设备端：userfs 挂载到 /storage

本产品 LiteOS-A 内核的 rootfs 初始化代码（`los_rootfs.c`）把 userfs 分区挂载在 **`/storage`**。因此：

```
镜像内 data/emoji/neutral.gif  →  设备 /storage/data/emoji/neutral.gif
```

这正是 init 配置（`vendor/hisilicon/hispark_taurus/init_configs/init_liteos_a_3516dv300.cfg`）里 `mkdir /storage/...`、`mount vfat /dev/mmcblk0 /sdcard` 等命令能操作 userfs 内容的前提——`/storage` 就是那个 vfat 分区。

---

## 5. 代码使用

### 5.1 main 层：初始化与容错

文件：`ui_display.cpp`

```cpp
/* 表情包 GIF 目录: userfs 分区 data/emoji/ (ui_display.cpp:48) */
constexpr const char *EMOJI_DIR = "/storage/data/emoji";
```

启动流程（`RunApp()`，ui_display.cpp:109）：

```cpp
/* 1. 初始化表情库：内部自建 GifEmojiView 视图，绑定 GIF 目录 */
if (!EmojiDisplay_Init(EMOJI_DIR)) {                       // ui_display.cpp:119
    printf("[ui_display] RunApp: emoji lib init failed\n");
}

/* 2. 设计层把表情视图挂进视图树并摆放位置 */
UiDesign_Build(root);                                      // ui_display.cpp:124
/*    ui_design.cpp:64  →  EmojiDisplay_GetView() 取视图 → SetPosition 摆放 */

/* 3. 显示默认表情；失败仅打印日志，UI 其余部分照常（容错设计） */
if (EmojiDisplay_SetEmotion("neutral")) {                  // ui_display.cpp:130
    printf("[ui_display] RunApp: emoji library ready\n");
} else {
    printf("[ui_display] RunApp: emoji gif not found at %s (userfs flashed?)\n",
           EMOJI_DIR);                                     // ui_display.cpp:134
}
```

### 5.2 事件层：运行时切换表情

```cpp
/* ui_display.cpp:69 EmojiSwitchListener —— 点按钮切下一个表情 */
if (EmojiDisplay_Next()) {
    UiDesign_SetEmotionText(EmojiDisplay_CurrentName());
}
```

### 5.3 底层：GIF 解码

门面 API 最终落到 `gif_decoder.h` 的纯 C 解码器（gif_decoder.h:9 的用法示例）：

```c
GifDecoder *d = GifLoadFile("/storage/data/emoji/neutral.gif");
int delayMs   = GifDecodeNextFrame(d);   // 解码一帧并合成到画布
const uint8_t *px = GifGetCanvas(d);     // BGRA 字节流，直接喂 ui_lite ARGB8888
GifRewind(d);                            // 循环播放时回到第一帧
GifClose(d);
```

帧调度由 `GifEmojiView`（emoji_display.h:38，ui_lite `Animator` 驱动）完成；缩放用最近邻采样（`SetScale`，限制 10~400）。

### 5.4 表情注册表：名字 → 文件的映射

`emoji_display.cpp:19` 的 `EMOJI_TABLE`：

```cpp
const EmojiLibrary::EmojiEntry EmojiLibrary::EMOJI_TABLE[] = {
    { "angry",    nullptr },   /* nullptr 表示文件名 = "<名字>.gif" */
    { "neutral",  nullptr },
    { "别名",      "happy.gif" }, /* 也可让别名复用已有文件 */
    ...
};
/* EMOJI_COUNT 由表自动推导，无需手改 (emoji_display.cpp:28) */
```

`SetEmotion("happy")` 就是查这张表拼出 `/storage/data/emoji/happy.gif` 再加载。

---

## 6. 实操：新增一个资源的完整 checklist

以"新增一个表情 `smile`"为例，**共 3 处改动**：

| 步骤 | 位置 | 改动 |
|---|---|---|
| ① 放文件 | `vendor/hisilicon/hispark_taurus/data/emoji/smile.gif` | 放入 240x240 GIF89a |
| ② 构建配置 | `ui_display/BUILD.gn` `copy("emoji_assets")` 的 `sources` | 加一行 `"//vendor/hisilicon/hispark_taurus/data/emoji/smile.gif",` |
| ③ 注册名字 | `emoji_display.cpp:19` `EMOJI_TABLE` | 加一行 `{ "smile", nullptr },` |

然后：

```bash
hb build -f          # 全量重编（copy 产物随 BUILD.gn 变化更新）
# 重新烧录 —— 必须刷 userfs 分区，不能只刷 rootfs！
```

---

## 7. 故障排查

| 现象 | 原因 | 排查 |
|---|---|---|
| `[emoji] load failed: /storage/data/emoji/xxx.gif` | **userfs 分区没烧**（最常见） | HiTool 烧录时勾选 userfs.img；串口执行 `ls /storage/data/emoji` 验证 |
| 同上，但其他表情正常 | 新 GIF 没进 BUILD.gn `sources`，out 里没有 | 检查 `out/<product>/data/emoji/` 下有没有该文件 |
| `unknown emotion: xxx` | 名字没在 `EMOJI_TABLE` 注册 | emoji_display.cpp 表里加一行 |
| out 目录里没有 data/emoji | `:emoji_assets` 没挂进 `deps` | 检查 BUILD.gn executable 的 deps |
| 设备路径不对 | userfs 挂载点不是 /storage（不同产品可能不同） | 以 `ui_display.cpp` 注释与 los_rootfs.c 实际挂载点为准，改 `EMOJI_DIR` |

**容错约定**：资源缺失时 `SetEmotion` 返回 false 并打 `[emoji] load failed`，UI 其余部分不受影响（ui_display.cpp:129 注释）；未注册的名字直接丢弃（emoji_display.cpp:244）。

---

## 8. 关键文件索引

| 文件 | 作用 |
|---|---|
| `vendor/hisilicon/hispark_taurus/data/emoji/*.gif` | 资产源文件（源自 xiaozhi-esp32 otto-emoji-gif-component） |
| `device/soc/hisilicon/common/platform/mytest/ui_display/BUILD.gn:32` | copy → out data/ |
| `vendor/hisilicon/hispark_taurus/fs.yml:185` | userfs 镜像打包规则 |
| `ui_display.cpp:48,109` | 运行期目录常量与初始化 |
| `ui_design.cpp:64` | 视图摆放（设计层引用表情视图） |
| `emoji_display.cpp:19` | 表情注册表 EMOJI_TABLE |
| `gif_decoder.h` | 纯 C GIF89a 解码器 API |

> 同类资源照此办理：字体的做法几乎一样——`SourceHanSansSC-Regular.otf` 也在 out 的 `data/` 下，代码里 `FONT_DIR = "/storage/data/"`（ui_display.cpp:43），只是它由独立的 copy 目标拷入。掌握"**vendor 放文件 → BUILD.gn copy → fs.yml userfs → /storage/data → 代码绝对路径**"这条路线即可举一反三。
