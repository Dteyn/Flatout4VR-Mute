#!/usr/bin/env python3
"""FlatOut 4 VR Mute v0.4.0 manual localization patcher.
Author: Dteyn
Date: 8/23/26

This patch changes the in-game text for the 'Show Profile' function to say 'Mute/Unmute'.
It patches all languages; not just English.

This patch is meant to be combined with the supplied .dll which changes the 'Show Profile'
function to 'Mute/Unmute'.

Normal use:
    Double-click this script from the FlatOut 4 VR game folder.

The script patches every supported localization (.PLOC) file it finds.
Backups are saved in Common/Localisation/BACKUP.

Running the script again will offer to restore the backups.

Command-line options are available for more operations. Use -h for details.

For game version: 1.87
"""

from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

LOCALISATION_DIR = Path("Common/Localisation")
BACKUP_DIR_NAME = "BACKUP"


@dataclass(frozen=True)
class PatchSpec:
    filename: str
    language: str
    offset: int
    original: str
    replacement: str

    @property
    def original_bytes(self) -> bytes:
        return (self.original + "\0").encode("utf-16le")

    @property
    def patched_bytes(self) -> bytes:
        original_payload = self.original.encode("utf-16le")
        replacement_payload = self.replacement.encode("utf-16le")
        remaining = len(original_payload) - len(replacement_payload)
        if remaining < 0 or remaining % 2:
            raise RuntimeError(
                f"Internal error: replacement for {self.filename} does not fit the original field"
            )
        return replacement_payload + (b" \x00" * (remaining // 2)) + b"\x00\x00"

# Below is the updated text for 'Mute/Unmute' or 'Toggle Mute' in all languages shipped with v1.87
PATCH_SPECS = (
    PatchSpec("LOCALISATION_CH_S.PLOC", "Chinese (Simplified)", 0x0D600, "显示档案", "切换静音"),
    PatchSpec("LOCALISATION_CH_T.PLOC", "Chinese (Traditional)", 0x0D456, "顯示設定檔", "切換靜音"),
    PatchSpec("LOCALISATION_EN.PLOC", "English", 0x2373A, "Show profile", "Mute/Unmute"),
    PatchSpec("LOCALISATION_FR.PLOC", "French", 0x27980, "Montrer le profil", "Muet/non muet"),
    PatchSpec("LOCALISATION_GE.PLOC", "German", 0x2939E, "Profil anzeigen", "Stumm an/aus"),
    PatchSpec("LOCALISATION_IT.PLOC", "Italian", 0x26ABE, "Mostra profilo", "Muto sì/no"),
    PatchSpec("LOCALISATION_JA.PLOC", "Japanese", 0x12AAE, "プロフィール表示", "ミュート切替"),
    PatchSpec("LOCALISATION_KO.PLOC", "Korean", 0x14248, "프로필 표시", "음소거 전환"),
    PatchSpec("LOCALISATION_POL.PLOC", "Polish", 0x280A2, "Pokaż profil", "Wycisz/Włącz"),
    PatchSpec("LOCALISATION_POR_B.PLOC", "Portuguese (Brazil)", 0x2672E, "Exibir perfil", "Mudo: sim/não"),
    PatchSpec("LOCALISATION_RU.PLOC", "Russian", 0x2589A, "Показать профиль", "Выкл./вкл. звук"),
    PatchSpec("LOCALISATION_SP.PLOC", "Spanish", 0x27C34, "Mostrar perfil", "Silencio sí/no"),
)

SPECS_BY_NAME = {spec.filename.upper(): spec for spec in PATCH_SPECS}

for _spec in PATCH_SPECS:
    if len(_spec.original_bytes) != len(_spec.patched_bytes):
        raise RuntimeError(f"Internal error: fixed-length mismatch for {_spec.filename}")


@dataclass(frozen=True)
class Target:
    path: Path
    spec: PatchSpec


class TargetError(ValueError):
    pass


def backup_path(target: Target) -> Path:
    return target.path.parent / BACKUP_DIR_NAME / target.path.name


def state_at_path(path: Path, spec: PatchSpec) -> str:
    if not path.is_file():
        return "missing"

    data = path.read_bytes()
    end = spec.offset + len(spec.original_bytes)
    if len(data) < end:
        return "too_small"

    chunk = data[spec.offset:end]
    if chunk == spec.original_bytes:
        return "original"
    if chunk == spec.patched_bytes:
        return "patched"
    return "unexpected"


def state_at_target(target: Target) -> str:
    return state_at_path(target.path, target.spec)


def target_for_file(path: Path) -> Target:
    spec = SPECS_BY_NAME.get(path.name.upper())
    if spec is None:
        supported = ", ".join(spec.filename for spec in PATCH_SPECS)
        raise TargetError(f"Unsupported PLOC file: {path.name}\nSupported files: {supported}")
    return Target(path, spec)


def targets_in_localisation_dir(directory: Path) -> list[Target]:
    return [
        Target(directory / spec.filename, spec)
        for spec in PATCH_SPECS
        if (directory / spec.filename).is_file()
    ]


def targets_from_argument(value: Path) -> list[Target]:
    value = value.expanduser()

    if value.suffix.upper() == ".PLOC":
        return [target_for_file(value)]

    candidates: list[Path] = []
    if value.name.lower() == "localisation":
        candidates.append(value)
    candidates.extend((value / LOCALISATION_DIR, value))

    seen: set[Path] = set()
    for directory in candidates:
        try:
            key = directory.resolve()
        except OSError:
            key = directory
        if key in seen:
            continue
        seen.add(key)

        targets = targets_in_localisation_dir(directory)
        if targets:
            return targets

    raise TargetError(
        "No supported FlatOut 4 localization files were found. Supply the game folder, "
        "Common/Localisation folder, or one supported .PLOC file."
    )


def auto_detect_targets() -> list[Target]:
    bases = [Path(__file__).resolve().parent, Path.cwd().resolve()]
    seen: set[Path] = set()

    for base in bases:
        for candidate in (base, base.parent, base.parent.parent):
            if candidate in seen:
                continue
            seen.add(candidate)
            try:
                return targets_from_argument(candidate)
            except TargetError:
                pass
    return []


def prompt_for_targets() -> list[Target]:
    print("FlatOut 4 was not found automatically.")
    print("Paste the FlatOut 4 game folder path below.")
    print()

    try:
        raw = input("Game folder: ").strip().strip('"')
    except (EOFError, KeyboardInterrupt):
        print()
        return []

    if not raw:
        return []

    try:
        return targets_from_argument(Path(raw))
    except TargetError as exc:
        print(f"\nERROR: {exc}")
        return []


def validate_patch_targets(targets: list[Target]) -> tuple[bool, int]:
    """Validate every selected file and backup before modifying anything."""
    for target in targets:
        state = state_at_target(target)
        if state not in {"original", "patched"}:
            print(f"ERROR: {target.path.name} did not match the supported game data.")
            return False, 2

        backup = backup_path(target)
        if backup.exists():
            if state_at_path(backup, target.spec) != "original":
                print(f"ERROR: Backup validation failed for {target.path.name}.")
                return False, 3
        elif state == "patched":
            print(f"ERROR: Original backup is missing for {target.path.name}.")
            return False, 3

    return True, 0


def patch_targets(targets: list[Target], *, quiet: bool = False) -> int:
    ok, exit_code = validate_patch_targets(targets)
    if not ok:
        print("No files were changed.")
        return exit_code

    to_patch = [target for target in targets if state_at_target(target) == "original"]
    if not to_patch:
        if not quiet:
            print("All selected localization files are already patched.")
        return 0

    # Create every required backup before changing any game file.
    for target in to_patch:
        backup = backup_path(target)
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target.path, backup)
            if state_at_path(backup, target.spec) != "original":
                print(f"ERROR: Backup verification failed for {target.path.name}.")
                return 4

    for target in to_patch:
        data = bytearray(target.path.read_bytes())
        start = target.spec.offset
        end = start + len(target.spec.original_bytes)
        data[start:end] = target.spec.patched_bytes
        target.path.write_bytes(data)

        if state_at_target(target) != "patched":
            print(f"ERROR: Patch verification failed for {target.path.name}.")
            return 4

    if not quiet:
        print(f"Patched {len(to_patch)} localization file(s).")
    return 0


def validate_restore_targets(targets: list[Target]) -> tuple[list[Target], int]:
    restore_list: list[Target] = []

    for target in targets:
        state = state_at_target(target)
        if state not in {"original", "patched"}:
            print(f"ERROR: {target.path.name} did not match the supported game data.")
            return [], 2
        if state == "original":
            continue

        backup = backup_path(target)
        if not backup.is_file():
            print(f"ERROR: Original backup is missing for {target.path.name}.")
            return [], 2
        if state_at_path(backup, target.spec) != "original":
            print(f"ERROR: Backup validation failed for {target.path.name}.")
            return [], 3

        restore_list.append(target)

    return restore_list, 0


def restore_targets(targets: list[Target], *, quiet: bool = False) -> int:
    restore_list, exit_code = validate_restore_targets(targets)
    if exit_code:
        print("No files were changed.")
        return exit_code

    if not restore_list:
        if not quiet:
            print("All selected localization files are already original.")
        return 0

    for target in restore_list:
        shutil.copy2(backup_path(target), target.path)
        if state_at_target(target) != "original":
            print(f"ERROR: Restore verification failed for {target.path.name}.")
            return 4

    if not quiet:
        print(f"Restored {len(restore_list)} localization file(s).")
    return 0


def print_status(targets: list[Target]) -> int:
    print(f"{'File':28} {'Language':23} State")
    print("-" * 62)

    result = 0
    for target in targets:
        state = state_at_target(target)
        print(f"{target.path.name:28} {target.spec.language:23} {state}")
        if state not in {"original", "patched"}:
            result = 1
    return result


def prompt_yes_no(prompt: str) -> bool:
    try:
        choice = input(f"{prompt} [y/N]: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print()
        return False
    return choice in {"y", "yes"}


def basic_run(target_hint: Path | None = None) -> int:
    print("FlatOut 4 Mute/Unmute Localisation Patch")
    print("----------------------------------------")
    print()
    print("This patch updates the multiplayer player list so the")
    print("'Show Profile' text is replaced with 'Mute/Unmute'.")
    print()
    print("This makes the player-list option better match its actual")
    print("function when using the multiplayer mute/unmute feature.")
    print()
    print("----------------------------------------")    
    print()
    
    if target_hint is not None:
        try:
            targets = targets_from_argument(target_hint)
        except TargetError as exc:
            print(f"ERROR: {exc}")
            return 2
    else:
        targets = auto_detect_targets()
        if not targets:
            targets = prompt_for_targets()
            if not targets:
                print("No changes made.")
                return 0

    states = [state_at_target(target) for target in targets]
    if any(state not in {"original", "patched"} for state in states):
        print("ERROR: One or more localization files do not match the supported game data.")
        print("No files were changed.")
        return 2

    all_patched = all(state == "patched" for state in states)

    if all_patched:
        print("The Mute/Unmute text patch is already installed.")
        print()
        print("You can restore the original localisation files to change")
        print("the player-list text back to 'Show Profile'.")
        print()
        if prompt_yes_no("Restore the original files?"):
            result = restore_targets(targets, quiet=True)
            if result == 0:
                print("\nOriginal localization files restored.")
                print()
                print("Text will now display 'Show Profile' again.")
            return result
        print("\nNo changes made.")
        return 0

    result = patch_targets(targets, quiet=True)
    if result == 0:
        print("Text patch installed successfully! 'Show Profile' is now changed to 'Mute/Unmute'.")
        print()
        print("Run this script again if you want to change the text back to 'Show Profile'.")
    return result


def pause_before_exit() -> None:
    try:
        input("\nPress Enter to close...")
    except (EOFError, KeyboardInterrupt):
        pass


def resolve_cli_targets(path: Path | None) -> list[Target]:
    if path is not None:
        return targets_from_argument(path)

    targets = auto_detect_targets()
    if not targets:
        raise TargetError(
            "No supported localization files were auto-detected. Supply the FlatOut 4 game folder, "
            "Common/Localisation folder, or one supported .PLOC file."
        )
    return targets


def build_parser() -> argparse.ArgumentParser:
    return argparse.ArgumentParser(
        description=(
            "Patch or restore FlatOut 4 mute-toggle localization labels. "
            "With no options, the script uses basic-user mode: it patches automatically on the "
            "first run and offers to restore originals when run again."
        ),
        epilog=(
            "Examples:\n"
            "  patch_localization.py\n"
            "  patch_localization.py --status \"C:\\Games\\FlatOut 4\"\n"
            "  patch_localization.py --patch  \"C:\\Games\\FlatOut 4\"\n"
            "  patch_localization.py --restore \"C:\\Games\\FlatOut 4\"\n"
            "\n"
            "Backups are stored in Common\\Localisation\\BACKUP using the original filenames."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )


def main() -> int:
    parser = build_parser()
    parser.add_argument(
        "path",
        type=Path,
        nargs="?",
        help=(
            "FlatOut 4 game folder, Common/Localisation folder, or one supported .PLOC file. "
            "If omitted, the script tries to detect the game automatically."
        ),
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--status", action="store_true", help="show the verified state of selected localization files")
    action.add_argument("--patch", action="store_true", help="explicitly apply the patch to selected localization files")
    action.add_argument("--restore", action="store_true", help="restore selected localization files from verified backups")
    args = parser.parse_args()

    explicit_action = args.status or args.patch or args.restore
    if explicit_action:
        try:
            targets = resolve_cli_targets(args.path)
        except TargetError as exc:
            parser.error(str(exc))

        if args.status:
            return print_status(targets)
        if args.patch:
            return patch_targets(targets)
        return restore_targets(targets)

    result = basic_run(args.path)
    pause_before_exit()
    return result


if __name__ == "__main__":
    sys.exit(main())
