# 把 gh-build 推到 GitHub（首次用）
# 用法: powershell -ExecutionPolicy Bypass -File push-to-github.ps1 -RepoUrl https://github.com/you/repo.git

param(
  [Parameter(Mandatory = $true)][string]$RepoUrl,
  [string]$CommitName = 'sbj',
  [string]$CommitEmail = 'sbj@users.noreply.github.com'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not (Test-Path '.git')) {
  git init | Out-Null
  git branch -M main
}
git config user.name  $CommitName
git config user.email $CommitEmail

git add -A
git -c core.quotepath=false commit -m "cloud imagebuilder: OpenWrt 24.10.8 / xiaomi_miwifi-3a" 2>$null
if ($LASTEXITCODE -ne 0) { Write-Host '[i] 没有新改动可提交，继续推送' }

$existing = git remote get-url origin 2>$null
if ($LASTEXITCODE -eq 0) { git remote set-url origin $RepoUrl } else { git remote add origin $RepoUrl }

Write-Host "[i] 推送到 $RepoUrl （如果弹窗就登录 GitHub）"
git push -u origin main

Write-Host ''
Write-Host '完成。下一步：'
Write-Host '  1. 打开仓库 -> Actions -> build-openwrt-firmware -> Run workflow'
Write-Host '  2. 跑完在页面底部 Artifacts 下载 sysupgrade.bin'
