#set document(
  title: "LabelQt 用户手册",
  author: "LabelQt Project",
)
#set page(
  paper: "a4",
  margin: (x: 21mm, y: 20mm),
  numbering: "1",
)
#set text(
  font: ("Source Han Serif", "Noto Serif CJK SC", "Droid Sans Fallback", "DejaVu Sans"),
  lang: "zh",
  size: 10.5pt,
)
#set par(justify: true, leading: 0.72em)
#set heading(numbering: "1.1")
#show heading: set text(font: ("Source Han Sans", "Noto Sans CJK SC", "Droid Sans Fallback"), weight: "bold")
#show raw: set text(font: ("Source Code Pro", "DejaVu Sans Mono"))

#let product-name = "LabelQt"
#let manual-version = "v0.1.0"
#let manual-date = "2026-07-10"

#import "style.typ": note

#align(center)[
  #text(font: ("Source Han Sans", "Noto Sans CJK SC", "Droid Sans Fallback"), size: 24pt, weight: "bold")[LabelQt 用户手册] \
  #v(8pt)
  #text(font: ("Source Han Sans", "Noto Sans CJK SC", "Droid Sans Fallback"), size: 12pt)[面向 LabelPlus 文本工程的跨平台编辑、校对与协作工具] \
  #v(16pt)
  #text(fill: rgb("#64748b"))[#manual-version · #manual-date] \
  #v(6pt)
  #text(size: 9pt, fill: rgb("#64748b"))[截图来自 Fedora 44 KDE；不同平台和主题下的视觉效果会有所不同。]
]

#v(24pt)
#note("阅读对象", [
  这份手册面向已经了解 LabelPlus 基本概念的用户。它不会重复解释漫画翻译标注的基础术语，而是重点说明 LabelQt 的界面、操作方式和新增工作流。
])

#note("截图环境", [
  本手册中的界面截图来自 Fedora 44 KDE。Windows、其他 Linux 桌面环境或不同 Qt 主题下，按钮、标题栏、滚动条和配色可能与截图不完全一致，但功能位置和操作逻辑相同。
])

#outline(title: "目录")
#pagebreak()

#include "chapters/intro.typ"
#include "chapters/quick-start.typ"
#include "chapters/main-window.typ"
#include "chapters/editing.typ"
#include "chapters/page-management.typ"
#include "chapters/proofreading.typ"
#include "chapters/merge.typ"
#include "chapters/automation.typ"
#include "chapters/preferences.typ"
#include "chapters/faq.typ"
