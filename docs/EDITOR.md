# Foundry Editor

One scene, one set of commands, several ways to edit it:

| Tool | Who | What |
|---|---|---|
| `foundry_editor.exe scene.fscene` | you (mouse + keyboard) | live 3D editor with entity list, properties, console |
| `fscene scene.fscene -c "..."` | Claude / scripts / CI | headless command-line editing (no GPU, builds natively too) |
| any text editor | either | the scene file is plain text |

The editor autosaves after every change and **watches the file**: edits made by `fscene` or a text editor
appear within ~0.3 s. If the file on disk is invalid the editor keeps your current scene, shows the error,
and refuses to overwrite the file until you press Ctrl+S.

## The scene file
One command per line, `#` starts a comment. The file *is* a list of commands, so anything you can type in
the console you can also write in the file.

    sun dir=-0.8,-0.7,-0.45 color=255,242,220 intensity=2.2
    ambient sky=40,54,82 ground=15,13,13
    shadow on=1 center=0,0,0 radius=14
    background color=22,26,36
    camera pos=7,4.5,8 target=0,1,0 fov=50
    plane floor pos=0,0,0 rot=0,0,0 scale=24,1,24 color=150,150,155
    cube bench pos=0,0.5,0 rot=0,0,0 scale=4,1,1.6 color=150,100,60
    light lamp pos=1,3,1.5 color=255,150,90 range=10 intensity=14

Transform order: scale, then rotate about X, Y, Z (degrees), then translate. Kinds: `cube sphere cylinder cone plane light`
(all unit size, centred; cylinder/cone axis is Y).

## Commands (`help` prints this too)
    add <kind> [name] [key=val ...]     <kind> [name] [key=val ...]        (shorthand)
    set <name> key=val ...              keys: pos rot scale color visible name; lights: range intensity
    move <name> x,y,z                   rotate <name> x,y,z                scale <name> n | x,y,z
    duplicate <name> [new] [offset=x,y,z]   delete <name>   rename <old> <new>   clear
    sun dir= color= intensity=          ambient sky= ground=               background color=
    shadow on= center= radius=          camera pos= target= fov=
    list | get <name> | undo | redo | help

Values: vectors `x,y,z` (a single number fills all three, so `scale=2` is `2,2,2`); colours `r,g,b[,a]` or `#rrggbb`.
Errors say what is wrong (`no entity named 'x'`, `unknown key 'foo' (valid: ...)`, `file:line:` when loading).

## fscene (command line)
    fscene scene.fscene                      print the scene
    fscene scene.fscene -c "cube crate pos=2,0.5,1" -c "set crate color=#3c78dc"
    fscene scene.fscene --script edits.txt   run commands from a file
Stops at the first error and saves nothing unless `--keep-going`; `--no-save` previews.

## Seeing a result without a window session
    foundry_editor.exe scene.fscene --render out.ppm [--size 1280x720] [--select name] [-c "cmd"]
Renders one frame through the scene's saved camera and exits. Commands are applied but not saved unless `--save`.
(On a headless Linux box: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a wine64 foundry_editor.exe ...`.)

## Editor controls
    mouse   left click select | right drag orbit | middle drag pan | wheel zoom
    add     1 cube  2 sphere  3 cylinder  4 cone  5 plane  6 light   (at the view target)
    edit    arrows move X/Z | PgUp/PgDn move Y | Q/E rotate | - / = scale | Shift = fine
            Del delete | Ctrl+D duplicate | Ctrl+Z / Ctrl+Y undo / redo
    view    F focus selection | C save view as scene camera | G grid | [ ] cycle selection
    console Tab or /  type a command, Enter runs it, Esc leaves, Up/Down history
    other   Ctrl+S save now | H help | F12 screenshot (foundry_shot.ppm)

## Not yet
Parenting/hierarchy, imported models (glTF) and textures, gizmos for dragging in the viewport,
multi-select, a game-object/behaviour system. Scenes are static; physics hooks come with the engine's physics layer.
