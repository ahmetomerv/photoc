# README demo. Regenerate from the repository root with:
#   vhs .github/assets/demo.tape
# query is read-only; the hidden setup only changes into the parent folder so
# the GIF shows short relative paths.

Output .github/assets/demo.gif

Require photoc

Set Shell "zsh"
Set FontSize 16
Set Width 1000
Set Height 320
Set Padding 24
Set Theme "Catppuccin Mocha"
Set TypingSpeed 55ms

Hide
Type `setopt interactivecomments; cd "$HOME/Downloads/my photography"; clear`
Enter
Sleep 1s
Show

Sleep 500ms
Type `# Low-light shots from the Como trip`
Enter
Sleep 300ms
Type `photoc query como --iso ">=800"`
Sleep 500ms
Enter
Sleep 4s

Type `# Wide-angle shots stopped down to f/5.6 or more`
Enter
Sleep 300ms
Type `photoc query como --focal "<=10" --aperture ">=5.6"`
Sleep 500ms
Enter
Sleep 5s
