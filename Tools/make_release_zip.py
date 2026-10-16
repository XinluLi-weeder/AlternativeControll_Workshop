"""Zip an already packaged Windows build for non-developer playtesters."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


root = Path(__file__).resolve().parents[1]
package = root / "Releases" / "Windows"
output = root / "Releases" / "RoomShiftDemo-Windows-UE5.7.zip"
if not (package / "RoomShiftDemo.exe").is_file():
    raise SystemExit(f"Packaged executable missing: {package / 'RoomShiftDemo.exe'}")

with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6) as archive:
    archive.write(root / "PLAYTEST.txt", "RoomShiftDemo-Windows/玩法说明.txt")
    for path in sorted(package.rglob("*")):
        if not path.is_file() or path.suffix.lower() == ".pdb":
            continue
        if path.name.startswith("Manifest_") and path.suffix == ".txt":
            continue
        archive.write(path, Path("RoomShiftDemo-Windows") / path.relative_to(package))

print(f"Created {output} ({output.stat().st_size / 1024**2:.1f} MiB)")
