// Pattern-based line colorization for switch CLI output.
// Applied per complete line; lines that already contain ANSI escapes are left
// untouched so the device's own coloring wins.

const RESET = "\x1b[39m";
const GREEN = "\x1b[38;5;42m";
const RED = "\x1b[38;5;203m";
const YELLOW = "\x1b[38;5;215m";
const CYAN = "\x1b[38;5;80m";
const MAGENTA = "\x1b[38;5;177m";
const BLUE = "\x1b[38;5;111m";
const DIM = "\x1b[38;5;245m";

interface Rule {
  re: RegExp;
  color: string;
}

// Order matters: earlier rules win on the same span via a single pass.
const RULES: Rule[] = [
  // CLI rejection messages (Cisco-style)
  { re: /%[^\r\n]*/g, color: RED },
  // Down / disabled states
  {
    re: /\b(administratively\s+down|err-disabled|errdisable|notconnect|shutdown|disabled|inactive)\b/gi,
    color: YELLOW,
  },
  { re: /\bdown\b/gi, color: RED },
  // Up / good states
  { re: /\b(up|connected|active|forwarding|established|reachable)\b/gi, color: GREEN },
  // Interface names (Cisco / generic)
  {
    re: /\b(?:Gi|Fa|Te|Fo|Hu|Twe|Eth|Ethernet|GigabitEthernet|FastEthernet|TenGigE|TenGigabitEthernet|TwentyFiveGigE|FortyGigE|HundredGigE|Port-channel|Po|Vlan|Loopback|Lo|Tunnel|Tu|Serial)\s?\d+(?:\/\d+)*(?:\.\d+)?\b/g,
    color: CYAN,
  },
  // VLAN IDs (when written as "VLAN 10" / "vlan10")
  { re: /\bVLAN\s?\d+\b/gi, color: MAGENTA },
  // MAC addresses (Cisco aaaa.bbbb.cccc and colon/dash forms)
  { re: /\b[0-9a-f]{4}\.[0-9a-f]{4}\.[0-9a-f]{4}\b/gi, color: MAGENTA },
  { re: /\b(?:[0-9a-f]{2}[:\-]){5}[0-9a-f]{2}\b/gi, color: MAGENTA },
  // IPv4 with optional mask
  { re: /\b\d{1,3}(?:\.\d{1,3}){3}(?:\/\d{1,2})?\b/g, color: BLUE },
  // Timestamps like 00:01:23 or 1d23h
  { re: /\b\d{1,3}:\d{2}:\d{2}\b/g, color: DIM },
  { re: /\b\d+[dhmw]\d+[hms]\b/g, color: DIM },
];

interface Match {
  start: number;
  end: number;
  color: string;
}

function colorizeLine(line: string): string {
  if (!line) return line;
  // Don't touch lines the device already colored.
  if (line.indexOf("\x1b[") !== -1) return line;

  // Collect non-overlapping matches with first-rule priority.
  const matches: Match[] = [];
  for (const rule of RULES) {
    rule.re.lastIndex = 0;
    let m: RegExpExecArray | null;
    while ((m = rule.re.exec(line)) !== null) {
      const start = m.index;
      const end = start + m[0].length;
      if (m[0].length === 0) {
        rule.re.lastIndex++;
        continue;
      }
      let overlap = false;
      for (const x of matches) {
        if (start < x.end && end > x.start) {
          overlap = true;
          break;
        }
      }
      if (!overlap) matches.push({ start, end, color: rule.color });
      if (!rule.re.global) break;
    }
  }
  if (matches.length === 0) return line;

  matches.sort((a, b) => a.start - b.start);
  let out = "";
  let i = 0;
  for (const m of matches) {
    if (m.start > i) out += line.slice(i, m.start);
    out += m.color + line.slice(m.start, m.end) + RESET;
    i = m.end;
  }
  if (i < line.length) out += line.slice(i);
  return out;
}

// Stateful colorizer per session: buffers a possibly-partial trailing line so
// regexes only ever run on complete lines.
export function makeColorizer() {
  const decoder = new TextDecoder("utf-8", { fatal: false });

  return function transform(bytes: Uint8Array): string {
    const text = decoder.decode(bytes, { stream: true });
    let out = "";
    let i = 0;
    while (i < text.length) {
      const nl = text.indexOf("\n", i);
      if (nl === -1) {
        // Trailing partial line — write through plain for responsive echo.
        out += text.slice(i);
        break;
      }
      const hasCR = nl > 0 && text.charCodeAt(nl - 1) === 13;
      const line = text.slice(i, hasCR ? nl - 1 : nl);
      out += colorizeLine(line) + (hasCR ? "\r\n" : "\n");
      i = nl + 1;
    }
    return out;
  };
}
