# build 目录管理

该目录不仅是编译输出，不能整体删除或整体退出版本管理。

## 保留并跟踪

- `src/`：规范 PS、RTL、仿真、IP 和 BD 源文件。
- `constraints/`：引脚和时序约束。
- `scripts/`、`vivado.tcl`：重建、修复及验证入口。

## 本地生成物（Git 忽略）

- `output/`：bit、XSA、FSBL、应用 ELF、BOOT.BIN 和打包清单。清理时保留，不代表目录内所有历史产物都是最新版本。
- `reports/`：构建、时序、CDC 和板测记录；不删除仍用于追溯的报告。此前跟踪的报告可从 Git 历史恢复。
- `sdio_vitis_*/`、`tcp_pipeline_usb/`、`verification/`：独立验证构建，可重新生成；不是日常使用的根目录 `workspace/`。
- `workspace_backups/`：历史备份，可能含有未提交的源文件，不能当作普通缓存直接删除。
- `archives/`：本地清理归档。清理历史工作区和备份前归档，并逐文件校验；恢复到其他目录后再选择需要的文件，不要直接覆盖当前源码。

2026-10-16 清理不会重建或刷新 `output/`，也不会改变当前 Vitis 工程和源码。压缩包不纳入 Git，建议另存到备份介质；它不是已验证的正常成像版本。

本次已清理 11 个旧目录、4 个测试文件；归档包含 24888 个文件，逐文件 SHA-256 校验通过，净释放约 685 MiB。

- 归档：`archives/build_cleanup_20261016_182044504.zip`
- 归档 SHA-256：`9AE6A4B039832EA5419F9CF9D12A04F9FC4D20B036DF98D2430B477AD5E6B752`
- `cleanup-manifest.json` 位于压缩包内，记录原路径、大小及文件哈希。
- 351 个受保护的源码和交付文件在清理前后哈希一致。

`scripts/cleanup_build_outputs.ps1` 默认仅列出明确指定的历史目标；加 `-Execute` 才归档、校验及清理，不会自动匹配或删除未来的新工作区。当前这些历史目标已清理，再次运行应为空操作。

原文档中 `sdio_vitis_*` 的历史路径已归档。检查当前 IDE 工作区时，显式使用 `scripts/check_sdio_build.ps1 -Workspace .\workspace`（从工程根目录执行时完整脚本路径为 `.\build\scripts\check_sdio_build.ps1`）；需要独立新平台时按原构建流程重新生成。清理不代表当前工程通过了新的构建、时序或板测验收。
