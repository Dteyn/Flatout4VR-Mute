FlatOut 4 VR Mute v0.4.0
=========================
https://github.com/Dteyn/Flatout4VR-Mute

For FlatOut 4: Total Insanity VR v1.87 (Windows x64), aka FlatOut 4 VR:
https://store.steampowered.com/app/3844750/FlatOut_4_Total_Insanity_VR/

INSTALL
=======

1. Extract the whole ZIP to one folder (Downloads works fine).
2. Run FlatOut4VR-Mute-Installer.exe.
3. Follow the setup wizard. Most Steam installs won't need admin rights,
   but if Windows blocks a required write, the installer will offer to
   restart as administrator.

The release folder contains:

FlatOut4VR-Mute-Installer.exe
fl4tout_voip_mute.dll
fl4tout_voip_mute.ini
loaders\
  version.dll
  winhttp.dll
readme.txt

Before installing anything, Setup checks that the DLLs and INI in this
folder haven't been modified or mixed up with another version. It finds
your FlatOut 4 VR install under Steam, confirms it's v1.87, and backs up
the original localization files to Common\Localisation\BACKUP before
changing "Show Profile" to "Mute/Unmute" in the multiplayer menu. If a
backup already exists, it's left alone.

Setup normally installs the loader as version.dll. If another mod is
already using that name, it uses winhttp.dll instead. Either way, it 
won't touch a loader file it doesn't recognize.

Once installed, you'll be asked which mute options you want. Run the
installer again any time to change them.

USAGE
=====

The mod repurposes the lobby's "Show Profile" action (Y on an Oculus 
controller, or whatever it's bound to for you), and changes it to 
"Mute/Unmute".

To Mute/Unmute yourself or another player, highlight player name and
press Y. Pressing Y again will toggle mute on/off.

When a player is muted, you should no longer see the 'speaker' icon
appear for that player.

When you're the lobby host, selecting the mute option will pop up a
submenu with options for 'Mute Player' or 'Kick Player'.

'Mute Player' is a toggle; selecting it again will unmute the player.
'Kick Player' is a default feature of this submenu - just a nice bonus!

UNINSTALL
=========

Run FlatOut4VR-Mute-Installer.exe from the extracted folder again and
choose Uninstall. This removes the mod's files and restores the original
"Show Profile" text. Your PLOC backups stay in the BACKUP folder, and any
loader files that aren't part of this mod are left untouched.

VOICE MUTE OPTIONS
==================

- Start with my microphone muted
- Auto-mute other players

These are saved to fl4tout_voip_mute.ini in the game folder and take
effect the next time you start FlatOut 4 VR.

ADVANCED INI OPTION
===================

AllowUnmute=1 is on by default, so you can always manually mute or unmute
yourself or another player even when one of the two options above is on.
The installer doesn't show this setting.

If you always use Discord or another external voice app and want to stop
accidental in-game unmuting, set AllowUnmute=0 directly in
fl4tout_voip_mute.ini. Set it back to 1 to allow manual overrides again.

MANUAL INSTALL (NO INSTALLER)
=============================

If you'd rather not run the installer exe, a manual install is available
as a separate download: fl4tout_voip_mute.dll, fl4tout_voip_mute.ini,
version.dll, and patch_localization.py. Copy all 4 files into your
FlatOut 4 VR game folder, then run patch_localization.py (requires
Python 3) to back up and patch the localization text. See README.md on
the project page for full manual-install steps, including how to edit
the localization files by hand if you'd rather not run any script.

GAME VERSION SUPPORTED
======================

This mod only works with FlatOut 4: Total Insanity VR v1.87. The
installer checks Flatout.exe's size and SHA-256 before doing anything,
and does the same for its own setup files, so a partial or mismatched
download won't get installed by accident.

TROUBLESHOOTING
===============

Each run writes a fresh log next to the installer:

FlatOut4VR-Mute-Installer.log

If something goes wrong, that log is the first thing to check or share.

If you run into any issues, please create an Issue here:
https://github.com/Dteyn/Flatout4VR-Mute/issues


SUPPORT
=======

If you find this useful and wish to support the developer:
https://ko-fi.com/Dteyn