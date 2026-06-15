# llmchat Theme Reference

Set theme via `"theme": "name"` in `~/.config/llmchat/config.json`.

## Color Index

| Code | Color   |
|------|---------|
| 0    | black   |
| 1    | red     |
| 2    | green   |
| 3    | yellow  |
| 4    | blue    |
| 5    | magenta |
| 6    | cyan    |
| 7    | white   |
| 8    | gray    |
| -1   | terminal default |

## Color Pair Usage

| Pair | Element                         | Config field        |
|------|---------------------------------|---------------------|
| 1    | `--[ YOU ]--` + message         | `user_fg` / `user_bg` |
| 2    | `--[ ASSISTANT ]--` + message   | `assistant_fg` / `assistant_bg` |
| 3    | `--[ SYSTEM ]--` + message      | `system_fg` / `system_bg` |
| 4    | `--[ TOOL ]--` + message        | `tool_fg` / `tool_bg` |
| 5    | `--[ ERROR ]--` + message       | `error_fg` / `error_bg` |
| 6    | Status bar + scroll indicator   | `status_fg` / `status_bg` |
| 7    | Input box border + title        | `separator_fg` / `separator_bg` |
| 8    | `>` prompt + typed text         | `input_fg` / `input_bg` |
| 9    | "GENERATING..." animation       | (auto, cycles through theme accents) |

## All Themes

| Theme | user | asst | sys | tool | err | status fg/bg | input | sep | notes |
|---|---|---|---|---|---|---|---|---|---|
| default | yel | cya | blu | mag | red | wht / blu | def | gry | |
| dark | yel | cya | blu | mag | red | blk / blu | def | gry | |
| monokai | cya | grn | mag | yel | red | wht / mag | grn | gry | |
| gruvbox | yel | grn | cya | mag | red | blk / yel | def | gry | |
| dracula | mag | cya | blu | grn | red | wht / mag | def | gry | |
| casino | yel | grn | mag | cya | red | yel / red | def | gry | ¹ |
| tokyo-night | wht | cya | mag | grn | red | cya / blu | cya | gry | |
| catppuccino | mag | grn | cya | yel | red | blk / mag | def | gry | |
| nord | cya | wht | blu | mag | red | wht / blu | cya | gry | |
| ayu | yel | grn | cya | mag | red | blk / cya | def | gry | |
| one-dark | cya | grn | blu | mag | red | wht / blu | cya | gry | |
| solarized-dark | yel | cya | blu | mag | red | blk / yel | def | gry | |
| everforest | yel | grn | cya | mag | red | wht / grn | def | gry | |
| tomorrow-night | yel | wht | blu | mag | red | blk / wht | def | gry | |
| oceanic-next | yel | grn | cya | mag | red | wht / blu | grn | gry | |
| palenight | mag | cya | blu | grn | red | wht / mag | def | gry | |
| material | yel | cya | blu | grn | red | blk / cya | def | gry | |
| github-dark | wht | cya | blu | grn | red | wht / blu | def | gry | |
| cyberpunk | cya | grn | mag | yel | red | blk / mag | def | gry | |
| neon | mag | grn | cya | yel | red | blk / grn | def | gry | |
| synthwave | mag | cya | yel | grn | red | wht / mag | def | gry | |
| outrun | yel | mag | cya | grn | red | blk / mag | def | gry | |
| forest | yel | grn | blu | cya | red | wht / grn | def | gry | |
| arctic | wht | cya | blu | mag | red | blk / cya | def | gry | |
| desert | yel | wht | cya | mag | red | yel / blu | def | gry | |
| mocha | yel | grn | cya | mag | red | wht / mag | def | gry | |
| latte | blk | blu | mag | red | red | blk / yel | blk | wht | ² |
| frappe | wht | grn | cya | mag | red | blk / wht | def | gry | |
| macchiato | wht | mag | cya | grn | red | wht / blu | def | gry | |
| aurora | cya | grn | mag | yel | red | wht / mag | def | gry | |
| rose-pine | yel | mag | cya | grn | red | wht / mag | wht | gry | |
| blood | wht | red | yel | mag | red | wht / red | def | gry | |
| ocean | wht | cya | blu | grn | red | wht / blu | def | gry | |
| midnight | cya | wht | mag | yel | red | wht / blu | cya | gry | |
| hacker | grn | wht | yel | cya | red | grn / blk | def | grn | |
| sunset | yel | red | mag | yel | red | yel / blu | def | gry | |
| iceberg | cya | wht | blu | mag | red | blk / cya | def | gry | |

**Abbreviations:** yel=yellow, cya=cyan, blu=blue, mag=magenta, red=red, grn=green, wht=white, blk=black, gry=gray, def=default

¹ Casino enables `slot_machine_animation` + `casino_status_bar`  
² Latte is a light theme (black text on default terminal light background); all others are dark

## Adding a Custom Theme

Add an entry to `src/themes.json` (or copy to `~/.config/llmchat/themes.json`). No C++ code changes needed.

```json
"my-theme": {
  "user_fg": 7, "user_bg": -1,
  "assistant_fg": 6, "assistant_bg": -1,
  "system_fg": 4, "system_bg": -1,
  "tool_fg": 5, "tool_bg": -1,
  "error_fg": 1, "error_bg": -1,
  "status_fg": 6, "status_bg": 4,
  "input_fg": 6, "input_bg": -1,
  "separator_fg": 8, "separator_bg": -1,
  "slot_machine_animation": false,
  "casino_status_bar": false
}
```
