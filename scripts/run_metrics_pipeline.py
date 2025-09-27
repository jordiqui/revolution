#!/usr/bin/env python3
"""Automation helper to gather Revolution search metrics for XP experiments."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PLAN = ROOT / "docs" / "pipelines" / "xp_plan.json"
DEFAULT_ENGINE = ROOT / "src" / "revolution v.2.80-dev-270925"


class UCIProcess:
    def __init__(self, binary: Path) -> None:
        self.proc = subprocess.Popen(
            [str(binary)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        if self.proc.stdin is None or self.proc.stdout is None:
            raise RuntimeError("Failed to start engine process")

    def send(self, command: str) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()

    def collect_until(self, predicate: Callable[[str], bool]) -> List[str]:
        assert self.proc.stdout is not None
        lines: List[str] = []
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("Engine closed the connection unexpectedly")
            stripped = line.rstrip("\n")
            lines.append(stripped)
            if predicate(stripped):
                break
        return lines

    def expect(self, token: str) -> None:
        self.collect_until(lambda line: line.strip() == token)

    def quit(self) -> None:
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def sanitize_label(label: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in label)


def round_value(value: float, digits: int = 4) -> float:
    return round(value, digits)


def summarize_metrics(metrics: Dict) -> Dict:
    iterations: List[Dict] = metrics.get("iterations", [])
    total_nodes = sum(int(item.get("nodes", 0)) for item in iterations)
    total_time_ms = sum(int(item.get("time_ms", 0)) for item in iterations)
    iteration_count = len(iterations)
    avg_re_searches = (
        sum(int(item.get("re_searches", 0)) for item in iterations) / iteration_count
        if iteration_count
        else 0.0
    )
    final_depth = iterations[-1]["depth"] if iteration_count else 0
    aspiration = metrics.get("aspiration", {})
    tt = metrics.get("tt", {})
    null_move = metrics.get("null_move", {})
    lmr_hist = metrics.get("lmr", {}).get("histogram", {})
    lmp_counts = metrics.get("lmp", {})

    probes = int(tt.get("probes", 0))
    hits = int(tt.get("hits", 0))
    hit_rate = hits / probes if probes else 0.0
    stores = int(tt.get("stores", 0))
    replacements = int(tt.get("replacements", 0))
    replace_rate = replacements / stores if stores else 0.0

    nmp_calls = int(null_move.get("calls", 0))
    nmp_cutoffs = int(null_move.get("cutoffs", 0))
    nmp_cutoff_rate = nmp_cutoffs / nmp_calls if nmp_calls else 0.0

    total_lmr = 0
    weighted_reduction = 0
    for bucket, count in lmr_hist.items():
        value = int(str(bucket).rstrip("+"))
        weighted_reduction += value * int(count)
        total_lmr += int(count)
    lmr_avg = weighted_reduction / total_lmr if total_lmr else 0.0

    total_lmp = sum(int(v) for v in lmp_counts.values())

    nps = (total_nodes * 1000.0 / total_time_ms) if total_time_ms else 0.0

    return {
        "final_depth": final_depth,
        "iterations": iteration_count,
        "avg_re_searches": round_value(avg_re_searches),
        "aspiration_re_searches": int(aspiration.get("re_searches", 0)),
        "aspiration_fail_highs": int(aspiration.get("fail_highs", 0)),
        "aspiration_fail_lows": int(aspiration.get("fail_lows", 0)),
        "total_nodes": total_nodes,
        "total_time_ms": total_time_ms,
        "nps": round_value(nps),
        "tt_hit_rate": round_value(hit_rate),
        "tt_replace_rate": round_value(replace_rate),
        "tt_probes": probes,
        "tt_replacements": replacements,
        "nmp_calls": nmp_calls,
        "nmp_cutoffs": nmp_cutoffs,
        "nmp_cutoff_rate": round_value(nmp_cutoff_rate),
        "lmr_events": total_lmr,
        "lmr_avg_reduction": round_value(lmr_avg),
        "lmp_prunes": total_lmp,
    }


def aggregate_summary(run_summaries: List[Dict]) -> Dict:
    if not run_summaries:
        return {}

    keys_to_mean = [
        "avg_re_searches",
        "nps",
        "tt_hit_rate",
        "tt_replace_rate",
        "nmp_cutoff_rate",
        "lmr_avg_reduction",
    ]
    aggregate: Dict[str, float] = {}
    for key in keys_to_mean:
        aggregate[f"mean_{key}"] = round_value(
            statistics.fmean(run["summary"][key] for run in run_summaries)
        )

    total_nodes = sum(run["summary"]["total_nodes"] for run in run_summaries)
    total_time = sum(run["summary"]["total_time_ms"] for run in run_summaries)
    aggregate["total_nodes"] = total_nodes
    aggregate["total_time_ms"] = total_time
    aggregate["overall_nps"] = round_value(total_nodes * 1000.0 / total_time) if total_time else 0.0
    aggregate["total_lmr_events"] = sum(run["summary"]["lmr_events"] for run in run_summaries)
    aggregate["total_lmp_prunes"] = sum(run["summary"]["lmp_prunes"] for run in run_summaries)
    aggregate["total_aspiration_re_searches"] = sum(
        run["summary"]["aspiration_re_searches"] for run in run_summaries
    )
    aggregate["total_nmp_cutoffs"] = sum(run["summary"]["nmp_cutoffs"] for run in run_summaries)
    aggregate["max_depth"] = max(run["summary"]["final_depth"] for run in run_summaries)

    return aggregate


def run_stage(
    engine_path: Path,
    stage: Dict,
    output_dir: Path,
    include_optional: bool,
) -> Optional[Dict]:
    if stage.get("optional") and not include_optional:
        print(f"Skipping optional stage '{stage['name']}'")
        return None

    stage_dir = output_dir / sanitize_label(stage["name"])
    stage_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n=== Stage: {stage['name']} ===")
    if stage.get("description"):
        print(stage["description"])
    if stage.get("notes"):
        print(f"Notes: {stage['notes']}")

    engine = UCIProcess(engine_path)
    try:
        engine.send("uci")
        engine.expect("uciok")

        stage_options = stage.get("uci_options", {})
        for name, value in stage_options.items():
            engine.send(f"setoption name {name} value {value}")
        engine.send("isready")
        engine.expect("readyok")

        run_summaries: List[Dict] = []
        for idx, run in enumerate(stage.get("runs", []), start=1):
            label = sanitize_label(run.get("label", f"run_{idx}"))
            metrics_file = stage_dir / f"{label}_metrics.json"

            engine.send(f"setoption name Metrics Log File value {metrics_file}")
            engine.send("isready")
            engine.expect("readyok")

            log_lines: List[str] = []
            for command in run.get("commands", []):
                engine.send(command)
                if command.strip().startswith("go "):
                    log_lines.extend(engine.collect_until(lambda line: line.startswith("bestmove")))
                else:
                    time.sleep(0.01)

            if not metrics_file.exists():
                raise RuntimeError(f"Metrics file {metrics_file} was not produced")

            with metrics_file.open("r", encoding="utf-8") as handle:
                metrics_data = json.load(handle)

            summary = summarize_metrics(metrics_data)
            run_summaries.append(
                {
                    "label": label,
                    "metrics_file": metrics_file.name,
                    "summary": summary,
                }
            )

            log_path = stage_dir / f"{label}.log"
            if log_lines:
                log_path.write_text("\n".join(log_lines), encoding="utf-8")

            print(
                f"  Run {label}: depth {summary['final_depth']} | nodes {summary['total_nodes']} | "
                f"NPS {summary['nps']} | TT hit-rate {summary['tt_hit_rate']}"
            )

        engine.send("quit")
        engine.proc.wait(timeout=5)
    finally:
        if engine.proc.poll() is None:
            engine.quit()

    aggregate = aggregate_summary(run_summaries)
    stage_summary = {
        "name": stage["name"],
        "description": stage.get("description", ""),
        "notes": stage.get("notes", ""),
        "runs": run_summaries,
        "aggregate": aggregate,
    }

    with (stage_dir / "summary.json").open("w", encoding="utf-8") as handle:
        json.dump(stage_summary, handle, indent=2)

    return stage_summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Run Revolution metrics pipeline")
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE, help="Path to engine binary")
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN, help="Path to plan JSON file")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Directory where stage artifacts are written (defaults to plan output)",
    )
    parser.add_argument(
        "--include-optional",
        action="store_true",
        help="Also execute stages marked as optional",
    )
    args = parser.parse_args()

    if not args.engine.exists():
        parser.error(f"Engine binary {args.engine} does not exist")
    if not args.plan.exists():
        parser.error(f"Plan file {args.plan} does not exist")

    with args.plan.open("r", encoding="utf-8") as handle:
        plan = json.load(handle)

    plan_output = plan.get("output_subdir", "metrics_runs")
    base_output = args.output or (ROOT / plan_output)
    timestamp = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
    output_dir = Path(base_output).expanduser().resolve() / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Writing artifacts to {output_dir}")

    stage_summaries: List[Dict] = []
    for stage in plan.get("stages", []):
        summary = run_stage(args.engine, stage, output_dir, args.include_optional)
        if summary is not None:
            stage_summaries.append(summary)

    overall_path = output_dir / "pipeline_summary.json"
    overall_data = {
        "plan": args.plan.name,
        "timestamp": timestamp,
        "stages": [summary["name"] for summary in stage_summaries],
    }
    overall_path.write_text(json.dumps(overall_data, indent=2), encoding="utf-8")
    print(f"Pipeline complete. Summary written to {overall_path}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
