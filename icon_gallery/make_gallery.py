#!/usr/bin/env python3
"""Generate gallery.html — a browsable preview of all extracted CS2 icon SVGs."""
import glob
import html
import string
import os
import sys

ICONS_ROOT = "icon_gallery/icons"
OUT = "icon_gallery/gallery.html"
CURRENT_MENU_ICON = "panorama/images/icons/ui/bug"
# icons actually referenced by the CS2 main menu shell (panorama/layout/mainmenu.vxml)
MAINMENU_ICONS = {
    "panorama/images/icons/ui/home",            # 房子 - 主菜单首页
    "panorama/images/icons/ui/watch_tv",        # 电视机 - 观战/直播
    "panorama/images/icons/ui/power",           # 电源 - 退出
    "panorama/images/icons/ui/settings",        # 齿轮 - 设置
    "panorama/images/icons/ui/check",
    "panorama/images/icons/ui/leave",
    "panorama/images/icons/ui/resumegame",
    "panorama/images/icons/ui/overwatch",
    "panorama/images/icons/ui/voteteamswitch",
    "panorama/images/icons/ui/report_server",
    "panorama/images/icons/ui/vacnet",
    "panorama/images/icons/ui/gc_connecting_inner",
    "panorama/images/icons/ui/gc_connecting_outer",
}


def s2r_url(rel_path_no_ext):
    # s2r://panorama/... + .vsvg (extensionless entries in the VPK map to .vsvg)
    return "s2r://" + rel_path_no_ext.replace(os.sep, "/") + ".vsvg"


def main():
    entries = []
    for p in sorted(glob.glob(ICONS_ROOT + "/**/*.svg", recursive=True)):
        rel = os.path.relpath(p, ICONS_ROOT)
        no_ext = rel[:-4]
        category = os.path.dirname(no_ext).replace(os.sep, "/").replace(
            "panorama/images/", "").replace("/", " › ")
        if not category:
            category = "icons (root)"
        entries.append((rel, no_ext, category, os.path.basename(no_ext)))

    categories = []
    for c in ["icons (root)", "icons › ui", "icons › equipment", "icons › flags",
              "icons › skillgroups", "icons › scoreboard", "icons › xp",
              "hud", "hud › deathnotice", "hud › deathpanel", "hud › gameinstructor",
              "hud › healtharmor", "hud › radar", "hud › radar › mapoverview",
              "hud › reticle", "hud › rosettaselector", "hud › scope",
              "hud › teamcounter", "hud › voicestatus", "hud › winpanel"]:
        if any(e[2] == c for e in entries):
            categories.append(c)

    tiles = []
    for rel, no_ext, category, name in entries:
        current = no_ext == CURRENT_MENU_ICON
        is_mainmenu = no_ext in MAINMENU_ICONS
        tiles.append(
            '<div class="tile" data-cat="{cat}" data-name="{name}" data-url="{url}">'
            '<div class="imgwrap"><img loading="lazy" src="{src}" alt="{name}"></div>'
            '<div class="name">{name}</div>'
            '<div class="url" title="点击复制 s2r 路径">{url}</div>'
            '{cur}{mm}</div>'.format(
                cat=html.escape(category),
                name=html.escape(name),
                src=html.escape("icons/" + rel.replace(os.sep, "/")),
                url=html.escape(s2r_url(no_ext)),
                cur='<div class="current">当前菜单图标</div>' if current else "",
                mm='<div class="mainmenu">主菜单在用</div>' if is_mainmenu else "",
            ))

    cat_buttons = "\n".join(
        f'<button class="catbtn" data-cat="{html.escape(c)}">{html.escape(c)}'
        f'<span class="cnt">{sum(1 for e in entries if e[2] == c)}</span></button>'
        for c in categories)

    html_doc = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>CS2 图标画廊 — Osiris 菜单图标替换候选</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; background: #16181d; color: #e8eaf0;
         font: 14px/1.45 "Segoe UI", "Microsoft YaHei", sans-serif; }
  header { position: sticky; top: 0; z-index: 10; background: #1d2027;
           border-bottom: 1px solid #2c313b; padding: 10px 18px; }
  h1 { font-size: 17px; margin: 0 0 8px; }
  h1 small { color: #9aa3b2; font-weight: normal; font-size: 12px; }
  .note { color: #8fd0a5; font-size: 12px; margin: 4px 0 8px; }
  #search { width: 100%; max-width: 420px; padding: 7px 10px; border-radius: 6px;
            border: 1px solid #3a4150; background: #12141a; color: #e8eaf0; }
  #cats { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 8px; }
  .catbtn { padding: 4px 10px; border-radius: 14px; border: 1px solid #3a4150;
            background: #23262e; color: #c6ccd8; cursor: pointer; font-size: 12px; }
  .catbtn:hover { border-color: #5a80ff; }
  .catbtn.active { background: #3b5bd6; border-color: #5a80ff; color: #fff; }
  .catbtn .cnt { opacity: .6; margin-left: 5px; font-size: 11px; }
  main { padding: 14px 18px 60px; }
  #grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(128px, 1fr)); gap: 10px; }
  .tile { background: #1f2229; border: 1px solid #2c313b; border-radius: 8px;
          padding: 8px; text-align: center; }
  .tile:hover { border-color: #5a80ff; }
  .tile.current { border: 2px solid #e5a13d; }
  .imgwrap { height: 72px; display: flex; align-items: center; justify-content: center;
             border-radius: 6px;
             background-image: repeating-conic-gradient(#2a2e37 0 25%, #22252d 0 50%);
             background-size: 16px 16px; }
  .imgwrap img { max-width: 64px; max-height: 64px; }
  .name { margin-top: 6px; font-size: 11px; color: #dfe3ec; word-break: break-all; }
  .url { margin-top: 3px; font-size: 9.5px; color: #7f8899; word-break: break-all;
         cursor: copy; }
  .url:hover { color: #aeb8cc; }
  .current { margin-top: 4px; font-size: 10px; color: #e5a13d; font-weight: bold; }
  .mainmenu { margin-top: 4px; font-size: 10px; color: #5aa8ff; font-weight: bold; }
  .hidden { display: none !important; }
  #status { color: #9aa3b2; font-size: 12px; margin: 6px 0; }
  footer { position: fixed; bottom: 0; left: 0; right: 0; background: #1d2027;
           border-top: 1px solid #2c313b; padding: 6px 18px; font-size: 11px; color: #8b93a5; }
</style>
</head>
<body>
<header>
  <h1>CS2 图标画廊 <small>— 从 game/csgo/pak01_dir.vpk 提取，用于替换菜单 debug 虫子图标</small></h1>
  <div class="note">替换方法：把 <code>Source/UI/Panorama/PanoramaGUI.h</code> 第 135 行的
  <code>src: "s2r://panorama/images/icons/ui/bug.vsvg"</code> 换成所选图标的 s2r 路径（点击图标路径即可复制）。</div>
  <input id="search" type="text" placeholder="搜索图标名，如 bomb / crosshair / settings…">
  <div id="cats">$cats</div>
  <div id="status"></div>
</header>
<main><div id="grid">$tiles</div></main>
<footer>共 $total 个 SVG 图标 · 提取自 CS2 pak01_dir.vpk · 工具脚本 icon_gallery/vpk_extract.py</footer>
<script>
  var search = document.getElementById('search');
  var tiles = Array.prototype.slice.call(document.querySelectorAll('.tile'));
  var activeCat = null;
  function applyFilter() {
    var q = search.value.trim().toLowerCase();
    var shown = 0;
    tiles.forEach(function (t) {
      var okCat = !activeCat || t.getAttribute('data-cat') === activeCat;
      var okQ = !q || t.getAttribute('data-name').toLowerCase().indexOf(q) >= 0;
      t.classList.toggle('hidden', !(okCat && okQ));
      if (okCat && okQ) shown++;
    });
    document.getElementById('status').textContent = '显示 ' + shown + ' / ' + tiles.length + ' 个图标';
  }
  search.addEventListener('input', applyFilter);
  var btns = document.querySelectorAll('.catbtn');
  btns.forEach(function (b) {
    b.addEventListener('click', function () {
      var cat = b.getAttribute('data-cat');
      if (activeCat === cat) { activeCat = null; }
      else { activeCat = cat; }
      btns.forEach(function (x) { x.classList.toggle('active', x.getAttribute('data-cat') === activeCat); });
      applyFilter();
    });
  });
  document.getElementById('grid').addEventListener('click', function (e) {
    var el = e.target.closest('.url');
    if (!el) return;
    var url = el.closest('.tile').getAttribute('data-url');
    if (navigator.clipboard) navigator.clipboard.writeText(url);
    el.style.color = '#8fd0a5';
    setTimeout(function () { el.style.color = ''; }, 900);
  });
  applyFilter();
</script>
</body>
</html>"""

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(string.Template(html_doc).substitute(
            cats=cat_buttons, tiles="\n".join(tiles), total=len(entries)))
    print(f"wrote {OUT} with {len(entries)} icons, {len(categories)} categories")


if __name__ == "__main__":
    main()
