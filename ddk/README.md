# ddk/ — 树外（DDK）外部模块的构建配方

这目录里的每个子目录对应**一个外部模块**，内容由 `recon/ddkemit.py` 生成：

- `Makefile` —— `obj-m += <stem>.o`（多源文件时再加 `<stem>-objs := ...`）
- `sources.txt` —— 该模块真正用到的源文件（相对内核树根，一行一个）

源文件**不在**这里：它们留在内核树的原位（`drivers/...`、`sound/soc/sprd/...`），
构建前由 `scripts/ddk-sync.sh` 复制进来（连同同目录的 `.h`），`scripts/ddk-build.sh`
负责把两步串起来。

## 为什么是树外，而不是在内核树里加 `obj-$(CONFIG_x)`

`recon/koflags.py` 给出的判据：`.modinfo` 里没有 `intree=Y` 的模块就是**外部模块**
（`kernel/module.c:3281` —— 缺这个 tag 就打 out-of-tree taint），与设备 `/proc/modules`
里行尾的 `(O)` 标记 190/190 完全一致。190 个厂商 `.ko` 的分布是 **IN-TREE 143 / EXTERNAL 47**。

外部模块在内核 `/proc/config.gz` 里**连一行符号都没有**（例如 `CONFIG_SND_SOC_SPRD_DAI`、
`CONFIG_SPRD_AGDSP_ACCESS`、`CONFIG_TOUCHSCREEN_HYNITRON` 既不是 `=y` 也不是
`# ... is not set`），说明它们的门控符号根本不在这套 Kconfig 里 ⇒ 用
`obj-$(CONFIG_x)` 永远编不出来（Kbuild 会把整行 eval 成空）。它们必须像 Android GKI 的
vendor 模块那样，用 `make -C <kernel> M=<dir> modules` 走树外构建。

我们已重建的 39 个模块里 **36 个是外部模块**，只有 3 个是树内
（`LEDS_TORCH_GPIO`、`SPRD_COMMONPHY`、`SPRD_USB_DUMMY_PHY` —— 它们的 Kconfig 与
`obj-` 行已经补进内核树，见 08 文档 8.20）。

## 验收标准

编出来的 `.ko` 能不能被设备内核加载，取决于 **CRC**（`CONFIG_MODVERSIONS=y` 时内核校验
`__versions` 里的每个符号 CRC）。标尺在 `recon/crcdump.py`：它把 190 个厂商 `.ko` 的
`__versions`（每条 64 B = `u64 crc` + 56 B 名字）全表挖出来。自编内核 + 自编模块必须与
这些 CRC 一致，否则模块加载报 `disagrees about version of symbol xxx`。
