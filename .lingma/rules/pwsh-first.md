---
trigger: pwsh-first
description: PowerShell命令使用规范
---


## 基本原则
1. **优先使用PowerShell cmdlet**
2. **避免传统CMD命令**
3. **统一命令风格和参数格式**

## 命令映射规则

### 文件操作
| 操作类型 | PowerShell命令 | 替代命令 |
|---------|---------------|----------|
| 删除文件 | `Remove-Item` | 替代del/rm |
| 列出文件 | `Get-ChildItem` | 替代dir/ls |
| 复制文件 | `Copy-Item` | 替代copy |
| 移动文件 | `Move-Item` | 替代move |

### 目录操作
| 操作类型 | PowerShell命令 | 说明 |
|---------|---------------|------|
| 创建目录 | `New-Item -ItemType Directory` | 标准创建方式 |
| 删除目录 | `Remove-Item -Recurse` | 递归删除 |
| 切换目录 | `Set-Location` | 替代cd |

## 最佳实践示例
