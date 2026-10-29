#!/usr/bin/env python3
"""Stage generated ARA headers and their dependencies without editing upstream."""
import pathlib
import re
import shutil
import sys
import tempfile

SERVICES = {
    "Metrics": "planningmetrics",
    "Remaining": "remainingdistance",
    "Turn": "turncommand",
    "Hazard": "hazardcommand",
}


def validate(base, dependency_roots=()):
    pending = []
    for family, service in SERVICES.items():
        for kind in ("proxy", "skeleton", "common"):
            header = base / f"oss/srv/AA2SCr{family}/scr_si_{service}_{kind}.h"
            if not header.is_file():
                raise ValueError(
                    f"Missing generated header: {header}\n"
                    "The pinned oss-ap lacks these Decision AP services. Set "
                    "DECISION_GENERATED_SOURCE to an Ecu_Machine directory containing "
                    "the generated services, or DECISION_GENERATED to a complete prepared directory."
                )
            pending.append(header)
    seen = set()
    while pending:
        header = pending.pop()
        if header in seen:
            continue
        seen.add(header)
        for name in re.findall(r'^\s*#\s*include\s*["<]([^">]+)', header.read_text(), re.M):
            if name.startswith("oss/"):
                target = base / name
                if not target.is_file():
                    candidates = [root / name for root in dependency_roots if (root / name).is_file()]
                    if not candidates:
                        raise ValueError(f"Missing dependency {name}, referenced by {header}")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(candidates[0], target)
                pending.append(target)
            elif name.endswith(".h") and not name.startswith(("ara/", "para/")):
                target = header.parent / name
                if not target.is_file():
                    raise ValueError(f"Missing relative dependency {name}, referenced by {header}")
                pending.append(target)
    return len(seen)


def main():
    source, destination = map(pathlib.Path, sys.argv[1:])
    if destination.exists():
        count = validate(destination / "Exe_ServiceCreator/include")
        print(f"Verified generated headers: {destination} ({count} service/dependency files)")
        return
    source_include = source / "Exe_ServiceCreator/include"
    if not source_include.is_dir():
        raise ValueError(f"Generated source directory missing: {source_include}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Publish only a complete, validated tree; leave existing inputs untouched.
    with tempfile.TemporaryDirectory(prefix=".generated-", dir=destination.parent) as temporary:
        staged = pathlib.Path(temporary) / "generated"
        include = staged / "Exe_ServiceCreator/include"
        shutil.copytree(source_include, include)
        roots = [source / "Exe_motionvelocityplanner/include", source / "Exe_missionplanner/include"]
        count = validate(include, roots)
        staged.rename(destination)
    print(f"Copied generated headers: {source} -> {destination} ({count} service/dependency files)")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        sys.exit(f"Error: {error}")
