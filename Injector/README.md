# Injector

手动映射（Manual Map）DLL 注入器，目标进程 `cs2.exe`（x64）。
不调用 `LoadLibrary`：模块通过手写 PE 装载流程映射进目标进程，
不会出现在目标进程的 PEB 模块列表里。

## 使用（小白版：零参数）

**双击 `Injector.exe` 即可**，全自动流程：

```
双击运行
  -> 非管理员则自动弹出 UAC 提权（拒绝则中止，不加载任何模块）
  -> 查找 cs2.exe：
      已运行   -> 直接注入
      未运行   -> 通过 Steam 启动 CS2（steam://rungameid/730）
                 等待 cs2.exe 出现（最长 5 分钟）
                 等待【真实游戏窗口】获得用户焦点（最长 15 分钟）
  -> 注入内置的 Osiris.dll
  -> 窗口保持显示结果（按任意键关闭）
```

"真实游戏窗口"判定（避免过早注入）：
- 前台窗口属于 cs2.exe，且是 **SDL_app** 类、标题含 **CS2**、尺寸 **≥800×600** 的主游戏窗口
  （启动时弹出的"选择国际/国服"小窗口不满足条件，会被忽略）
- 且游戏引擎已加载（进程内存在 `client.dll` / `engine2.dll`）

如果卡在地区选择弹窗，工具会持续等待（控制台实时显示 `focused=0 engine=0`），
直到你选择区域、进入实际游戏后自动注入。

不需要、也不接受任何命令行参数。控制台输出为诊断日志，供开发者排查问题。

## 编译（Release x64，静默无窗口）

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Injector\build_injector.ps1
```

脚本依次执行：
1. MSBuild 编译 `Osiris.sln`（Release x64）→ `x64\Release\Osiris.dll`
2. 把 DLL 二进制转成 `EmbeddedDll.h`（字节数组）
3. MSBuild 编译 `Injector.vcxproj` → `Injector\x64\Release\Injector.exe`

产物：
- `Injector\x64\Release\Injector.exe`（内置当前构建的 Osiris.dll）
- 编译日志：`Injector\build_osiris.log`、`Injector\build_injector.log`

### 嵌入其它 DLL（开发者调试）

默认嵌入 `x64\Release\Osiris.dll`。想嵌入别的 DLL：

```powershell
powershell -ExecutionPolicy Bypass -File Injector\generate_embedded.ps1 -DllPath E:\path\to\custom.dll
```

重新执行第 3 步即可（或直接重跑 `build_injector.ps1`，它会重新嵌入并编译）。

## GitHub Actions 云端编译（推荐，零本地安装）

`windows.yml` 的 msbuild 任务现在会一次性产出两个文件并上传为 artifact：
- `Osiris.dll`
- `Injector.exe`（已内置对应配置的 Osiris.dll，运行即注入）

推送/手动触发后，在 Actions 页面 → 对应任务 → **Artifacts** 下载
`Osiris-Release-MSVC-windows-2022`（zip 内含上述两个文件）。

本地跑 `build_injector.ps1` 与 CI 是同一套流程（`generate_embedded.ps1` 共用）。

## 开发者调试开关（环境变量，用户无感）

| 变量 | 效果 |
|---|---|
| `INJECTOR_NO_ELEVATE=1` | 跳过 UAC 提权（自动化测试用；正常使用不设置） |
| `INJECTOR_TARGET_EXE=<名>` | 覆盖目标进程名（默认 `cs2.exe`，测试用） |
| `INJECTOR_NO_LAUNCH=1` | 目标不存在时不执行 Steam 启动，只等待 |
| `INJECTOR_DRY_RUN=1` | 走完全流程但跳过真正注入 |

## 原理

1. 解析 PE 头（校验 x64 / PE32+），按 `SizeOfImage` 在目标进程内
   `VirtualAllocEx` 分配 RWX 内存（优先镜像首选基址，失败则任意地址）。
2. 注入器本地按 VA 对齐重建镜像（头 + 各节），写入目标进程。
3. 若实际基址 ≠ 首选基址，应用 `.reloc` 重定位（DIR64 / HIGHLOW）。
4. 组装参数块 + 位置无关载荷（`Shellcode.asm`，PIC，无重定位），写入目标。
5. `CreateRemoteThread` 在目标内执行载荷：
   - 经 `ntdll!LdrLoadDll` / `LdrGetProcedureAddress` 在目标进程内解析导入表
     （与 LoadLibrary 等效，保证地址正确）；
   - 注册异常目录（`RtlAddFunctionTable`，x64 SEH）；
   - 调用 TLS 回调；
   - 调用入口点（`DllMain`，DLL_PROCESS_ATTACH）。
6. 读取载荷返回状态，清理载荷内存，保留映射后的镜像。

## 注意事项

- **自动提权（UAC）**：非管理员运行时，注入器会通过 UAC 自动申请提权并重新启动
  自身。**提权被拒绝时直接中止，不会加载任何模块。**
- **权限不足不加载**：即使已提权，若 `OpenProcess` 目标进程仍被拒绝（受保护进程等），
  同样中止，不会执行注入。
- **窗口保持**：交互式控制台运行结束后会停在 "Press any key to continue..."，
  窗口不会自动关闭；输出被重定向（脚本/CI）时自动跳过等待。
- **反作弊风险**：CS2 带有 VAC，注入行为可能被检测。仅用于学习和自用测试。
- 被映射的 DLL 入口点（DllMain）返回 `FALSE` 时注入器会报失败并释放镜像。
- 载荷超时（60 秒）视为失败，防止 DllMain 卡死时注入器悬挂。

## Osiris 参数（配置）保存位置

Osiris.dll 的配置保存在系统 AppData（Roaming）目录：

```
%APPDATA%\OsirisCS2\configs\default.cfg
```

展开后通常为 `C:\Users\<用户名>\AppData\Roaming\OsirisCS2\configs\default.cfg`。
- 写入方式：先写临时文件 `default.cfg.new`，再原子重命名覆盖 `default.cfg`
- 游戏内修改配置后自动保存；注入器在成功注入后会打印该路径

## 文件

| 文件 | 说明 |
|---|---|
| `Injector.cpp` | 零参数入口、自动提权、自动注入、诊断输出 |
| `ManualMapper.cpp/.h` | PE 解析、重定位、导入解析、载荷组装 |
| `Shellcode.asm` | 目标进程内执行的 PIC 载荷（MASM x64） |
| `EmbeddedDll.h` | 生成文件：内置 DLL 字节数组（勿手改） |
| `build_injector.ps1` | 静默构建脚本 |
