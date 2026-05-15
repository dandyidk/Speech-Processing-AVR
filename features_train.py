"""
train_dtw_32k.py
================
Quantized DTW Training Pipeline for Isolated-Word Speech Recognition
Target: ATmega32A @ 11.0592MHz with 32KB External SRAM (No FPU)

Accepts pre-extracted feature files (.txt) in the format:
  F0:[73,0,55,0,194,117,61,20]
  F1:[85,0,83,0,88,57,48,14]
  ...

Dataset folder structure:
  DATASET_DIR/
    word1/
      sample1.txt
      sample2.txt
    word2/
      ...
"""

import os
import glob
import re
import numpy as np
from sklearn.model_selection import train_test_split

# ===========================================================================
# Configuration
# ===========================================================================
DATASET_DIR      = "/home/dandy/dataset/dataset"
N_FEATURES       = 8
K_TEMPLATES      = 2               # medoid templates per word
SAKOE_CHIBA_W    = 8             # DTW Sakoe-Chiba band width
RANDOM_SEED      = 42
SAMPLES_PER_WORD = 1000
TEST_SPLIT       = 0.15
OUTPUT_HEADER    = "dtw_templates.h"

# Must match #define VAD_THRESHOLD in dtw.c exactly.
VAD_THRESHOLD    = 100


# ===========================================================================
# 1. Load feature file
# ===========================================================================
def load_feature_file(file_path):
    """
    Parse a .txt feature file in the format:
      F0:[73,0,55,0,194,117,61,20]
      F1:[85,0,83,0,88,57,48,14]
      ...

    Returns: (num_frames, N_FEATURES) uint8 numpy array, or None on failure.
    """
    frames = []
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                # Match FN:[v0,v1,...,v7]
                m = re.match(r'F\d+:\[([^\]]+)\]', line)
                if not m:
                    continue
                vals = [int(v.strip()) for v in m.group(1).split(',')]
                if len(vals) != N_FEATURES:
                    print(f"  [WARN] Expected {N_FEATURES} features, got {len(vals)} in {file_path}")
                    continue
                frames.append(vals)
    except Exception as e:
        print(f"  [WARN] Skipping {os.path.basename(file_path)}: {e}")
        return None

    if len(frames) < 3:
        return None

    return np.array(frames, dtype=np.uint8)


# ===========================================================================
# 2. VAD Trim
# ===========================================================================
def vad_trim(seq, threshold=VAD_THRESHOLD):
    """
    Remove leading/trailing frames whose quantized STE (feature 0)
    is below threshold. Mirrors VAD in dtw.c.
    """
    ste    = seq[:, 0].astype(np.int16)
    active = np.where(ste >= threshold)[0]
    if len(active) == 0:
        return seq
    return seq[active[0]:active[-1] + 1]


# ===========================================================================
# 3. DTW Distance (Manhattan + Sakoe-Chiba Band)
# ===========================================================================
def dtw_distance_uint8(a, b, w=SAKOE_CHIBA_W):
    WEIGHTS = np.array([1, 1, 1, 1, 2, 2, 2, 2], dtype=np.int16)

    N, M = len(a), len(b)
    w = max(SAKOE_CHIBA_W, abs(N - M))

    INF = 10**9
    prev = np.full(M, INF, dtype=np.int32)
    curr = np.full(M, INF, dtype=np.int32)

    # init row
    for j in range(M):
        if abs(j) <= w:
            diff = np.abs(a[0].astype(np.int32) - b[j].astype(np.int32))
            cost = np.sum(diff * WEIGHTS)
            prev[j] = cost if j == 0 else prev[j-1] + cost

    # DP
    for i in range(1, N):
        curr[:] = INF
        j_start = max(0, i - w)
        j_end   = min(M, i + w + 1)

        for j in range(j_start, j_end):
            diff = np.abs(a[i].astype(np.int32) - b[j].astype(np.int32))
            cost = np.sum(diff * WEIGHTS)

            best = INF
            if j > 0: best = min(best, curr[j-1])
            best = min(best, prev[j])
            if j > 0: best = min(best, prev[j-1])

            curr[j] = cost + best

        prev, curr = curr, prev

    return prev[M - 1] / (N + M)


# ===========================================================================
# 4. K-Medoids Template Selection
# ===========================================================================
def kmedoids_select(sequences, k=K_TEMPLATES):
    """
    Greedy K-Medoids on already VAD-trimmed uint8 sequences.
    """
    n = len(sequences)
    if n <= k:
        return list(range(n))

    print(f"    Computing pairwise DTW matrix ({n}x{n})...", end=" ", flush=True)
    dist_matrix = np.zeros((n, n), dtype=np.float64)
    for i in range(n):
        for j in range(i + 1, n):
            d = dtw_distance_uint8(sequences[i], sequences[j])
            dist_matrix[i, j] = d
            dist_matrix[j, i] = d
    print("Done.")

    selected = [int(np.argmin(dist_matrix.sum(axis=1)))]
    for _ in range(1, k):
        best_candidate = -1
        best_cost = float('inf')
        for c in range(n):
            if c in selected:
                continue
            candidate_set = selected + [c]
            cost = sum(min(dist_matrix[s, m] for m in candidate_set) for s in range(n))
            if cost < best_cost:
                best_cost      = cost
                best_candidate = c
        selected.append(best_candidate)

    return selected


# ===========================================================================
# 5. C-Header Export
# ===========================================================================
def export_c_header(words, templates_per_word, output_path):
    """
    Generate dtw_templates.h for AVR-GCC.
    Features are already uint8 — no float min/max needed.
    feat_min/feat_max stubs are exported as all-zeros/all-255 so dsp.c
    compiles unchanged (quantization is bypassed since data is pre-quantized).
    """
    n_words         = len(words)
    total_templates = sum(len(t) for t in templates_per_word)
    max_frames      = max(max(len(seq) for seq in tpls) for tpls in templates_per_word)

    with open(output_path, "w") as f:
        f.write("/*\n")
        f.write(" * dtw_templates.h  — AUTO-GENERATED, DO NOT EDIT\n")
        f.write(" * Quantized DTW Templates for Isolated-Word Speech Recognition\n")
        f.write(" * Target: ATmega32A + 32KB External SRAM\n")
        f.write(f" * Words: {n_words}  Templates/Word: {K_TEMPLATES}  Features: {N_FEATURES}\n")
        f.write(f" * Metric: Weighted Manhattan DTW  Band: w={SAKOE_CHIBA_W}\n")
        f.write(f" * VAD threshold: {VAD_THRESHOLD} (quantized STE, 0-255)\n")
        f.write(f" * Max template frames: {max_frames}\n")
        f.write(" * NOTE: Features loaded directly from pre-quantized txt files.\n")
        f.write(" *       feat_min/feat_max are identity (0/255) — dsp.c clamps cleanly.\n")
        f.write(" */\n\n")
        f.write("#ifndef DTW_TEMPLATES_H\n#define DTW_TEMPLATES_H\n\n")
        f.write("#include <stdint.h>\n#include <avr/pgmspace.h>\n\n")

        f.write(f"#define N_WORDS            {n_words}\n")
        f.write(f"#define N_FEATURES         {N_FEATURES}\n")
        f.write(f"#define TEMPLATES_PER_WORD {K_TEMPLATES}\n")
        f.write(f"#define TOTAL_TEMPLATES    {total_templates}\n")
        f.write(f"#define MAX_TEMPLATE_FRAMES {max_frames}\n")
        f.write(f"#define DTW_BAND_W         {SAKOE_CHIBA_W}\n")
        f.write(f"#define VAD_THRESHOLD      {VAD_THRESHOLD}\n\n")

        # Word label strings
        f.write("// Word label strings (in Flash)\n")
        for i, w in enumerate(words):
            f.write(f'static const char word_str_{i}[] PROGMEM = "{w}";\n')
        f.write(f"const char* const word_labels[N_WORDS] PROGMEM = {{\n")
        for i in range(n_words):
            comma = "," if i < n_words - 1 else ""
            f.write(f"    word_str_{i}{comma}\n")
        f.write("};\n\n")

        # Identity min/max so dsp.c compiles without modification
        f.write("// feat_min / feat_max: identity mapping (data is pre-quantized)\n")
        f.write(f"const float feat_min[N_FEATURES] PROGMEM = {{\n    ")
        f.write(", ".join(["0.00000000f"] * N_FEATURES))
        f.write("\n};\n\n")
        f.write(f"const float feat_max[N_FEATURES] PROGMEM = {{\n    ")
        f.write(", ".join(["255.00000000f"] * N_FEATURES))
        f.write("\n};\n\n")

        # Template-to-word label mapping
        labels = []
        for word_idx, tpls in enumerate(templates_per_word):
            labels.extend([str(word_idx)] * len(tpls))
        f.write("// Maps template index -> word index\n")
        f.write(f"const uint8_t template_labels[TOTAL_TEMPLATES] PROGMEM = {{\n    ")
        f.write(", ".join(labels))
        f.write("\n};\n\n")

        # Template lengths
        lengths = [str(len(seq)) for tpls in templates_per_word for seq in tpls]
        f.write("// Number of (VAD-trimmed) frames per template\n")
        f.write(f"const uint8_t template_lengths[TOTAL_TEMPLATES] PROGMEM = {{\n    ")
        f.write(", ".join(lengths))
        f.write("\n};\n\n")

        # Template data
        f.write("// ===== Quantized uint8_t Template Data =====\n\n")
        template_idx = 0
        for word_idx, word in enumerate(words):
            for seq in templates_per_word[word_idx]:
                num_frames = len(seq)
                f.write(f"// Template {template_idx}: '{word}' ({num_frames} frames)\n")
                f.write(f"const uint8_t template_{template_idx}"
                        f"[{num_frames}][N_FEATURES] PROGMEM = {{\n")
                for fi, frame in enumerate(seq):
                    vals  = ", ".join(f"{int(v):3d}" for v in frame)
                    comma = "," if fi < num_frames - 1 else ""
                    f.write(f"    {{{vals}}}{comma}\n")
                f.write("};\n\n")
                template_idx += 1

        # Pointer array
        f.write("// Pointer array for indexed access\n")
        f.write(f"const uint8_t* const template_ptrs[TOTAL_TEMPLATES] PROGMEM = {{\n")
        for i in range(total_templates):
            comma = "," if i < total_templates - 1 else ""
            f.write(f"    (const uint8_t*)template_{i}{comma}\n")
        f.write("};\n\n")

        f.write("#endif // DTW_TEMPLATES_H\n")

    print(f"  [OK] C header written to '{output_path}'")


# ===========================================================================
# Main Pipeline
# ===========================================================================
def main():
    print("=" * 70)
    print("  Quantized DTW Training Pipeline  (pre-quantized .txt features)")
    print("  Target: ATmega32A @ 11.0592MHz + 32KB External SRAM")
    print("=" * 70)

    if not os.path.exists(DATASET_DIR):
        print(f"[ERROR] Dataset directory '{DATASET_DIR}' not found.")
        return

    word_folders = sorted([
        d for d in os.listdir(DATASET_DIR)
        if os.path.isdir(os.path.join(DATASET_DIR, d))
    ])
    if not word_folders:
        print("[ERROR] No word folders found.")
        return

    N_WORDS = len(word_folders)
    print(f"\n[1/6] Detected {N_WORDS} classes: {', '.join(word_folders)}")

    # ------------------------------------------------------------------
    # Step 1: Load .txt feature files
    # ------------------------------------------------------------------
    print(f"\n[2/6] Loading pre-quantized feature files...")
    all_sequences = {}

    for word in word_folders:
        word_dir  = os.path.join(DATASET_DIR, word)
        txt_files = sorted(glob.glob(os.path.join(word_dir, "**", "*.txt"), recursive=True))

        seqs = []
        for fpath in txt_files:
            seq = load_feature_file(fpath)
            if seq is not None:
                seqs.append(seq)
            if len(seqs) >= SAMPLES_PER_WORD:
                break

        all_sequences[word] = seqs
        frame_counts = [len(s) for s in seqs]
        print(f"  {word:>12s}: {len(seqs)} samples  "
              f"frames/sample: {min(frame_counts)}-{max(frame_counts)}")

    # ------------------------------------------------------------------
    # Step 2: VAD trim
    # ------------------------------------------------------------------
    print(f"\n[3/6] Applying VAD trim (threshold={VAD_THRESHOLD})...")
    for word in word_folders:
        trimmed = [vad_trim(s) for s in all_sequences[word]]
        trimmed = [s for s in trimmed if len(s) >= 2]
        all_sequences[word] = trimmed
        frame_counts = [len(s) for s in trimmed]
        if frame_counts:
            print(f"  {word:>12s}: frames/sample after trim: "
                  f"{min(frame_counts)}-{max(frame_counts)} "
                  f"(mean {np.mean(frame_counts):.1f})")

    # ------------------------------------------------------------------
    # Step 3: Train / test split
    # ------------------------------------------------------------------
    print(f"\n[4/6] Splitting {int((1-TEST_SPLIT)*100)}/{int(TEST_SPLIT*100)} "
          f"train/test (seed={RANDOM_SEED})...")
    train_seqs = {}
    test_seqs  = {}
    for word in word_folders:
        seqs = all_sequences[word]
        if len(seqs) < 2:
            train_seqs[word] = seqs
            test_seqs[word]  = seqs
        else:
            tr, te = train_test_split(seqs, test_size=TEST_SPLIT,
                                      random_state=RANDOM_SEED)
            train_seqs[word] = tr
            test_seqs[word]  = te
        print(f"  {word:>12s}: {len(train_seqs[word])} train / "
              f"{len(test_seqs[word])} test")

    # ------------------------------------------------------------------
    # Step 4: K-Medoids template selection
    # ------------------------------------------------------------------
    print(f"\n[5/6] Selecting {K_TEMPLATES} medoid templates per word...")
    templates_per_word = []

    for word in word_folders:
        print(f"  Processing '{word}'  ({len(train_seqs[word])} sequences)...")
        q_seqs = train_seqs[word]

        if len(q_seqs) <= K_TEMPLATES:
            medoid_indices = list(range(len(q_seqs)))
        else:
            medoid_indices = kmedoids_select(q_seqs, k=K_TEMPLATES)

        selected = [q_seqs[i] for i in medoid_indices]
        templates_per_word.append(selected)
        for idx in medoid_indices:
            print(f"    Medoid #{idx}: {len(q_seqs[idx])} frames")

    # ------------------------------------------------------------------
    # Step 5: Evaluate on held-out test set
    # ------------------------------------------------------------------
    print(f"\n[6/6] Evaluating on test set (DTW band w={SAKOE_CHIBA_W})...")

    all_templates       = [t for tpls in templates_per_word for t in tpls]
    all_template_labels = [wi for wi, tpls in enumerate(templates_per_word)
                           for _ in tpls]

    total_correct = 0
    total_count   = 0
    conf_matrix   = np.zeros((N_WORDS, N_WORDS), dtype=int)

    for true_idx, word in enumerate(word_folders):
        word_correct = 0
        for test_seq in test_seqs[word]:
            distances = [dtw_distance_uint8(test_seq, tmpl) for tmpl in all_templates]
            pred_idx  = all_template_labels[int(np.argmin(distances))]
            conf_matrix[true_idx, pred_idx] += 1
            if pred_idx == true_idx:
                word_correct  += 1
                total_correct += 1
            total_count += 1

        n   = len(test_seqs[word])
        acc = (word_correct / n * 100) if n > 0 else 0.0
        print(f"  {word:>12s}: {acc:6.2f}%  ({word_correct}/{n})")

    overall = (total_correct / total_count * 100) if total_count > 0 else 0.0
    print(f"\n  >>> Overall Accuracy: {overall:.2f}%  ({total_correct}/{total_count})")

    print(f"\n{'':=<70}")
    print("  Confusion Matrix  (rows=True, cols=Predicted)")
    print(f"{'':=<70}")
    header = f"  {'':14s}" + "".join(f"{w:<12s}" for w in word_folders)
    print(header)
    print("  " + "-" * (14 + 12 * N_WORDS))
    for i, w in enumerate(word_folders):
        row = f"  {w:<14s}" + "".join(f"{conf_matrix[i,j]:<12d}" for j in range(N_WORDS))
        print(row)

    # ------------------------------------------------------------------
    # Export
    # ------------------------------------------------------------------
    print(f"\n  Exporting C header...")
    total_bytes = sum(seq.shape[0] * seq.shape[1]
                      for tpls in templates_per_word for seq in tpls)
    print(f"  Template data: {total_bytes} bytes "
          f"({total_bytes / 1024:.1f} KB Flash)")

    export_c_header(
        words              = word_folders,
        templates_per_word = templates_per_word,
        output_path        = OUTPUT_HEADER,
    )

    # Save .npz for offline testing
    model_path      = "dtw_model.npz"
    flat_templates  = [seq for tpls in templates_per_word for seq in tpls]
    flat_labels     = [wi  for wi, tpls in enumerate(templates_per_word) for _ in tpls]
    flat_lengths    = [len(seq) for seq in flat_templates]

    save_dict = {
        "word_labels":      np.array(word_folders),
        "template_labels":  np.array(flat_labels,  dtype=np.uint8),
        "template_lengths": np.array(flat_lengths, dtype=np.uint8),
        "n_templates":      np.array(len(flat_templates)),
        "vad_threshold":    np.array(VAD_THRESHOLD, dtype=np.uint8),
    }
    for i, t in enumerate(flat_templates):
        save_dict[f"template_{i}"] = t
    np.savez(model_path, **save_dict)
    print(f"  [OK] Python model saved to '{model_path}'")

    print(f"\n{'':=<70}")
    print("  Pipeline Complete!")
    print(f"{'':=<70}")


if __name__ == "__main__":
    import warnings
    warnings.filterwarnings("ignore")
    main()
