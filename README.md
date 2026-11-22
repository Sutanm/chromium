# BaoFlashBrowser Next — Chromium 142 / Electron 39 · 原生 PPAPI Flash 移植

> 本文件替代本树自带的 Chromium README，记录这次移植的**版本谱系与修复覆盖度**。
> 上游 Chromium 文档仍在 [docs/README.md](docs/README.md)。

**一句话结论：只有 v142（本树）是完整修复集合。v138 与 v140 是残缺的历史基线，
不要用它们出 dist，也不要把它们当作"可以跑真实站点"的参照。**

---

## 1. 版本对照

| 项目 | v138 | v140 | **v142（本树）** |
|---|---|---|---|
| Chromium | 138.0.7204 | 140.0.7339 | **142.0.7444.52** |
| Electron | 37 | 38 | **39.0.0** |
| 源码树 | `D:\electron-build\cr\src` | `D:\electron-build-140\cr\src` | `D:\electron-build-142\cr\src` |
| HEAD / tag | `32b3d14f10612` | `c8e871cc70b75`（`bao-v140-baseline`） | `bao-v142-verified-2` |
| 移植提交数 | 1 | 118 | 6 |

### 能力矩阵

| 能力 / 修复 | v138 | v140 | v142 |
|---|---|---|---|
| 2D 出帧 | ✅ 已验证 | ✅ | ✅ 已验证 |
| Stage3D（AUTO / SOFTWARE） | ❌ 未做 | ✅ 已修 | ✅ 已验证（心跳 400+） |
| JS↔Flash 脚本桥（`SetVariable`/`PercentLoaded`…） | ❌ | ❌ | ✅ 19 个 embed API 全通 |
| Flash 私有接口覆盖面 | 第一批 | 大幅补齐 | ✅ 同上全部存在 |
| `<param>` 子元素进插件参数表 | ❌ | ❌ | ✅ |
| 非主线程阻塞发送（SyncMessageFilter） | ✅ 上游自带 | ✅ 上游自带 | ✅ 移植时曾丢失，已修回 |
| `flash_fullscreen` 按 87 语义传递 | ❌ | ❌ | ✅ |
| **真实站点端到端（4399 火线精英）** | ❌ 不可用 | ❌ 卡在 MainLoader | ✅ 进到游戏内 UI |
| 可用于出 dist | ❌ | ❌ | ✅（性能问题见 §4） |

---

## 2. 各版本修了什么、没修什么

### v138 — Chromium 138.0.7204 / Electron 37（基线）
提交：`32b3d14f10612 port(138): native PPAPI Flash on Chromium 138 / Electron 37 (baseline, verified rendering)`

- **修了**：PPAPI 复活的第一批 —— `ppapi/c/private/ppb_flash*.h` 整套私有接口、
  `ppapi/proxy/flash_{file,menu,drm,clipboard,fullscreen,font_file,print}.cc`、
  `ppapi_messages.h` 的 Flash 消息、`pepper_in_process_resource_creation`、插件启用开关。
  2D 出帧已验证。
- **没修**：Stage3D；JS↔Flash（gin internal-field）；`<param>` 插件参数；
  `flash_fullscreen` 87 语义。
- **后果**：只能证明"2D 能出画面"，真实站点一律不可用。

### v140 — Chromium 140.0.7339 / Electron 38（大批量修复）
118 个移植提交，例如 `feat(pepper): PPB_Flash_Clipboard 浏览器侧宿主`、
`PPB_Flash::DrawGlyphs`、`PPB_Flash_Menu 宿主`、`feat(net): 只为 PPAPI 插件请求关闭 ORB`、
`port(linux): 恢复 PPAPI 沙箱接线`、`feat(ppapi): [ppexp] 诊针常驻化`。

- **修了**：Stage3D；Flash 私有接口大幅补齐（剪贴板 / 菜单 / DrawGlyphs / DRM / 字体文件）；
  插件流量单独关 ORB（避免 `net::ERR_BLOCKED_BY_ORB`）；Linux 沙箱接线；诊断探针。
- **没修**：`<param>` 子元素（→ 4399 火线精英拿不到插件参数，拼出
  `nullassets/config/revisions.dat`，404 循环，一个子 SWF 都不加载）；
  gin internal-field tag（→ JS↔Flash 断）；`flash_fullscreen` 87 语义。
- **后果**：2D 与 Stage3D 可用，但真实站点仍然过不去 —— 本报告 §9.4 那 8 次误判就发生在这条线上。

### v142 — Chromium 142.0.7444.52 / Electron 39（本树，完整修复）
```
d683f01  probe(142): FlashTrace collector + instrumented PPAPI data path   ← 采集器（探针）
356a22b  fix(ppapi): route blocking plugin->browser sends through a SyncMessageFilter
b4a3ad8  fix(blink): restore <param> collection for plugin elements
61c4eb9  chore(142): drop ppdbg instrumentation, revert the OnQueryFile split
a206e42  port(142): restore legacy IPC message send path
9dd829f  port(142): revive native PPAPI Flash on Chromium 142            ← 上游删空的 legacy IPC 发送端
```

- **修了**：v138/v140 的全部能力，**外加三处前两版都没有的修复**：
  1. `<param>` 子元素回收（Blink 140+ 删插件支持时连这段循环一起删了）。
     4399 火线精英的 `MainLoader.swf` 从插件参数取资源 base，缺参数就把字面量 `null`
     拼进路径 —— 修好后 `assets/config/revisions.dat` 才 200。
  2. gin internal-field tag 回退读（V8 sandbox 下 tag 不匹配静默返回 nullptr，
     命名属性拦截器恒返回 kNo → `<embed>` API 全部消失）。
  3. `flash_fullscreen` 按 87 语义经 `PpapiMsg_PPPInstance_DidChangeView` 传递。
  4. 移植回退项：`PpapiThread::Send` 的非主线程阻塞发送（见下）。
- **已验证**：同一入口下全链路
  `MainLoader.swf → revisions.dat → gameLoaderUI.swf → config.dat → DebugTrace.swf →
  creatnameUI.swf → CreateName.swf → Main.swf` 全部 200，Stage3D 持续出帧，
  进到游戏内 UI（创建角色 / 命名框之后不再黑屏）。

> **关于 `SyncMessageFilter` 那一条**：138/140 是上游自带（`ChildThread::Send` 分线程处理），
> 142 上游把它删了，本树移植时用了一句注释"`SyncChannel::Send` 是线程安全的"做了替代 ——
> **这个假设是错的**。Flash 从工作线程调 `PPB_Flash_File`（BROWSER 方向的同步消息）时，
> 会命中 `base/sequence_checker.cc:21 DCHECK failed: CalledOnValidSequence`，
> 插件进程直接 `IMMEDIATE_CRASH`（命名框之后瞬间黑屏，异常码 `0x80000003`）。
> 现在按 138 的语义恢复：非主线程走独立 `SyncMessageFilter`。

---

## 3. 为什么"旧版本残缺"不是配置问题

缺的是**代码**，不是参数。两处硬证据：

- `html_object_element.cc` 在 138/140/142 上游都不再有 `<param>` 循环；
  只有本树把它补回来了（`b4a3ad8`）。
- `gin/object_template_builder.cc` 在 138/140 里仍是裸读 internal field（无 tag 回退），
  `ppp_instance_proxy.cc` 在 138/140 里仍硬编码 `PP_FALSE`。

也就是说，即使把 138/140 用 release 参数重新构建，这两个站点级缺陷依然在。
**若要保 138/140 作为交付线，需要把 `<param>`、gin tag、`flash_fullscreen` 三项反向回填。**

---

## 4. 已知未解决 / 未定论

| 现象 | 现状 | 判断 |
|---|---|---|
| 整体比 Electron 11（Chromium 87）慢 1.7–2× | `out/Testing` 是 `dcheck_always_on` 构建，连纯 HTML 网关页都慢 3.3× | 正在用 `out/Rel`（同配置、仅关 DCHECK）验证 |
| 打字 / 拖选卡顿 | SwapBuffers 间隔 p90 = 90 ms、p99 = 972 ms（出帧抖动） | 疑同因（DCHECK/非优化构建） |
| 载入界面 6% → 99% 慢 | 网络（单文件 4.18 MB，CDN ~70–120 KB/s）+ 按需加载；曾观测一次 56 s 的 `CreateDir` 阻塞，未复现 | 非移植缺陷为主 |
| 无 BGM，但按钮音正常 | 整轮会话**只请求了 `assets/sound/ui/button.mp3`**，没有任何 bgm/music 请求；音频输出流正常 | 游戏侧当前状态未播 BGM，非音频通路缺陷 |
| `GetDefaultCharSet` 同步往返 2 万次 | 单次 p50 0.25 ms | 吵但非瓶颈 |

---

## 5. 构建与运行

```powershell
# 环境
$env:DEPOT_TOOLS_WIN_TOOLCHAIN='0'; $env:vs2022_install='C:\BuildTools'; $env:SCCACHE_SERVER_PORT='5123'
$env:PATH="D:\depot_tools;D:\electron-build-142\cr\src\buildtools\win;D:\electron-build-142\cr\src\third_party\ninja;$env:PATH"

# 构建（PPAPI 需要 enable_plugins=true，args 见 out/Testing/args.gn）
ninja -C out/Testing -j8 electron
```

**三颗地雷**（都踩过）：

1. 启动 Electron 前必须 `Remove-Item Env:\ELECTRON_RUN_AS_NODE`（设成 `''` 无效）。
2. 构建前先杀 electron 进程，否则链接报 permission denied。
3. 本树已设 `gc.auto=0`，别让 git 触发全量 repack。
4. 在 electron 仓库给 HEAD 打非 semver tag 会让 `get-git-version.py` 的
   `git describe` 结果无法解析，构建直接失败（所以 electron 侧修复只记 commit 号）。

运行示例：

```bat
electron.exe --no-sandbox --allow-command-line-plugins ^
  "--register-pepper-plugins=<dll>#Shockwave Flash#Shockwave Flash 29.0 r0#29.0.0.171;application/x-shockwave-flash#swf#spl" ^
  <probe-dir>
```

---

## 5.5 当前决定：冻结在 142

产品线冻结在 Chromium 142 / Electron 39。将来若因硬问题被迫升级内核，按归档仓库里的手册施工，不要重新探索：

- D:\port-archive\docs\upgrade-playbook.md —— 何时必须升级、版本地图、三层依赖、
  传输层 A/B 决策、升级流程 checklist、坑清单、工作量基线
- D:\port-archive\docs\forward-version-feasibility.md —— 138→140/142/144/150 可行性实测
- D:\port-archive\manifests\142-message-baseline.txt —— PPAPI 消息面基线（75/390 种）

上面的版本矩阵以归档仓库的 README 为准；本文件只保留结论与引用。

## 6. 证据与工具

- 完整排查记录、8 次误判复盘、根因证据：`docs/reports/2026-11-21-142-flash-runtime.md`（§9、§10）
- 采集器：`ppapi/trace/flash_trace.h`（env `FLASH_TRACE_DIR` 开启；未设时零开销）
  解码器：`flashprobe/trace_decode.py`
- 探针：`D:\electron-build-142\flashprobe\`（`main.js`、`run-*.bat`、`capwin.ps1`、
  `dmp.py`、`sym.ps1`、`as3/`）
- 崩溃符号化：`dmp.py` 打印 RVA，交给
  `third_party/llvm-build/Release+Asserts/bin/llvm-symbolizer.exe --obj=out/Testing/electron.exe 0x1<RVAs>`
