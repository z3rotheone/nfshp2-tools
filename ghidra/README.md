# Function map

Derived by static analysis of `SLUS_203.62` (NTSC, 2.3 MB). No game code is
reproduced here — these are addresses and the source file names that the
binary's own debug strings disclose.

## Files

| File | Contents |
|---|---|
| `functions.csv` | all 12,094 function boundaries, with size and call counts |
| `functions_attributed.csv` | the 377 tied to a source file, each marked `direct` or `inferred` |
| `sites.csv` | 262 call sites carrying an original file name and line number |
| `HP2Import.java` | applies the above in Ghidra (no Python needed) |
| `ghidra_import_hp2.py` | the same, for PyGhidra; needs Ghidra started in PyGhidra mode |

## How it was found

The binary passes source paths and line numbers to a debug allocator at
`0x2577c0`, called as `(size, filename, line, alignment)`. Each such call site
therefore names a file and a line from the original build, and the function
containing it belongs to that file.

That gives **199 direct attributions**. The rest were propagated through the
call graph under a strict rule: a function is attributed only when *every* one
of its callers is already attributed and they all agree. A looser rule that
merely required the attributed callers to agree produced 1,426 attributions,
but 590 of those landed in `racecoordinator.cpp` — the main loop calls
everything, so the inference leaked through it. Treat `inferred` rows as a
strong hint, not a fact.

## Use

1. Open `SLUS_203.62` in Ghidra with the
   [Emotion Engine extension](https://github.com/chaoticgd/ghidra-emotionengine-reloaded).
   The extension version must match your Ghidra version.
2. Let auto-analysis finish completely, then save (Ctrl+S).
3. `Window` → `Script Manager` → add this folder via *Manage Script
   Directories* → run `HP2Import.java` → point it at this folder.
4. Save again.

Function names come out as `<sourcefile>_<n>_<address>`, and each allocation
site gets a pre-comment with its file and line.
