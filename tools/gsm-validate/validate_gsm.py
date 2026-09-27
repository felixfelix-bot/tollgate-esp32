#!/usr/bin/env python3
"""validate_gsm.py — bench validation for the CTG-1 cellular upstream.

This is the operator-side half of CTG-1.  The firmware half is
main/gsm_upstream.c (AT core + UART transport); this script asks the same
questions from a laptop with the shield plugged into a USB-serial adapter, so a
bring-up can be validated *before* the ESP32 firmware is flashed and without
rebuilding anything.

It answers exactly one question per run: **does this modem + this SIM give us a
cellular data bearer?**  Specifically it walks

    AT -> AT+CPIN? -> AT+CSQ -> AT+CREG? -> AT+CEREG? -> AT+COPS?
       -> AT+CGDCONT=<cid>,"IP","<apn>" -> ATD*99***<cid>#

and stops at the modem's CONNECT (data mode).  CONNECT is where AT ends: from
there the UART carries PPP frames and this script cannot see inside them.  The
IP layer is only proven once the ESP32 firmware attaches LwIP PPPoS — which is
NOT implemented yet (see docs/GSM-UPSTREAM-INTEGRATION.md).  A CONNECT verdict
means "radio, SIM, registration and PDP context all work", not "we have an IP".

Usage
-----
    # live bench run (needs: pip install pyserial)
    python3 tools/gsm-validate/validate_gsm.py --port /dev/ttyUSB0 \
        --apn silent.link --out /tmp/ctg1-transcript.txt

    # replay a transcript captured earlier (no serial, no dependencies)
    python3 tools/gsm-validate/validate_gsm.py --replay /tmp/ctg1-transcript.txt

    # parse the built-in SIM800L fixture (smoke test for the parser itself)
    python3 tools/gsm-validate/validate_gsm.py --self-test

Exit codes: 0 = data bearer reached, 2 = bring-up failed (reason printed),
3 = usage / missing dependency.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time

SILENTLINK_APN = "silent.link"
DEFAULT_CID = 1

# SIM800L/SIM800C run their UART at 9600; SIM7600/A7670 class at 115200.
BAUD_BY_MODEM = {"sim800": 9600, "sim7600": 115200, "a7670": 115200}

# Failure reasons, deliberately the same vocabulary as gsm_failure_name() in
# main/gsm_upstream.c so a failed bench run and a failed firmware bring-up are
# reported identically.
FAIL = {
    "NONE": "none",
    "NO_RESPONSE": "modem did not respond to AT",
    "SIM_NOT_INSERTED": "SIM not inserted / not detected",
    "SIM_PIN": "SIM locked (PIN/PUK required)",
    "REG_DENIED": "network denied registration",
    "NO_COVERAGE": "no coverage / not registered",
    "PDP_REJECTED": "PDP context rejected (APN or data plan)",
    "AUTH_FAILED": "PDP authentication failed (PAP/CHAP)",
    "DIAL_FAILED": "dial failed (no CONNECT)",
    "OPERATOR_REFUSED": "operator refused attach (CME 3)",
    "TWO_G_ONLY": "registered on 2G only (EU sunset risk)",
}

RE_FINAL = re.compile(r"^(OK|ERROR|CONNECT(?: \d+)?|NO CARRIER|BUSY|NO DIALTONE|\+CM[ES] ERROR:.*)$")
RE_CPIN = re.compile(r"\+CPIN:\s*(.+)", re.I)
RE_CSQ = re.compile(r"\+CSQ:\s*(\d+)\s*,\s*(\d+)")
RE_REG = re.compile(r"\+C(?:E|G)?REG:\s*([\d,]+)")
RE_COPS = re.compile(r'\+COPS:\s*(\d+),\s*(\d+),"?([^",]*)"?,?\s*(\d+)?')
RE_CME = re.compile(r"\+CM[ES] ERROR:\s*(\d+)")


# --------------------------------------------------------------------------- #
# parsing                                                                      #
# --------------------------------------------------------------------------- #


def classify(line: str) -> str:
    """Final result code of a modem line, or 'INFO' for anything else."""
    line = line.strip()
    if not line:
        return "INFO"
    m = RE_FINAL.match(line)
    if not m:
        return "INFO"
    token = m.group(1)
    if token.startswith("CONNECT"):
        return "CONNECT"
    if token.startswith("+CM"):
        return "CME_ERROR"
    return token


def rssi_dbm(rssi: int) -> int:
    """AT+CSQ index -> dBm (0 for the unknown/99 index)."""
    if rssi < 0 or rssi > 31:
        return 0
    return -113 + 2 * rssi


def rssi_bars(rssi: int) -> int:
    dbm = rssi_dbm(rssi)
    if dbm == 0:
        return 0
    for threshold, bars in ((-75, 5), (-85, 4), (-95, 3), (-105, 2)):
        if dbm >= threshold:
            return bars
    return 1


def act_is_2g(act) -> bool:
    return act in (0, 1, 3)


def act_name(act) -> str:
    return {
        0: "GSM (2G)", 1: "GSM compact (2G)", 3: "EDGE (2G)", 2: "UMTS (3G)",
        4: "HSDPA (3G)", 5: "HSUPA (3G)", 6: "HSPA (3G)", 7: "LTE (4G)",
        9: "NB-IoT", 10: "LTE Cat-M1",
    }.get(act, "unknown")


def parse_transcript(text: str) -> dict:
    """Extract the evidence lines from a raw AT transcript."""
    out = {
        "sim": None,
        "rssi": None,
        "ber": None,
        "reg": None,
        "reg_2g": None,
        "reg_lte": None,
        "oper": None,
        "act": None,
        "cgdcont_error": False,
        "cme_code": None,
        "connect": False,
        "no_carrier": False,
        "saw_ok": False,
        "saw_at": False,
        "apn": None,
    }
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        low = line.lower()
        if classify(line) == "CONNECT":
            out["connect"] = True
        elif classify(line) == "OK":
            out["saw_ok"] = True
        elif classify(line) == "NO CARRIER":
            out["no_carrier"] = True
        cme = RE_CME.search(line)
        if cme:
            out["cme_code"] = int(cme.group(1))
        m = RE_CPIN.search(line)
        if m:
            state = m.group(1).strip().upper()
            if state.startswith("READY"):
                out["sim"] = "READY"
            elif "PUK" in state:
                out["sim"] = "PUK"
            elif "PIN" in state:
                out["sim"] = "PIN"
            else:
                out["sim"] = state
        m = RE_CSQ.search(line)
        if m:
            out["rssi"], out["ber"] = int(m.group(1)), int(m.group(2))
        if "+CREG:" in line:
            out["reg_2g"] = line.split(":", 1)[1].strip()
        if "+CEREG:" in line:
            out["reg_lte"] = line.split(":", 1)[1].strip()
        m = RE_REG.search(line)
        if m:
            params = [p for p in m.group(1).split(",") if p != ""]
            out["reg"] = int(params[0]) if len(params) == 1 else int(params[1])
        m = RE_COPS.search(line)
        if m:
            out["oper"] = m.group(3).strip() or None
            out["act"] = int(m.group(4)) if m.group(4) else None
        if "cgdcont" in low:
            out["apn"] = line.split(",")[-1].strip().strip('"') or None
        if low.startswith("at+") and "cgdcont" in low:
            out["apn"] = line.split(",")[-1].strip().strip('"') or None
    if "at" in text.lower().split("\n")[0:1]:
        out["saw_at"] = True
    out["saw_at"] = bool(re.search(r"^at\s*$", text, re.I | re.M))
    return out


def diagnose(parsed: dict, cgdcont_sent: bool = True) -> str:
    """Map parsed evidence to one failure reason (mirrors gsm_diagnose())."""
    if parsed["sim"] in ("PIN", "PUK"):
        return "SIM_PIN"
    if parsed["cme_code"] in (10, 15):
        return "SIM_NOT_INSERTED"
    if parsed["cme_code"] in (11, 12):
        return "SIM_PIN"
    if parsed["reg"] == 3:
        return "REG_DENIED"
    if parsed["no_carrier"]:
        return "DIAL_FAILED"
    if parsed["cme_code"] == 3:
        return "OPERATOR_REFUSED"
    if cgdcont_sent and parsed["cgdcont_error"]:
        return "PDP_REJECTED"
    if parsed["connect"]:
        if parsed["act"] is not None and act_is_2g(parsed["act"]):
            return "TWO_G_ONLY"
        return "NONE"
    if not parsed["saw_ok"] and parsed["rssi"] is None:
        return "NO_RESPONSE"
    if parsed["reg"] == 2 or parsed["reg"] is None:
        return "NO_COVERAGE"
    return "DIAL_FAILED"


def verdict(parsed: dict) -> tuple[bool, str]:
    """(ok, reason key) for a completed bring-up attempt."""
    reason = diagnose(parsed)
    ok = reason in ("NONE", "TWO_G_ONLY")
    return ok, reason


# --------------------------------------------------------------------------- #
# reporting                                                                    #
# --------------------------------------------------------------------------- #


def report(parsed: dict, reason: str, ok: bool) -> None:
    print("")
    print("--- evidence ---")
    print(f"  SIM            : {parsed['sim'] or 'unknown'}")
    if parsed["rssi"] is None:
        print("  signal         : unknown")
    else:
        print(f"  signal         : rssi={parsed['rssi']} ber={parsed['ber']} "
              f"=> {rssi_dbm(parsed['rssi'])} dBm ({rssi_bars(parsed['rssi'])}/5 bars)")
    print(f"  registration   : {parsed['reg'] if parsed['reg'] is not None else 'unknown'}"
          f"  (CREG={parsed['reg_2g'] or '-'} CEREG={parsed['reg_lte'] or '-'})")
    if parsed["oper"] or parsed["act"] is not None:
        print(f"  operator       : {parsed['oper'] or '(numeric)'}"
              f"  act={parsed['act']} {act_name(parsed['act']) if parsed['act'] is not None else ''}")
    print(f"  apn            : {parsed['apn'] or 'unknown'}")
    print(f"  data mode      : {'CONNECT seen' if parsed['connect'] else 'not reached'}")

    print("")
    if ok:
        print(f"VERDICT: PASS — modem reached data mode. reason={FAIL[reason]}")
        if reason == "TWO_G_ONLY":
            print("WARNING: registered on a 2G cell only. The EU is retiring 2G;")
            print("         treat this as a lab result, not a deployable upstream.")
        print("NOTE   : CONNECT is where AT ends. The IP layer is proven only by")
        print("         the firmware's PPPoS attach, which is not implemented yet.")
    else:
        print(f"VERDICT: FAIL — {FAIL[reason]}")
        print("         (reason vocabulary is shared with main/gsm_upstream.c "
              "gsm_failure_name())")
    print("")


# --------------------------------------------------------------------------- #
# live run                                                                     #
# --------------------------------------------------------------------------- #


def live_run(port: str, baud: int, apn: str, cid: int, out_path: str | None,
             timeout: float) -> int:
    try:
        import serial  # pyserial
    except ImportError:
        print("ERROR: live mode needs pyserial — pip install pyserial", file=sys.stderr)
        print("       (or capture to a file elsewhere and use --replay)", file=sys.stderr)
        return 3

    transcript: list[str] = []

    def read_reply(ser, deadline: float) -> str:
        """Read lines until a final result code or the deadline."""
        buf = b""
        while time.time() < deadline:
            chunk = ser.readline()
            if not chunk:
                continue
            line = chunk.decode("ascii", "replace").strip()
            if not line:
                continue
            transcript.append(line)
            print(f"  < {line}")
            if classify(line) != "INFO":
                return line
        return ""

    def at(ser, cmd: str, timeout_s: float) -> str:
        transcript.append(f"(send) {cmd}")
        print(f"  > {cmd}")
        ser.reset_input_buffer()
        ser.write((cmd + "\r").encode())
        ser.flush()
        return read_reply(ser, time.time() + timeout_s)

    def ok(line: str) -> bool:
        return classify(line) == "OK"

    with serial.Serial(port, baud, timeout=0.3, write_timeout=2) as ser:
        print(f"--- live bring-up on {port} @ {baud} baud, apn={apn} cid={cid} ---")
        time.sleep(0.5)

        if not ok(at(ser, "AT", timeout)):
            _finish(transcript, out_path)
            parsed = parse_transcript("\n".join(transcript))
            report(parsed, "NO_RESPONSE", False)
            return 2

        if not ok(at(ser, "AT+CPIN?", timeout)):
            _finish(transcript, out_path)
            parsed = parse_transcript("\n".join(transcript))
            report(parsed, diagnose(parsed, cgdcont_sent=False), False)
            return 2

        at(ser, "AT+CSQ", timeout)
        at(ser, "AT+CREG?", timeout)
        at(ser, "AT+CEREG?", timeout)
        at(ser, "AT+COPS?", timeout)
        at(ser, "AT+CGSN", timeout)          # IMEI, for the record

        cgdcont_sent = True
        if not ok(at(ser, f'AT+CGDCONT={cid},"IP","{apn}"', timeout)):
            transcript.append("(cgdcont_error=1)")
            _finish(transcript, out_path)
            parsed = parse_transcript("\n".join(transcript))
            report(parsed, diagnose(parsed), False)
            return 2

        # ATD enters data mode; the modem answers CONNECT and then the line is
        # carrying PPP frames, so this is the last AT step.
        dial = at(ser, f"ATD*99***{cid}#", max(timeout, 60.0))
        transcript.append(f"(dial_result={classify(dial)})")

    _finish(transcript, out_path)
    text = "\n".join(transcript)
    parsed = parse_transcript(text)
    if "(cgdcont_error=1)" in text:
        parsed["cgdcont_error"] = True
    ok_run, reason = verdict(parsed)
    report(parsed, reason, ok_run)
    return 0 if ok_run else 2


def _finish(transcript: list[str], out_path: str | None) -> None:
    text = "\n".join(transcript) + "\n"
    if out_path:
        with open(out_path, "w") as fh:
            fh.write(text)
        print(f"\ntranscript written to {out_path}")


# --------------------------------------------------------------------------- #
# fixtures / replay                                                            #
# --------------------------------------------------------------------------- #

FIXTURE_OK_2G = """\
AT
OK
AT+CPIN?
+CPIN: READY
OK
AT+CSQ
+CSQ: 20,0
OK
AT+CREG?
+CREG: 0,1
OK
AT+CEREG?
ERROR
AT+COPS?
+COPS: 0,0,"Vodafone.de",0
OK
AT+CGSN
861234567890123
OK
AT+CGDCONT=1,"IP","silent.link"
OK
ATD*99***1#
CONNECT
"""

FIXTURE_FAIL_NO_SIM = """\
AT
OK
AT+CPIN?
+CME ERROR: 10
"""

FIXTURE_FAIL_NO_COVERAGE = """\
AT
OK
AT+CPIN?
+CPIN: READY
OK
AT+CSQ
+CSQ: 12,0
OK
AT+CREG?
+CREG: 0,2
OK
AT+CEREG?
+CREG: 0,2
OK
AT+COPS?
+COPS: 0,2,26201,7
OK
"""

FIXTURE_FAIL_PDP = """\
AT
OK
AT+CPIN?
+CPIN: READY
OK
AT+CSQ
+CSQ: 18,0
OK
AT+CREG?
+CREG: 0,1
OK
AT+CEREG?
+CEREG: 0,1
OK
AT+CGDCONT=1,"IP","silent.link"
+CME ERROR: 3
"""

FIXTURES = {
    "ok_2g": (FIXTURE_OK_2G, True, "TWO_G_ONLY"),
    "fail_no_sim": (FIXTURE_FAIL_NO_SIM, False, "SIM_NOT_INSERTED"),
    "fail_no_coverage": (FIXTURE_FAIL_NO_COVERAGE, False, "NO_COVERAGE"),
    "fail_pdp": (FIXTURE_FAIL_PDP, False, "OPERATOR_REFUSED"),
}


def self_test() -> int:
    """Parse the built-in fixtures — proves the parser without a modem."""
    bad = 0
    for name, (text, want_ok, want_reason) in FIXTURES.items():
        parsed = parse_transcript(text)
        if "cgdcont" in text and "+CME ERROR" in text:
            parsed["cgdcont_error"] = True
        got_ok, got_reason = verdict(parsed)
        status = "ok " if (got_ok == want_ok and got_reason == want_reason) else "BAD"
        if status == "BAD":
            bad += 1
        print(f"  [{status}] {name}: ok={got_ok} reason={got_reason} "
              f"(want ok={want_ok} reason={want_reason})")
    print(f"\nself-test: {len(FIXTURES) - bad}/{len(FIXTURES)} fixtures correct")
    return 0 if bad == 0 else 2


# --------------------------------------------------------------------------- #


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device, e.g. /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=None,
                    help="UART baud (default: 9600 for --modem sim800, else 115200)")
    ap.add_argument("--modem", choices=sorted(BAUD_BY_MODEM), default="sim7600",
                    help="modem class; sets the default baud and the expected AT surface")
    ap.add_argument("--apn", default=SILENTLINK_APN, help="PDP APN (default silent.link)")
    ap.add_argument("--cid", type=int, default=DEFAULT_CID, help="PDP context id")
    ap.add_argument("--out", default=None, help="write the raw transcript here")
    ap.add_argument("--timeout", type=float, default=10.0, help="per-command timeout, s")
    ap.add_argument("--replay", metavar="FILE", help="parse a captured transcript")
    ap.add_argument("--self-test", action="store_true", help="parse built-in fixtures")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.replay:
        if not os.path.exists(args.replay):
            print(f"ERROR: no such transcript: {args.replay}", file=sys.stderr)
            return 3
        with open(args.replay) as fh:
            text = fh.read()
        parsed = parse_transcript(text)
        if "cgdcont_error" in text:
            parsed["cgdcont_error"] = True
        ok_run, reason = verdict(parsed)
        report(parsed, reason, ok_run)
        return 0 if ok_run else 2

    if not args.port:
        ap.print_help()
        return 3

    baud = args.baud or BAUD_BY_MODEM[args.modem]
    return live_run(args.port, baud, args.apn, args.cid, args.out, args.timeout)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
