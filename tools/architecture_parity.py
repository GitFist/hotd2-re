"""Compare current x86/x64 gameplay transcripts and native saves using local fixtures.

Original scene captures remain read-only. Passing this checks architecture parity,
not perfect agreement with the original captures: existing capture mismatches are
retained verbatim in each transcript. Network integration uses loopback only.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def cases(game, fixtures):
    core = [
        ("network_room", "network_room_test", [game]),
        ("randomizer_recovery", "randomizer_test", [game]),
        ("stage_results", "stage_results_test", [game, fixtures / "script_auto_full.txt"]),
        ("credits", "credits_skip_test", [game]),
        ("credits_final_chapter", "credits_skip_test", [game, fixtures / "final-boss-flicker/start.txt", fixtures / "script_auto_full2.txt"]),
        ("night_shift_boss_records", "night_shift_test", [game, fixtures / "script_modes_boss0.txt"]),
        ("night_shift_live_horde", "night_shift_replay_test", [game]),
        ("bat_spawn_audit", "bat_variants_test", [game]),
        ("blood", "blood_color_test", [game]),
        ("pause", "pause_menu_test", []),
        ("present_clock", "present_clock_test", []),
        ("texture_pack", "texture_pack_test", [game]),
        ("widescreen", "widescreen_test", [game]),
        ("temporal_history", "temporal_history_test", []),
        ("render_pose", "render_pose_test", []),
        ("scene_pose", "scene_pose_test", [game]),
        ("mesh_refinement", "mesh_refinement_test", []),
        ("leaderboard_udp", "@leaderboard_client_integration.py", ["leaderboard_client_test"]),
        ("home_directory_udp", "@home_lobby_preview_integration.py", ["home_lobby_preview_test", game]),
    ]
    capture = []
    def add(label, tool, name, script=None, *extra):
        args = [game, fixtures / ("scene_capture_" + name + ".txt")]
        if script:
            args += ["--script", fixtures / (script + ".txt")]
        capture.append((label, tool, args + list(extra)))
    add("world_boot", "world_test", "play1full", "script_p1_start")
    add("world_reload", "world_test", "play2scene1", "script_p1_reload")
    add("world_2p", "world_test", "2p", "script_2p")
    add("world_pause", "world_test", "pause_p1", "script_pause_p1")
    add("world_st1", "world_test", "auto_st1", "script_auto_full")
    for stage, start in ((2, 14659), (3, 29736), (4, 44107), (5, 56007), (6, 70806)):
        script = "script_auto_full" if stage < 4 else "script_auto_full2"
        extra = ["--stage-start", str(start)]
        if stage == 6:
            extra += ["--civilians", "0,1"]
        add(f"world_st{stage}", "world_test", f"auto_st{stage}", script, *extra)
    add("world_ending", "world_test", "auto_st6_end", "script_auto_full2",
        "--prefix", fixtures / "scene_capture_auto_st6.txt", "--stage-start", "70806", "--civilians", "0,1")
    add("frontend_boot", "frontend_test", "play1full", "script_p1_start")
    add("frontend_2p", "frontend_test", "2p", "script_2p")
    add("frontend_options", "frontend_test", "modes_options", "script_modes_options")
    add("frontend_rankings", "frontend_test", "modes_rank", "script_modes_rank")
    add("frontend_original", "frontend_test", "modes_orig", "script_modes_orig")
    for boss in range(9):
        extra = ["--save-patch", "9C9F33:0101010101010101"] if boss == 8 else []
        add(f"frontend_boss{boss}", "frontend_test", f"modes_boss{boss}", f"script_modes_boss{boss}", *extra)
    for weapon in range(1, 4):
        add(f"frontend_weapon{weapon}", "frontend_test", f"modes_weapon{weapon}", f"script_modes_weapon{weapon}",
            "--save-patch", "9C9F3D:010101")
    for lesson in range(2, 10):
        add(f"frontend_training{lesson}", "frontend_test", f"modes_train{lesson}", f"script_modes_train{lesson}",
            "--save-patch", "9C9F29:03030303030303030300")
    return core, capture


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    core, capture = cases(args.game.resolve(), args.fixtures.resolve())
    selected = core if args.group == "core" else capture if args.group == "captures" else core + capture
    if args.case:
        selected = [case for case in selected if re.search(args.case, case[0])]
    if not selected and not args.netplay_only:
        raise ValueError("No cases selected")
    env = {k: v for k, v in os.environ.items() if not k.startswith("HOTD2_")}
    report = {"game": str(args.game.resolve()), "fixtures": str(args.fixtures.resolve()), "cases": []}
    if args.netplay_only:
        selected=[]
        previous=out/"report.json"
        if previous.exists():
            report=json.loads(previous.read_text())
    for name, tool, case_args in selected:
        item = {"name": name, "runs": {}}
        for arch, build in (("x86", args.x86), ("x64", args.x64)):
            folder = out / name / arch
            folder.mkdir(parents=True, exist_ok=True)
            native = folder / "native-save.bin"
            # This harness owns this exact isolated file. Each replay starts from the same original read-only save.
            if native.exists():
                native.unlink()
            command_args = [str(x) for x in case_args]
            if tool.startswith("@"):
                exe = build.resolve() / "Release" / (command_args[0] + ".exe")
                command = [sys.executable, str(ROOT / "tests" / tool[1:]), str(exe)] + command_args[1:]
            else:
                exe = build.resolve() / "Release" / (tool + ".exe")
                command = [str(exe)] + command_args
            if tool == "frontend_test":
                command += ["--save-native", str(native)]
            if not exe.is_file():
                raise FileNotFoundError(exe)
            start = time.monotonic()
            log = folder / "transcript.txt"
            with log.open("wb") as stream:
                result = subprocess.run(command, cwd=folder, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=args.timeout, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            transcript = log.read_bytes()
            # Only the deliberately distinct isolated save output path can vary between these otherwise equal runs.
            normalized = transcript.replace(str(native).encode(), b"<isolated-native-save>")
            (folder / "comparable.txt").write_bytes(normalized)
            run_info = {"command": command, "exit": result.returncode, "seconds": round(time.monotonic()-start, 3),
                        "exe_sha256": digest(exe.read_bytes()), "transcript_sha256": digest(normalized),
                        "save_sha256": digest(native.read_bytes()) if native.exists() else None}
            item["runs"][arch] = run_info
            print(f"{name} {arch}: exit {result.returncode}, {run_info['seconds']}s", flush=True)
        a, b = item["runs"]["x86"], item["runs"]["x64"]
        item["passed"] = (a["exit"] == b["exit"] == 0 and a["transcript_sha256"] == b["transcript_sha256"]
                          and a["save_sha256"] == b["save_sha256"])
        if not item["passed"]:
            left = (out / name / "x86/comparable.txt").read_text(errors="replace").splitlines(True)
            right = (out / name / "x64/comparable.txt").read_text(errors="replace").splitlines(True)
            (out / name / "difference.diff").write_text("".join(difflib.unified_diff(left, right, fromfile="x86", tofile="x64")))
        report["cases"].append(item)
        report["passed"] = all(case["passed"] for case in report["cases"])
        (out / "report.json").write_text(json.dumps(report, indent=2))
        print(("PASS " if item["passed"] else "FAIL ") + name + " architecture transcripts/save bytes", flush=True)
    if args.netplay or args.netplay_only:
        spec = importlib.util.spec_from_file_location("local_arch_room", ROOT / "tools/netplay_room_smoke.py")
        room = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(room)
        room.REPO = args.fixtures.resolve().parent  # only game/scene fixture paths; service code stays this checkout's.
        hashes = {}
        for arch, build in (("x86", args.x86), ("x64", args.x64)):
            try:
                result = room.run(build.resolve() / "src/port/Release/play.exe", night_shift=True)
            except BaseException as error:
                report["local_netplay"]={"failed_architecture":arch,"error":str(error),"completed":hashes}
                report["passed"]=False
                (out/"report.json").write_text(json.dumps(report,indent=2))
                raise
            hashes[arch] = result
        report["local_netplay"] = hashes
        report["passed"] = all(case["passed"] for case in report["cases"]) and hashes["x86"]["hash"] == hashes["x64"]["hash"]
        (out / "report.json").write_text(json.dumps(report, indent=2))
    print(f"{'PASS' if report['passed'] else 'FAIL'} {len(report['cases'])} paired current-source cases; {out / 'report.json'}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--x86", type=Path, required=True, help="current Win32 build folder")
    parser.add_argument("--x64", type=Path, required=True, help="current x64 build folder")
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--group", choices=("core", "captures", "all"), default="all")
    parser.add_argument("--case", help="optional case-name regular expression")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--netplay", action="store_true", help="also run same-architecture real client pairs through a local service")
    parser.add_argument("--netplay-only", action="store_true", help="retain a previous capture report and rerun only local client pairs")
    raise SystemExit(run(parser.parse_args()))
