# Ransomware Simulator — Safe Demo (Win32 C)

> **⚠️ IMPORTANT: This is a SAFE EDUCATIONAL TOOL. It does NOT encrypt, damage, or exfiltrate any real data.**

---

## What this is

A Windows desktop application that **visually simulates** a ransomware attack for security awareness training, red team demonstrations, and educational purposes.

### What it DOES
- Creates a temporary sandbox folder with decoy files and renames them to `.locked` (simulated encryption)
- Displays a fullscreen ransomware-style UI with a countdown timer and fake process kill log
- Temporarily changes the desktop wallpaper to a ransomware-style warning image
- Restores the original wallpaper automatically on exit

### What it does NOT do
- ❌ Encrypt any real files
- ❌ Terminate any real processes
- ❌ Connect to any network
- ❌ Write to the registry (beyond temporary wallpaper setting)
- ❌ Persist after exit (no startup entry, no scheduled task)
- ❌ Exfiltrate any data

---

## Requirements

- Windows 7 or later (Win32 API)
- No administrator privileges required

---

## Build

**MSVC:**
```
cl safe_sim.c /link user32.lib gdi32.lib shell32.lib advapi32.lib
```

**MinGW:**
```
gcc safe_sim.c -o safe_sim.exe -mwindows -luser32 -lgdi32 -lshell32 -ladvapi32
```

---

## How to exit

| Method | Behaviour |
|--------|-----------|
| `Esc` key | Confirmation dialog, then exit |
| `EXIT SIMULATOR` button | Confirmation dialog, then exit |
| `Alt+F4` | Confirmation dialog, then exit |

The wallpaper is restored on every exit path, including crashes (via `atexit`).

---

## Cleanup

After exiting, delete the sandbox folder manually if needed:

```
%USERPROFILE%\RansomwareSimSandbox
```

---

## Intended use cases

- Security awareness training for employees
- Red team / blue team exercise demonstrations
- University or bootcamp cybersecurity courses
- Personal learning about ransomware behavior

---

## ⚠️ Antivirus notice

Most antivirus and EDR solutions (including Windows Defender) will flag this binary due to its visual behavior, even though it causes no real harm. This is expected. Add an exclusion for the file in your AV settings before running in a lab environment, and **inform participants before running the demo**.

---

## Legal & ethical conditions of use

By using, building, or sharing this tool you agree that:

1. You will only run it on machines you **own** or have **explicit written permission** to use.
2. You will **inform all participants** that this is a simulation before it runs.
3. You will **not modify it** to perform real harmful actions (real encryption, real process termination, network exfiltration, persistence, etc.).
4. You will **not use it** to deceive, extort, or cause distress to anyone.
5. You understand that running this on another person's machine without consent may be **illegal** under computer crime laws in your jurisdiction.

See `LICENSE` for full terms.

---

## Contributing

Pull requests welcome for additional visual effects, platform support, or safer sandbox cleanup. Please do not submit PRs that add any real harmful capability.
y
