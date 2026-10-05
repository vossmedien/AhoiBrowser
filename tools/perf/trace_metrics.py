"""Trace-event metrics of the desktop perf harness (run_desktop_perf.py).

Pure functions over Chrome trace events: span durations and presented-frame
latency. Split out of run_desktop_perf.py to keep it within the source budget.
"""

from __future__ import annotations

import json


def trace_durations_ms(events: list[dict], names: dict[str, str]) -> dict[str, list[float]]:
    """Durations of complete ('X') or begin/end ('B'/'E') trace events, by metric."""
    result: dict[str, list[float]] = {metric: [] for metric in names.values()}
    open_events: dict[tuple, float] = {}
    for event in events:
        metric = names.get(event.get("name"))
        if not metric:
            continue
        phase = event.get("ph")
        if phase == "X":
            result[metric].append(event.get("dur", 0) / 1000)
        elif phase == "B":
            open_events[(event["name"], event.get("tid"))] = event["ts"]
        elif phase == "E":
            start = open_events.pop((event["name"], event.get("tid")), None)
            if start is not None:
                result[metric].append((event["ts"] - start) / 1000)
    return result


# cc/metrics/compositor_frame_reporter.cc emits one async "PipelineReporter"
# track per compositor frame (categories "cc,benchmark,..."); the begin event
# carries the frame's final state and its end is the presentation time.
PRESENTED_FRAME_STATES = {"STATE_PRESENTED_ALL", "STATE_PRESENTED_PARTIAL"}
PRESENTED_FRAME_LIMIT_US = 2_000_000  # a switch without a frame in 2 s is a failure


class TraceError(RuntimeError):
    pass


def presented_frames(events: list[dict], by_host: bool = False) -> dict:
    """(begin, end) microseconds of presented PipelineReporter frames per pid,
    or per (pid, layer_tree_host_id) when `by_host` is set."""
    begins: dict[tuple, tuple[float, str, object]] = {}
    frames: dict = {}
    for event in events:
        if event.get("name") != "PipelineReporter" or event.get("ph") not in ("b", "e"):
            continue
        identity = event.get("id2") or event.get("id")
        key = (event.get("pid"), json.dumps(identity, sort_keys=True))
        if event["ph"] == "b":
            # The JSON trace names the typed field `frame_reporter` (build 45,
            # validation cb11); `chrome_frame_reporter` is the proto field name.
            args = event.get("args") or {}
            reporter = args.get("frame_reporter") or args.get("chrome_frame_reporter") or {}
            begins[key] = (event["ts"], str(reporter.get("state", "")),
                           reporter.get("layer_tree_host_id"))
            continue
        begin = begins.pop(key, None)
        if begin is not None and begin[1] in PRESENTED_FRAME_STATES:
            group = (event.get("pid"), begin[2]) if by_host else event.get("pid")
            frames.setdefault(group, []).append((begin[0], event["ts"]))
    for values in frames.values():
        values.sort()
    return frames


def presented_latency_ms(events: list[dict], names: dict[str, str]) -> dict[str, list[float]]:
    """Ahoi event start to the end of the first presented frame begun after it ended.

    A frame whose frame time precedes the end of the Ahoi event cannot contain
    its result. Metric names end in `_presented_ms`. A traced Ahoi event with no
    such frame in the same process within 2 s aborts the scenario, so a missing
    frame can never shorten the statistic.
    """
    frames = presented_frames(events)
    spans: list[tuple[str, int, float, float]] = []
    open_events: dict[tuple, float] = {}
    for event in events:
        metric = names.get(event.get("name"))
        if not metric:
            continue
        phase = event.get("ph")
        if phase == "X":
            spans.append((metric, event.get("pid"), event["ts"], event["ts"] + event.get("dur", 0)))
        elif phase == "B":
            open_events[(event["name"], event.get("pid"), event.get("tid"))] = event["ts"]
        elif phase == "E":
            start = open_events.pop((event["name"], event.get("pid"), event.get("tid")), None)
            if start is not None:
                spans.append((metric, event.get("pid"), start, event["ts"]))
    result: dict[str, list[float]] = {
        metric.removesuffix("_ms") + "_presented_ms": [] for metric in names.values()}
    for metric, pid, start, end in spans:
        frame = next((frame_end for frame_begin, frame_end in frames.get(pid, [])
                      if frame_begin >= end), None)
        if frame is None or frame - start > PRESENTED_FRAME_LIMIT_US:
            raise TraceError(f"no presented frame after {metric} in process {pid}")
        result[metric.removesuffix("_ms") + "_presented_ms"].append((frame - start) / 1000)
    # Diagnostic split per compositor (owner review of cb12: the window and the
    # command bar present through different layer tree hosts). Not budgeted.
    for (pid, host), host_frames in sorted(presented_frames(events, by_host=True).items(),
                                           key=lambda item: str(item[0])):
        if host is None:
            continue
        for metric, span_pid, start, end in spans:
            if span_pid != pid:
                continue
            frame = next((frame_end for frame_begin, frame_end in host_frames
                          if frame_begin >= end), None)
            if frame is not None:
                result.setdefault(f"{metric.removesuffix('_ms')}_presented_host{host}_ms",
                                  []).append((frame - start) / 1000)
    return result
