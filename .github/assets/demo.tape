# README demo. Regenerate from the repository root with:
#   vhs .github/assets/demo.tape
# The hidden setup copies the photo to /tmp/photoc-demo and removes GPS tags
# from the copy, so the original's folder and location never appear in the GIF.

Output .github/assets/demo.gif

Require photoc

Set Shell "zsh"
Set FontSize 16
Set Width 1000
Set Height 620
Set Padding 24
Set Theme "Catppuccin Mocha"
Set TypingSpeed 60ms

Hide
Type `rm -rf /tmp/photoc-demo && mkdir /tmp/photoc-demo && cp "$HOME/Downloads/my photography/como/DSC09087.jpeg" /tmp/photoc-demo/ && cd /tmp/photoc-demo && photoc scrub DSC09087.jpeg --gps --in-place --quiet; clear`
Enter
Sleep 1s
Show

Sleep 500ms
Type "photoc exif DSC09087.jpeg"
Sleep 600ms
Enter
Sleep 6s
