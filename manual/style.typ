#let note(title, body) = block(
  width: 100%,
  inset: 8pt,
  radius: 4pt,
  stroke: rgb("#c7d2fe"),
  fill: rgb("#eef2ff"),
)[
  *#title* \
  #body
]

#let warning(title, body) = block(
  width: 100%,
  inset: 8pt,
  radius: 4pt,
  stroke: rgb("#fecaca"),
  fill: rgb("#fff1f2"),
)[
  *#title* \
  #body
]

#let ui(body) = text(
  font: ("Source Han Sans", "Noto Sans CJK SC", "Droid Sans Fallback", "DejaVu Sans"),
  weight: "medium",
)[#body]

#let menu(body) = ui(body)

#let use-screenshots = sys.inputs.at("screenshots", default: "false") == "true"

#let screenshot(file, caption) = figure(
  if use-screenshots {
    image("assets/" + file, width: 100%)
  } else {
    rect(
    width: 100%,
    height: 92pt,
    radius: 4pt,
    stroke: rgb("#94a3b8"),
    fill: rgb("#f8fafc"),
    )[
      #align(center + horizon)[
        #text(fill: rgb("#64748b"))[
          截图占位：`manual/assets/#file`
        ]
      ]
    ]
  },
  caption: caption,
)
