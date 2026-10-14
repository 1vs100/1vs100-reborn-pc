Avatar Editor (PC recompilation) for the 1 vs 100 restoration
=============================================================

Source: https://github.com/panedivitasei/AvatarEditorRecomp (commit c791019),
GPL-3.0. AvatarEditorRecomp-c791019.zip is that source unchanged; its LICENSE
and third-party licenses are inside. This is a separate program from the game.

assets\ holds files from your own Xbox 360 dashboard 2.0.12581.0
($SystemUpdate: AvatarEditor.xex; DashPack: Avatar*.Avatar and the ten
*.AvatarAnimation files; AvatarAsset: the two .toc packs) plus the full
XenonCLatin.xtt / XenonJKLatin.xtt fonts. Do not share them.

Build: run build_avatar_editor.cmd (log in outputs\diagnostics).

Your Avatar items (closet)
--------------------------
Launcher > Play > "Import Avatar items...", or Import-AvatarCloset.cmd here.
Choose any folder that holds Xbox 360 Avatar item packages you own; every
file inside it is checked and only Avatar items are copied. Ways to get them:
  - Your console's hard drive: copy the Content folder (it holds one folder
    per profile, then per game, with Avatar items in 00009000 folders) to the
    PC with a USB drive, FTP or a tool such as Velocity or Horizon, then
    choose that copy.
  - Items you saved as package files: choose the folder holding them.
Items go to closet\ (one .bin and icon per item, closet_index.tsv). Run it
again to add more; existing items stay. The editor and the game read the
closet the next time they start.

Do not share the closet folder: the items are your own downloads and belong
to their publishers. In an online show, players who have the same item see
it; players who do not see the standard item in its place.
