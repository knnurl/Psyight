"""Build the M4 training set: fingerprint, de-duplicate, then teacher-label each track (SPEC 6.9).

Usage, from the repository root (after a Release build of the app):
    tools/python/.venv/Scripts/python tools/python/m4_dataset.py fingerprint <folder> [more...]
    tools/python/.venv/Scripts/python tools/python/m4_dataset.py plan
    tools/python/.venv/Scripts/python tools/python/m4_dataset.py export [--limit N]

fingerprint: psyight_teacher --fingerprint-only on every audio file under the folders, and on the
    four reference tracks in data/, into data/m4/fingerprints/. Resumable.
plan: groups files whose fingerprints match (the same music, even re-encoded or offset), drops
    every group holding a reference track (they are evaluation tracks, never trained on), keeps
    one file per remaining group (WAV first), and writes data/m4/manifest.json.
export: for each kept track, copies it into data/m4/work/<id>/, runs Demucs there, runs
    psyight_teacher --stems into data/m4/tracks/<id>.{json,bin}, and deletes the work folder, so
    stems never pile up (about 0.37 GB each). Resumable: finished tracks are skipped.

Only reads the source folders. Everything written stays in data/ (gitignored).
"""

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "data"
M4 = DATA / "m4"
TEACHER = ROOT / "app" / "build" / "windows-msvc" / "release" / "psyight_teacher.exe"
SEPARATE = ROOT / "tools" / "python" / "separate.py"
REFERENCES = ["OculusDrift.mp3", "Phase Change.mp3", "Hall of Purity.wav", "Hagios.mp3"]
AUDIO = {".mp3", ".wav", ".flac", ".aiff", ".aif"}

# Matching: the fingerprint holds 12 band levels a second in the student's feature units
# (1 unit = 60 dB). Two files match when, at the best offset within MAX_LAG_S, at least
# MIN_OVERLAP_S seconds line up with a mean absolute band difference under MAX_DIFF_DB.
MAX_LAG_S = 90
MIN_OVERLAP_S = 60
MAX_DIFF_DB = 2.0


def file_id(path: pathlib.Path) -> str:
    return hashlib.sha1(str(path.resolve()).lower().encode("utf-8")).hexdigest()[:12]


def read_array(base: pathlib.Path, name: str) -> np.ndarray:
    meta = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))
    a = meta["arrays"][name]
    raw = base.with_suffix(".bin").read_bytes()
    count = int(np.prod(a["shape"]))
    return np.frombuffer(raw, dtype=np.dtype(a["dtype"]), count=count,
                         offset=a["offset"]).reshape(a["shape"])


def fingerprint(args) -> int:
    out = M4 / "fingerprints"
    out.mkdir(parents=True, exist_ok=True)
    files = [DATA / name for name in REFERENCES]
    for folder in args.folders:
        files += sorted(p for p in pathlib.Path(folder).rglob("*") if p.suffix.lower() in AUDIO)
    index_path = M4 / "files.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.exists() else {}
    for i, path in enumerate(files):
        fid = file_id(path)
        index[fid] = {"path": str(path), "reference": path.parent == DATA}
        base = out / fid
        if base.with_suffix(".json").exists():
            continue
        print(f"[{i + 1}/{len(files)}] {path.name}", flush=True)
        result = subprocess.run([str(TEACHER), "--fingerprint-only", str(path), str(base)],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        if result.returncode != 0:
            print(f"  failed: {result.stderr.strip()}", flush=True)
            index[fid]["failed"] = result.stderr.strip()
    index_path.write_text(json.dumps(index, indent=2), encoding="utf-8")
    return 0


def match_db(a: np.ndarray, b: np.ndarray) -> float:
    """Smallest mean absolute band difference (dB) over offsets with enough overlap."""
    best = float("inf")
    for lag in range(-MAX_LAG_S, MAX_LAG_S + 1):
        if lag >= 0:
            x, y = a[lag:], b
        else:
            x, y = a, b[-lag:]
        n = min(len(x), len(y))
        if n < MIN_OVERLAP_S:
            continue
        best = min(best, float(np.mean(np.abs(x[:n] - y[:n]))) * 60.0)
    return best


def plan(args) -> int:
    index = json.loads((M4 / "files.json").read_text(encoding="utf-8"))
    prints = {}
    for fid, entry in index.items():
        if entry.get("failed"):
            continue
        prints[fid] = read_array(M4 / "fingerprints" / fid, "fingerprint").astype(np.float32)
    ids = sorted(prints)
    parent = {i: i for i in ids}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    # Cheap screen first: the overall band profile must be close before the offset search.
    profile = {i: prints[i].mean(axis=0) for i in ids}
    pairs = 0
    for n, i in enumerate(ids):
        for j in ids[n + 1:]:
            if np.mean(np.abs(profile[i] - profile[j])) * 60.0 > 3.0 * MAX_DIFF_DB:
                continue
            pairs += 1
            if match_db(prints[i], prints[j]) < MAX_DIFF_DB:
                parent[find(i)] = find(j)
    groups = {}
    for i in ids:
        groups.setdefault(find(i), []).append(i)
    manifest = {"kept": [], "excluded": []}
    for members in groups.values():
        paths = [pathlib.Path(index[m]["path"]) for m in members]
        if any(index[m]["reference"] for m in members):
            refs = [p.name for m, p in zip(members, paths) if index[m]["reference"]]
            for m, p in zip(members, paths):
                if not index[m]["reference"]:
                    manifest["excluded"].append({"id": m, "path": str(p),
                                                 "reason": "copy of " + ", ".join(refs)})
            continue
        order = sorted(zip(members, paths), key=lambda mp: (mp[1].suffix.lower() != ".wav",
                                                             str(mp[1])))
        keep_id, keep_path = order[0]
        manifest["kept"].append({"id": keep_id, "path": str(keep_path),
                                 "duplicates": [str(p) for _, p in order[1:]]})
    manifest["kept"].sort(key=lambda e: e["path"])
    (M4 / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    duplicates = sum(len(e["duplicates"]) for e in manifest["kept"])
    print(f"{len(ids)} files, {pairs} pairs compared in detail: {len(manifest['kept'])} tracks "
          f"kept, {duplicates} duplicates dropped, {len(manifest['excluded'])} copies of "
          f"reference tracks excluded")
    for e in manifest["excluded"]:
        print(f"  excluded {pathlib.Path(e['path']).name} ({e['reason']})")
    return 0


def export(args) -> int:
    manifest = json.loads((M4 / "manifest.json").read_text(encoding="utf-8"))
    tracks = M4 / "tracks"
    tracks.mkdir(parents=True, exist_ok=True)
    todo = [e for e in manifest["kept"] if not (tracks / (e["id"] + ".json")).exists()]
    if args.limit:
        todo = todo[: args.limit]
    status = 0
    for n, entry in enumerate(todo):
        source = pathlib.Path(entry["path"])
        work = M4 / "work" / entry["id"]
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        copy = work / (entry["id"] + source.suffix.lower())
        print(f"[{n + 1}/{len(todo)}] {source.name}", flush=True)
        try:
            shutil.copyfile(source, copy)
            separated = subprocess.run([sys.executable, str(SEPARATE), str(copy)],
                                       capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
            if separated.returncode != 0:
                raise RuntimeError("demucs: " + separated.stderr.strip()[-400:])
            labelled = subprocess.run([str(TEACHER), "--stems", str(copy),
                                       str(tracks / entry["id"])], capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
            if labelled.returncode != 0:
                raise RuntimeError("teacher: " + labelled.stderr.strip())
            meta_path = (tracks / entry["id"]).with_suffix(".json")
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
            meta["source_path"] = str(source)
            meta_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")
        except Exception as error:  # Keep going; the failure is logged and retried next run.
            print(f"  failed: {error}", flush=True)
            for leftover in tracks.glob(entry["id"] + ".*"):
                leftover.unlink()
            status = 1
        finally:
            shutil.rmtree(work, ignore_errors=True)
    return status


def main() -> int:
    # File names can hold characters the Windows console code page lacks (logs are redirected).
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    f = sub.add_parser("fingerprint")
    f.add_argument("folders", nargs="+")
    sub.add_parser("plan")
    e = sub.add_parser("export")
    e.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()
    return {"fingerprint": fingerprint, "plan": plan, "export": export}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
