#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the HSpec UI design-reference mockups as PNGs.

Every number mirrors either `hspec.cpp` (marked "code") or
`docs/design-spec.md`. Scenes are HTML rendered through headless Edge at
2x, so type scale, oklch tokens and geometry stay 1:1 with what the
settings window actually draws.

Usage:
    python docs/mockup/src/render_mockups.py           # render all scenes
    python docs/mockup/src/render_mockups.py theme     # only scenes whose name contains "theme"

Output: docs/mockup/*.png   HTML intermediates: docs/mockup/src/_build/
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT_DIR = ROOT / "docs" / "mockup"
BUILD_DIR = Path(__file__).resolve().parent / "_build"
FONT = ROOT / "build" / "font-backup" / "MiSans-Medium.ttf"
EDGE = Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")

# ---------------------------------------------------------------------------
# Tokens — docs/design-spec.md §1  (code: MakePalette)
# ---------------------------------------------------------------------------

def token_vars(dark: bool, hue: int = 250) -> str:
    h = hue
    if dark:
        vals = {
            "primary": f"oklch(0.75 0.14 {h})", "chip-bg": f"oklch(0.33 0.035 {h})",
            "chip-fg": f"oklch(0.75 0.10 {h})", "title": f"oklch(0.88 0.010 {h})",
            "detail": f"oklch(0.70 0.015 {h})", "card": f"oklch(0.23 0.015 {h})",
            "page": f"oklch(0.16 0.014 {h})", "divider": "rgba(255,255,255,0.14)",
            "item-bg": f"oklch(0.33 0.035 {h})", "item-fg": f"oklch(0.75 0.10 {h})",
            "knob-ring": "rgba(0,0,0,0.157)",
        }
    else:
        vals = {
            "primary": f"oklch(0.70 0.14 {h})", "chip-bg": f"oklch(0.95 0.025 {h})",
            "chip-fg": f"oklch(0.55 0.12 {h})", "title": f"oklch(0.25 0.020 {h})",
            "detail": f"oklch(0.42 0.020 {h})", "card": "#ffffff",
            "page": f"oklch(0.95 0.010 {h})", "divider": "rgba(0,0,0,0.086)",
            "item-bg": f"oklch(0.95 0.025 {h})", "item-fg": f"oklch(0.55 0.12 {h})",
            "knob-ring": "rgba(0,0,0,0.125)",
        }
    return ";".join(f"--{k}:{v}" for k, v in vals.items())


HUE_STOPS = ",".join(f"oklch(0.70 0.14 {h})" for h in range(0, 360, 30))

CSS = """
@font-face { font-family:'MiSans';
  src:url('file:///%s') format('truetype'); font-weight:500; }
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family:'MiSans','Microsoft YaHei',sans-serif; font-weight:500;
       -webkit-font-smoothing:antialiased; }
""" % FONT.as_posix()

# ---------------------------------------------------------------------------
# Controls — code: DrawSwitch / DrawSlider / DrawHueStrip / DrawThemeThumb
# ---------------------------------------------------------------------------

def switch(on: bool) -> str:
    bg = "var(--primary)" if on else "var(--chip-bg)"
    kx = "right:2.5px" if on else "left:2.5px"
    return (f'<div style="width:44px;height:24px;border-radius:12px;background:{bg};'
            f'position:relative;flex:none"><div style="position:absolute;top:2.5px;{kx};'
            f'width:19px;height:19px;border-radius:50%;background:#fff"></div></div>')


def slider(t: float, w: int = 300) -> str:
    kx = round(2 + (w - 4) * max(0.0, min(1.0, t)))
    return (f'<div style="position:relative;width:{w}px;height:13px">'
            f'<div style="position:absolute;top:4.5px;left:2px;right:2px;height:4px;'
            f'border-radius:2px;background:var(--chip-bg)"></div>'
            f'<div style="position:absolute;top:4.5px;left:2px;width:{max(0, kx-2)}px;height:4px;'
            f'border-radius:2px;background:var(--primary)"></div>'
            f'<div style="position:absolute;top:0;left:{kx-6.5}px;width:13px;height:13px;'
            f'border-radius:50%;background:#fff;border:1px solid var(--knob-ring);'
            f'box-sizing:border-box"></div></div>')


def hue_strip(w: int = 560) -> str:
    return (f'<div style="width:{w}px;height:14px;border-radius:7px;'
            f'background:linear-gradient(to right,{HUE_STOPS});position:relative">'
            f'<div style="position:absolute;left:62%;top:1.5px;width:11px;height:11px;'
            f'border-radius:50%;background:#fff;border:1px solid rgba(0,0,0,0.2);'
            f'box-sizing:border-box"></div></div>')


def radio(on: bool) -> str:
    ring = "var(--primary)" if on else "var(--chip-bg)"
    core = '<div style="width:6px;height:6px;border-radius:50%;background:#fff"></div>' if on else ""
    fill = "var(--primary)" if on else "transparent"
    return (f'<div style="width:16px;height:16px;border-radius:50%;flex:none;'
            f'border:1.5px solid {ring};background:{fill};display:flex;'
            f'align-items:center;justify-content:center">{core}</div>')


def theme_thumb(kind: str, w: int = 227, h: int = 119) -> str:
    """code: DrawThemeThumb — programmatic, no real text (design-spec §7).
    Tile is sized by the card width like the real page: tileW = (w-72)/3,
    thumbH = tileW * 84 / 161."""
    scale = w / 161.0
    bar = lambda bw, c: (f'<div style="width:{bw * scale:.1f}px;height:{3.6 * scale:.1f}px;'
                         f'border-radius:2px;background:{c};margin-bottom:{4 * scale:.1f}px"></div>')
    if kind == "plain":
        rows = "".join(bar(wid, "rgba(255,255,255,0.92)") for wid in (74, 70, 66))
        inner = f'<div style="padding:{13 * scale:.0f}px {12 * scale:.0f}px">{rows}</div>'
        card = ""
    elif kind == "light":
        rows = "".join(bar(wid, "rgba(0,0,0,0.62)") for wid in (74, 70, 66))
        inner = f'<div style="padding:{13 * scale:.0f}px {12 * scale:.0f}px">{rows}</div>'
        card = (f'position:absolute;inset:{9 * scale:.0f}px;border-radius:{8 * scale:.0f}px;'
                f'background:rgba(255,255,255,0.96);box-shadow:0 {2 * scale:.0f}px '
                f'{8 * scale:.0f}px rgba(0,0,0,0.18)')
    else:
        rows = "".join(
            f'<div style="display:flex;gap:{6 * scale:.0f}px;align-items:center;'
            f'margin-bottom:{5 * scale:.0f}px">'
            f'<div style="width:{9 * scale:.0f}px;height:{9 * scale:.0f}px;'
            f'border-radius:{3 * scale:.0f}px;background:var(--chip-fg)"></div>'
            f'{bar(bwid, "rgba(0,0,0,0.55)")}</div>' for bwid in (62, 58, 54))
        inner = f'<div style="padding:{12 * scale:.0f}px {10 * scale:.0f}px">{rows}</div>'
        card = (f'position:absolute;inset:{8 * scale:.0f}px;border-radius:{10 * scale:.0f}px;'
                f'background:rgba(255,255,255,0.84);backdrop-filter:blur(6px);'
                f'box-shadow:0 {2 * scale:.0f}px {10 * scale:.0f}px rgba(0,0,0,0.16)')
    bg = "linear-gradient(135deg,#5b6d8e,#8a7f6d)"
    body = f'<div style="{card}">{inner}</div>' if card else inner
    return (f'<div style="width:{w}px;height:{h}px;border-radius:{8 * scale:.0f}px;'
            f'overflow:hidden;position:relative;background:{bg};flex:none">{body}</div>')


ROW_ICONS = {
    "cpu": '<rect x="2.5" y="2.5" width="9" height="9" rx="1"/>',
    "board": '<rect x="2" y="3" width="10" height="8" rx="1"/><path d="M4 13.5h6"/>',
    "mem": '<rect x="1.5" y="4" width="11" height="6" rx="1"/><path d="M3.5 12.5h7"/>',
    "disk": '<rect x="2" y="3.5" width="10" height="7" rx="1"/><circle cx="7" cy="7" r="1.6"/>',
    "gpu": '<rect x="1.5" y="3.5" width="11" height="7" rx="1"/><path d="M4 12.5h6"/>',
    "net": '<circle cx="7" cy="7" r="5"/><path d="M2 7h10M7 2c2 2.2 2 7.8 0 10M7 2c-2 2.2-2 7.8 0 10"/>',
}

NAV_ICONS = {
    "appearance": '<circle cx="9" cy="9" r="6.2"/><circle cx="9" cy="9" r="2.2"/>',
    "content": '<path d="M3 4.5h12M3 9h12M3 13.5h8"/>',
    "behavior": '<rect x="3" y="3.5" width="12" height="11" rx="1.5"/><path d="M3 7h12M7 7v7.5"/>',
    "about": '<circle cx="9" cy="9" r="6.2"/><path d="M9 8v4M9 5.6v.1"/>',
}


def icon_chip(kind: str, size: int = 24, chip: int = 30) -> str:
    inner = round(size * 0.62)
    return (f'<div style="width:{chip}px;height:{chip}px;border-radius:9px;background:var(--chip-bg);'
            f'display:flex;align-items:center;justify-content:center;flex:none">'
            f'<svg width="{inner}" height="{inner}" viewBox="0 0 14 14" fill="none" '
            f'stroke="var(--chip-fg)" stroke-width="1.2">{ROW_ICONS[kind]}</svg></div>')


# ---------------------------------------------------------------------------
# Settings window shell — code: SettingsDraw (1024x720)
# ---------------------------------------------------------------------------

PAGES = [("appearance", "外观"), ("content", "显示内容"),
         ("behavior", "面板行为"), ("about", "关于")]


def settings_window(page_key: str, body: str, subtitle: str, dark: bool) -> str:
    nav = ""
    for key, label in PAGES:
        on = key == page_key
        bg = "background:var(--item-bg);" if on else ""
        fg = "var(--item-fg)" if on else "var(--detail)"
        nav += (f'<div style="width:200px;height:46px;border-radius:12px;{bg}display:flex;'
                f'align-items:center;gap:8px;padding:0 14px;color:{fg};margin-bottom:8px">'
                f'<svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" '
                f'stroke-width="1.4">{NAV_ICONS[key]}</svg>'
                f'<div style="font-size:15px">{label}</div></div>')
    heading = ""
    if page_key != "about":
        title = dict(PAGES)[page_key]
        heading = (
            '<div style="display:flex;align-items:flex-start;gap:16px;height:68px">'
            '<div style="width:4px;height:24px;border-radius:2px;background:var(--primary);'
            'margin-top:6px;flex:none"></div><div>'
            f'<div style="font-size:24px;color:var(--title);line-height:32px">{title}</div>'
            f'<div style="font-size:14px;color:var(--detail);line-height:20px">{subtitle}</div>'
            "</div></div>")
    toks = token_vars(dark)
    return f"""<div style="{toks};width:1024px;height:720px;background:var(--page);
border-radius:8px;overflow:hidden;position:relative;box-shadow:0 18px 50px rgba(0,0,0,0.35)">
<div style="position:absolute;top:0;left:0;right:0;height:58px;display:flex;
align-items:center;padding:0 24px">
<div style="width:22px;height:22px;border-radius:6px;background:var(--primary);display:flex;
align-items:center;justify-content:center;flex:none">
<svg width="13" height="13" viewBox="0 0 14 14" fill="none" stroke="#fff" stroke-width="1.3">
<rect x="1.5" y="3.5" width="11" height="7" rx="1"/></svg></div>
<div style="font-size:14px;color:var(--detail);margin-left:8px">轻谱 · 设置</div>
<div style="margin-left:auto;width:40px;height:30px;border-radius:8px;display:flex;
align-items:center;justify-content:center">
<svg width="13" height="13" viewBox="0 0 13 13" stroke="var(--detail)" stroke-width="1.6"
stroke-linecap="round"><path d="M1.5 1.5l10 10M11.5 1.5l-10 10"/></svg></div></div>
<div style="position:absolute;left:20px;top:76px;bottom:0;width:200px;display:flex;
flex-direction:column">{nav}
<div style="font-size:12px;color:var(--detail);margin-top:auto;margin-bottom:24px;padding:0 6px">
HSpec 1.0</div></div>
<div style="position:absolute;left:240px;top:28px;right:32px;bottom:0">{heading}{body}</div>
</div>"""


def card(title: str, inner: str, height: int = 0) -> str:
    """code: FillRound(card, 14s) — radius 14, cards spaced 14 apart."""
    h = f"height:{height}px;" if height else ""
    return (f'<div style="background:var(--card);border-radius:14px;padding:20px;'
            f'margin-bottom:14px;{h}">'
            f'<div style="font-size:18px;color:var(--title);line-height:24px;margin-bottom:14px">'
            f'{title}</div>{inner}</div>')


# ---------------------------------------------------------------------------
# Page bodies
# ---------------------------------------------------------------------------

def page_appearance(dark: bool) -> str:
    """code: PageLook — card gap 14, slider rows 38 pitch, thumbs tile-sized."""
    tile_w, tile_h = 227, 119          # (752-72)/3 x tile*84/161
    thumbs = ""
    for key, name, sel in [("plain", "纯文字", False), ("light", "轻卡片", False),
                           ("ethereal", "卡片", True)]:
        color = "var(--item-fg)" if sel else "var(--detail)"
        ring = ("box-shadow:0 0 0 2.2px var(--primary);border-radius:12px;" if sel else "")
        thumbs += (f'<div style="display:flex;flex-direction:column">'
                   f'<div style="{ring}">{theme_thumb(key, tile_w, tile_h)}</div>'
                   f'<div style="display:flex;align-items:center;height:22px;margin-top:8px">'
                   f'<div style="font-size:14px;color:{color}">{name}</div>'
                   f'<div style="margin-left:auto;margin-right:6px">{radio(sel)}</div></div></div>')
    theme = card("主题",
                 '<div style="font-size:14px;color:var(--detail);line-height:20px;'
                 'margin:-8px 0 10px">同一个信息模型，三种渲染层</div>'
                 f'<div style="display:flex;gap:16px">{thumbs}</div>',
                 height=256)
    accent = card("主色",
                  '<div style="font-size:15px;color:var(--title);line-height:22px;'
                  'margin:-8px 0 0">跟随壁纸取色'
                  f'<span style="float:right;margin-top:-2px">{switch(True)}</span></div>'
                  '<div style="font-size:13.5px;color:var(--detail);line-height:18px">'
                  '从壁纸采样主色，换算成 oklch 的 hue</div>'
                  f'<div style="margin:12px 0 0">{hue_strip(712)}</div>'
                  '<div style="display:flex;align-items:center;height:18px;margin-top:8px">'
                  '<div style="font-size:13.5px;color:var(--detail)">'
                  'Hue 250 · 蓝紫 · 与站内主题共用同一套 oklch 变量</div>'
                  '<div style="margin-left:auto;font-size:13.5px;color:var(--detail)">250°</div></div>',
                  height=172)
    rows = "".join(
        '<div style="display:flex;align-items:center;height:24px;margin-bottom:14px">'
        f'<div style="font-size:15px;color:var(--title);width:150px">{n}</div>'
        f'<div style="width:438px">{slider(t, 438)}</div>'
        f'<div style="margin-left:auto;width:56px;text-align:right;font-size:14px;'
        f'color:var(--detail)">{v}</div></div>'
        for n, t, v in [("卡片不透明度", 0.733, "84%"), ("圆角", 0.667, "16px")])
    panel = card("面板", rows, height=150)
    return settings_window("appearance", theme + accent + panel,
                           "面板画在桌面层上，这里决定它离壁纸有多近", dark)


def page_content(dark: bool) -> str:
    spec = [("cpu", "处理器", "型号 / 核心 / 线程", False),
            ("board", "主板与固件", "厂商 / 固件版本 / 启动方式 / 证书", True),
            ("mem", "内存", "品牌 / 型号 / 容量 / 频率", True),
            ("disk", "硬盘", "型号 / 容量 / 分区格式 / 总线", True),
            ("gpu", "显卡", "型号（PE 下 PCI ID 兜底）", True),
            ("net", "网卡", "类型 / MAC / IP / 速率 / SSID / 网关", False)]
    rows = ""
    for i, (icon, name, meta, on) in enumerate(spec):
        sep = "border-bottom:1px solid var(--divider);" if i < len(spec) - 1 else ""
        rows += (f'<div style="height:84px;display:flex;align-items:center;gap:16px;padding:0 10px;{sep}">'
                 f'{icon_chip(icon, 24, 40)}'
                 f'<div style="flex:1"><div style="font-size:16px;color:var(--title)">{name}</div>'
                 f'<div style="font-size:13.5px;color:var(--detail);margin-top:4px">{meta}</div></div>'
                 f'{switch(on)}</div>')
    body = f'<div style="background:var(--card);border-radius:12px;padding:12px 10px">{rows}</div>'
    return settings_window("content", body, "关掉的项目不再出现在面板上", dark)


def page_behavior(dark: bool) -> str:
    """NEW design — the deferred 面板行为 page."""
    corners = ""
    for pos, on in [("left:6px;top:6px", False), ("right:6px;top:6px", True),
                    ("left:6px;bottom:6px", False), ("right:6px;bottom:6px", False)]:
        ring = ("border:2px solid var(--primary)" if on
                else "border:1.5px solid var(--divider)")
        fill = "var(--primary)" if on else "transparent"
        corners += (f'<div style="position:absolute;{pos};width:44px;height:30px;border-radius:6px;'
                    f'{ring};background:{fill};box-sizing:border-box"></div>')
    picker = (f'<div style="width:230px;height:132px;border-radius:10px;background:var(--chip-bg);'
              f'position:relative;border:1px solid var(--divider)">{corners}'
              '<div style="position:absolute;inset:0;display:flex;align-items:center;'
              'justify-content:center;font-size:12px;color:var(--detail)">显示器</div></div>')
    pos = card("位置",
               '<div style="font-size:14px;color:var(--detail);margin:-6px 0 16px">'
               '面板停靠的角落与屏幕边距</div>'
               f'<div style="display:flex;gap:28px;align-items:center">{picker}'
               '<div style="display:flex;flex-direction:column;gap:10px">'
               '<div style="font-size:15px;color:var(--title)">右上角</div>'
               '<div style="font-size:13.5px;color:var(--detail)">Win+D 后随桌面一同隐藏</div>'
               '<div style="display:flex;align-items:center;gap:16px;margin-top:6px">'
               '<div style="font-size:15px;color:var(--title);width:70px">边距</div>'
               f'{slider(0.4, 200)}'
               '<div style="font-size:14px;color:var(--detail)">24px</div></div></div></div>')
    scale = card("缩放",
                 '<div style="font-size:14px;color:var(--detail);margin:-6px 0 16px">'
                 '整个面板的显示缩放，不改变信息结构</div>'
                 '<div style="display:flex;align-items:center;gap:16px">'
                 '<div style="font-size:15px;color:var(--title);width:110px">面板缩放</div>'
                 f'{slider(0.42, 300)}'
                 '<div style="font-size:14px;color:var(--detail)">120%</div></div>'
                 '<div style="display:flex;gap:24px;margin-top:18px;align-items:center">'
                 '<div style="font-size:13.5px;color:var(--detail);width:110px">跟随系统 DPI</div>'
                 f'{switch(True)}'
                 '<div style="font-size:13.5px;color:var(--detail)">关掉后固定用 100% 基准绘制</div></div>')
    chips = ""
    for label, on in [("15 秒", False), ("60 秒", True), ("5 分钟", False)]:
        st = ("border:1.5px solid var(--primary);color:var(--primary)" if on
              else "border:1.5px solid var(--divider);color:var(--detail)")
        chips += f'<div style="padding:8px 18px;border-radius:9px;{st};font-size:14px">{label}</div>'
    refresh = card("刷新间隔",
                   '<div style="font-size:14px;color:var(--detail);margin:-6px 0 16px">'
                   '硬件信息轮询周期；设备与壁纸事件始终即时刷新</div>'
                   f'<div style="display:flex;gap:12px">{chips}'
                   '<div style="font-size:13.5px;color:var(--detail);margin-left:8px;'
                   'align-self:center">可选 15 秒 – 30 分钟</div></div>')
    return settings_window("behavior", pos + scale + refresh,
                           "面板本体的行为，不用去托盘菜单里翻", dark)


def page_about(dark: bool) -> str:
    info = [("许可", "MIT License"),
            ("界面字体", "MiSans Medium（内嵌子集 908 字）"),
            ("数据来源", "SMBIOS / UEFI 变量 / 注册表 / 物理磁盘 IOCTL"),
            ("运行方式", "单文件 · 管理员 · 不联网 · 不写注册表")]
    rows = "".join(
        '<div style="display:flex;height:44px;align-items:center;border-bottom:1px solid var(--divider)">'
        f'<div style="font-size:14px;color:var(--detail);width:116px">{k}</div>'
        f'<div style="font-size:15px;color:var(--title)">{v}</div></div>' for k, v in info)
    body = (
        '<div style="margin-top:26px">'
        '<div style="font-size:24px;color:var(--title)">轻谱 Hydrogen Spec</div>'
        '<div style="font-size:13.5px;color:var(--detail);margin-top:10px">Version 1.0 · x64</div>'
        '<div style="font-size:14px;color:var(--detail);margin-top:6px">轻量、零依赖的桌面硬件信息面板</div>'
        f'<div style="background:var(--card);border-radius:12px;padding:8px 20px;margin-top:26px">{rows}</div>'
        '<div style="display:flex;gap:16px;margin-top:26px">'
        '<div style="width:156px;height:44px;border-radius:10px;background:var(--chip-bg);'
        'display:flex;align-items:center;justify-content:center;font-size:15px;color:var(--item-fg)">'
        'GitHub 仓库</div>'
        '<div style="width:168px;height:44px;border-radius:10px;background:var(--primary);'
        'display:flex;align-items:center;justify-content:center;font-size:15px;color:#fff">'
        '复制版本信息</div></div>'
        '<div style="font-size:12px;color:var(--detail);margin-top:34px">'
        '© 2026 Hydrogen Spec · 与轻键 / 轻韵同一套做法</div></div>')
    return settings_window("about", body, "", dark)


# ---------------------------------------------------------------------------
# Panel — code: RenderInternal, card mode (526dp, chip 30 round 9)
# ---------------------------------------------------------------------------

PANEL_ROWS = [
    ("cpu", "AMD Ryzen 7 5800H with Radeon Graphics", ["核心: 8    线程: 16"]),
    ("board", "主板: LNVNB161216", ["厂商: 联想", "固件: GSCN31WW", "启动: UEFI (Secure Boot)", "证书: PCA 2011"]),
    ("mem", "内存: 16 GB DDR4 ×2", ["品牌: 美光", "型号: 4ATF1G64HZ-3G2E1 8 GB 3200 MHz"]),
    ("gpu", "显卡: NVIDIA GeForce RTX 3050 Laptop GPU", []),
    ("gpu", "显卡: AMD Radeon Graphics", []),
    ("disk", "硬盘: 海力士 HFS512GDE9X084N", ["容量: 477 GB   格式: GPT   类型: NVME"]),
    ("net", "网卡: Realtek 8852BE Wireless LAN WiFi 6 PCI-E NIC",
     ["类型: 无线   MAC: E0-0A-F6-A2-BC-61", "IP: 192.168.31.30   速率: 1.2 Gbps",
      "SSID: Xiaomi_2L_5G   网关: 192.168.31.1"]),
]


def panel_card(hover_row: int = -1, copied_row: int = -1, dark: bool = False) -> str:
    rows = ""
    for i, (icon, title, details) in enumerate(PANEL_ROWS):
        hl = ""
        if i == hover_row:
            hl = "background:color-mix(in srgb, var(--item-bg) 60%, transparent);border-radius:8px;"
        if i == copied_row:
            hl = "background:color-mix(in srgb, var(--primary) 18%, transparent);border-radius:8px;"
        det = "".join(f'<div style="font-size:14px;color:var(--detail);line-height:21px">{d}</div>'
                      for d in details)
        toast = ""
        if i == copied_row:
            toast = ('<div style="margin-left:auto;align-self:center;font-size:12px;color:var(--primary);'
                     'background:color-mix(in srgb, var(--primary) 14%, transparent);'
                     'padding:3px 10px;border-radius:9px">已复制</div>')
        rows += (f'<div style="display:flex;gap:10px;padding:2px 4px;{hl}">'
                 f'{icon_chip(icon, 24, 30)}'
                 f'<div style="flex:1"><div style="font-size:16px;color:var(--title);'
                 f'line-height:24px">{title}</div>{det}</div>{toast}</div>')
    return (f'<div style="{token_vars(dark)};width:526px;background:color-mix(in srgb, '
            f'var(--card) 84%, transparent);border-radius:16px;padding:15px 14px;'
            f'backdrop-filter:blur(10px);box-shadow:0 10px 34px rgba(0,0,0,0.22)">'
            f'<div style="display:flex;flex-direction:column;gap:9px">{rows}</div></div>')


def plain_panel(light_text: bool) -> str:
    """纯文字 theme — text straight on the wallpaper (code: THEME_PLAIN)."""
    fg = "#ffffff" if light_text else "#1c1c1c"
    sub = "rgba(255,255,255,0.82)" if light_text else "rgba(28,28,28,0.72)"
    rows = ""
    for icon, title, details in PANEL_ROWS:
        det = "".join(f'<div style="font-size:14px;color:{sub};line-height:21px">{d}</div>'
                      for d in details)
        rows += (f'<div style="display:flex;gap:10px">'
                 f'<svg width="18" height="18" viewBox="0 0 14 14" fill="none" stroke="{fg}" '
                 f'stroke-width="1.2" style="margin-top:3px;flex:none">{ROW_ICONS[icon]}</svg>'
                 f'<div><div style="font-size:16px;color:{fg};line-height:24px">{title}</div>'
                 f'{det}</div></div>')
    return (f'<div style="width:480px;padding:4px"><div style="display:flex;'
            f'flex-direction:column;gap:9px">{rows}</div></div>')


WALLPAPERS = {
    "light": "linear-gradient(140deg,#cfd8ea 0%,#e8e2d8 45%,#d5d9e6 100%)",
    "dark": "linear-gradient(140deg,#20242e 0%,#2c2a28 50%,#1c2028 100%)",
    "busy": ("linear-gradient(115deg,#1d2430 0%,#3c4658 26%,#c8b9a2 48%,#e6ddd0 58%,"
             "#2b3340 82%,#171b22 100%)"),
}


# ---------------------------------------------------------------------------
# Scenes — each returns (html, width, height)
# ---------------------------------------------------------------------------

def scene_settings(light_html: str, dark_html: str) -> str:
    return (f'<div style="width:2240px;height:880px;padding:56px;background:#151519;'
            f'display:flex;gap:64px">'
            f'<div><div style="font-size:22px;color:#9aa0ab;margin-bottom:18px">浅色</div>{light_html}</div>'
            f'<div><div style="font-size:22px;color:#9aa0ab;margin-bottom:18px">深色</div>{dark_html}</div>'
            f'</div>')


def scene_panel(left_label: str, left_html: str, right_label: str, right_html: str,
                wallpaper: str, footer: str, w: int = 1120, h: int = 800) -> str:
    return (f'<div style="width:{w}px;height:{h}px;padding:44px;background:{wallpaper};'
            f'position:relative;display:flex;gap:36px">'
            f'<div><div style="font-size:17px;color:rgba(255,255,255,0.88);background:rgba(0,0,0,0.30);'
            f'display:inline-block;padding:4px 12px;border-radius:8px;margin-bottom:16px">{left_label}</div>'
            f'{left_html}</div>'
            f'<div><div style="font-size:17px;color:rgba(255,255,255,0.88);background:rgba(0,0,0,0.30);'
            f'display:inline-block;padding:4px 12px;border-radius:8px;margin-bottom:16px">{right_label}</div>'
            f'{right_html}</div>'
            f'<div style="position:absolute;bottom:18px;left:44px;font-size:14px;'
            f'color:rgba(255,255,255,0.62)">{footer}</div></div>')


def scene_interaction() -> str:
    default = panel_card(dark=True)
    hover = panel_card(hover_row=1, dark=True)
    copied = panel_card(copied_row=3, dark=True)
    legend = (
        '<div style="background:#101014;border-radius:14px;padding:22px 26px;color:#c6cad2;'
        'width:526px"><div style="font-size:19px;color:#fff;margin-bottom:14px">'
        '面板可点击 · 状态机</div><div style="font-size:14px;line-height:24px">'
        '<div><b style="color:#fff;font-weight:500">hover 行</b> — 行区域 itemBg 60% 圆角 8，即时跟随</div>'
        '<div><b style="color:#fff;font-weight:500">点击行</b> — 该节信息复制到剪贴板，'
        '行内闪现「已复制」1 秒淡出</div>'
        '<div><b style="color:#fff;font-weight:500">点击空白</b> — 打开设置窗口</div>'
        '<div><b style="color:#fff;font-weight:500">右键</b> — 托盘同款菜单（设置… / 刷新 / 退出）</div>'
        '<div style="margin-top:10px;color:#8b93a1">命中区表布局时重建（同设置窗口 HitAdd）；'
        '桌面浮层不进 Tab 序，键盘不抢焦点。</div></div></div>')
    return (f'<div style="width:1760px;height:1060px;padding:48px;background:#0c0c10;'
            f'display:flex;gap:36px">'
            f'<div><div style="font-size:18px;color:#8b93a1;margin-bottom:14px">默认</div>{default}</div>'
            f'<div><div style="font-size:18px;color:#8b93a1;margin-bottom:14px">hover 主板行</div>{hover}</div>'
            f'<div style="display:flex;flex-direction:column;gap:20px">'
            f'<div><div style="font-size:18px;color:#8b93a1;margin-bottom:14px">点击显卡行 · 复制反馈</div>{copied}</div>'
            f'{legend}</div></div>')


def scene_upgrade() -> str:
    before = panel_card(dark=True)
    after = panel_card(hover_row=1, dark=True)
    notes = (
        '<div style="background:#101014;border-radius:14px;padding:22px 26px;color:#c6cad2;'
        'width:526px;margin-top:22px"><div style="font-size:19px;color:#fff;margin-bottom:12px">'
        '升级方向</div><div style="font-size:14px;line-height:24px">'
        '<div>1. <b style="color:#fff;font-weight:500">行 hover 底</b> — 为可点击铺路，'
        'itemBg 60% 圆角 8</div>'
        '<div>2. <b style="color:#fff;font-weight:500">卡片投影</b> — 0 10 34 柔和阴影，'
        '从壁纸上「浮」起来</div>'
        '<div>3. <b style="color:#fff;font-weight:500">图标 chip 主色化</b> — chipFg 随 hue 变</div>'
        '<div>4. <b style="color:#fff;font-weight:500">行内主色提示</b> — 关键值用 primary 强调</div>'
        '<div style="margin-top:10px;color:#8b93a1">布局、字阶、信息结构不动 —— 只换画法，'
        '与三档主题「同一个信息模型」原则一致。</div></div></div>')
    return (f'<div style="width:1300px;height:1080px;padding:48px;background:{WALLPAPERS["busy"]};'
            f'display:flex;gap:44px">'
            f'<div><div style="font-size:18px;color:rgba(255,255,255,0.88);'
            f'background:rgba(0,0,0,0.30);display:inline-block;padding:4px 12px;border-radius:8px;'
            f'margin-bottom:16px">现状（Ethereal）</div>{before}</div>'
            f'<div><div style="font-size:18px;color:rgba(255,255,255,0.88);'
            f'background:rgba(0,0,0,0.30);display:inline-block;padding:4px 12px;border-radius:8px;'
            f'margin-bottom:16px">升级态</div>{after}{notes}</div></div>')


SCENES = {
    "settings-theme": lambda: (scene_settings(page_appearance(False), page_appearance(True)), 2240, 880),
    "settings-content": lambda: (scene_settings(page_content(False), page_content(True)), 2240, 880),
    "settings-behavior": lambda: (scene_settings(page_behavior(False), page_behavior(True)), 2240, 880),
    "about": lambda: (scene_settings(page_about(False), page_about(True)), 2240, 880),
    "panel-light": lambda: (
        scene_panel("当前 · 文字直贴壁纸", plain_panel(False),
                    "Ethereal · 卡片 + 主色图标", panel_card(),
                    WALLPAPERS["light"], "同一个信息模型，仅渲染层不同 · 480/526 dp · 100% 缩放"), 1120, 800),
    "panel-dark": lambda: (
        scene_panel("当前 · 文字直贴壁纸", plain_panel(True),
                    "Ethereal · 卡片 + 主色图标", panel_card(dark=True),
                    WALLPAPERS["dark"], "深色壁纸：浅字直贴 vs 半透明卡片"), 1120, 800),
    "panel-busy": lambda: (
        scene_panel("纯文字 · 明暗交界处丢字", plain_panel(True),
                    "Ethereal · 卡片把对比度钉死", panel_card(),
                    WALLPAPERS["busy"], "复杂壁纸：卡片版在任何区域都保持可读"), 1120, 800),
    "panel-interaction": lambda: (scene_interaction(), 1760, 1060),
    "panel-upgrade": lambda: (scene_upgrade(), 1300, 1080),
}

# ---------------------------------------------------------------------------
# Render
# ---------------------------------------------------------------------------

def render(name: str) -> None:
    factory = SCENES[name]
    html, w, h = factory()
    png_name = name + ".png"
    doc = (f"<!DOCTYPE html><html><head><meta charset='utf-8'><style>{CSS}</style>"
           f"</head><body>{html}</body></html>")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    html_path = BUILD_DIR / f"{name}.html"
    html_path.write_text(doc, encoding="utf-8")
    out = OUT_DIR / png_name
    cmd = [str(EDGE), "--headless=new", "--disable-gpu",
           f"--screenshot={out.as_posix()}", "--force-device-scale-factor=2",
           f"--window-size={w},{h}", html_path.as_uri()]
    subprocess.run(cmd, check=True, capture_output=True)
    print(f"  ok  {png_name}  ({w * 2}x{h * 2})")


def main() -> int:
    if not FONT.exists():
        print(f"font missing: {FONT}", file=sys.stderr)
        return 2
    if not EDGE.exists():
        print(f"edge missing: {EDGE}", file=sys.stderr)
        return 2
    names = sys.argv[1:] or list(SCENES)
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        # allow substring matching for convenience
        matched = [k for k in SCENES if any(u in k for u in names)]
        if not matched:
            print(f"unknown scene(s): {unknown}", file=sys.stderr)
            return 2
        names = matched
    print("rendering mockups -> docs/mockup/")
    for n in names:
        render(n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
