"""
train_dtw_32k.py
================
Quantized DTW Training Pipeline for Isolated-Word Speech Recognition
Target: ATmega32A @ 11.0592MHz with 32KB External SRAM (No FPU)

Modification: exhaustive weight search over all combinations,
scored on full training accuracy (all samples vs medoid templates).
"""

import os
import glob
import re
import numpy as np
from sklearn.model_selection import train_test_split
import itertools

# ===========================================================================
# Configuration
# ===========================================================================
DATASET_DIR      = "dataset"
N_FEATURES       = 9
K_TEMPLATES      = 8
SAKOE_CHIBA_W    = 9
RANDOM_SEED      = 42
SAMPLES_PER_WORD = 1000
TEST_SPLIT       = 0.01
OUTPUT_HEADER    = "dtw_templates.h"
VAD_THRESHOLD    = 100

# Weight candidates per feature — keep small or combos explode
# 8 features × 4 values = 4^8 = 65,536 combinations (fast enough)
FEATURE_WEIGHT_SPACE = {
    "energy": [ 2],
    "zcr":    [2],
    "ssc":    [1],
    "ser":    [ 1],
    "mel1":   [ 3],
    "mel2":   [  4],
    "mel3":   [  4],
    "mel4":   [ 4],
    "delta_energy":   [6],
}

# ===========================================================================
# 1. Load feature file
# ===========================================================================
def load_feature_file(file_path):
    frames = []
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                m = re.match(r'F\d+:\[([^\]]+)\]', line)
                if not m:
                    continue
                vals = [int(v.strip()) for v in m.group(1).split(',')]
                if len(vals) != N_FEATURES:
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
    ste    = seq[:, 0].astype(np.int16)
    active = np.where(ste >= threshold)[0]
    if len(active) == 0:
        return seq
    return seq[active[0]:active[-1] + 1]


# ===========================================================================
# 3. DTW Distance — weights now a required parameter
# ===========================================================================
def dtw_distance(a, b, weights):
    N, M = len(a), len(b)
    w    = max(SAKOE_CHIBA_W, abs(N - M))

    INF  = 10 ** 9
    prev = np.full(M, INF, dtype=np.int32)
    curr = np.full(M, INF, dtype=np.int32)

    for j in range(M):
        if j <= w:
            diff    = np.abs(a[0].astype(np.int32) - b[j].astype(np.int32))
            cost    = int(np.dot(diff, weights))
            prev[j] = cost if j == 0 else prev[j - 1] + cost

    for i in range(1, N):
        curr[:] = INF
        j_start = max(0, i - w)
        j_end   = min(M, i + w + 1)

        for j in range(j_start, j_end):
            diff = np.abs(a[i].astype(np.int32) - b[j].astype(np.int32))
            cost = int(np.dot(diff, weights))

            best = prev[j]
            if j > 0:
                best = min(best, curr[j - 1], prev[j - 1])

            curr[j] = cost + best

        prev, curr = curr, prev

    return prev[M - 1] / (N + M)


# ===========================================================================
# 4. K-Medoids
# ===========================================================================
def kmedoids_select(sequences, weights, k=K_TEMPLATES):
    n = len(sequences)
    if n <= k:
        return list(range(n))

    dist_matrix = np.zeros((n, n), dtype=np.float64)
    for i in range(n):
        for j in range(i + 1, n):
            d = dtw_distance(sequences[i], sequences[j], weights)
            dist_matrix[i, j] = d
            dist_matrix[j, i] = d

    selected = [int(np.argmin(dist_matrix.sum(axis=1)))]
    for _ in range(1, k):
        best_candidate = -1
        best_cost      = float('inf')
        for c in range(n):
            if c in selected:
                continue
            candidate_set = selected + [c]
            cost = sum(
                min(dist_matrix[s, m] for m in candidate_set)
                for s in range(n)
            )
            if cost < best_cost:
                best_cost      = cost
                best_candidate = c
        selected.append(best_candidate)

    return selected


# ===========================================================================
# 5. Classify one sequence
# ===========================================================================
def classify_one(seq, templates, labels, weights):
    distances = [dtw_distance(seq, t, weights) for t in templates]
    return labels[int(np.argmin(distances))]


# ===========================================================================
# 6. Score a weight combo: build medoids from train, classify all train samples
# ===========================================================================
def score_weights(weights, train_seqs, word_folders):
    all_templates = []
    all_labels    = []

    for word_idx, word in enumerate(word_folders):
        seqs = train_seqs[word]
        if len(seqs) <= K_TEMPLATES:
            medoid_indices = list(range(len(seqs)))
        else:
            medoid_indices = kmedoids_select(seqs, weights, k=K_TEMPLATES)

        for mi in medoid_indices:
            all_templates.append(seqs[mi])
            all_labels.append(word_idx)

    correct = 0
    total   = 0
    for true_idx, word in enumerate(word_folders):
        for seq in train_seqs[word]:
            pred = classify_one(seq, all_templates, all_labels, weights)
            if pred == true_idx:
                correct += 1
            total += 1

    return correct / total if total > 0 else 0.0


# ===========================================================================
# 7. Exhaustive weight search
# ===========================================================================
def find_best_weights(train_seqs, word_folders):

    feature_names = list(FEATURE_WEIGHT_SPACE.keys())

    candidate_lists = [
        FEATURE_WEIGHT_SPACE[name]
        for name in feature_names
    ]

    all_combos = itertools.product(*candidate_lists)

    # count total combinations
    total = 1
    for c in candidate_lists:
        total *= len(c)

    print(f"\n  Total combinations: {total:,}")

    best_acc = -1.0
    best_weights = np.ones(N_FEATURES, dtype=np.int16)

    for i, combo in enumerate(all_combos):
        print(f"\n  Evaluating combo {i+1:,}/{total:,} ...")

        weights = np.array(combo, dtype=np.int16)

        acc = score_weights(
            weights,
            train_seqs,
            word_folders
        )

        if (i + 1) % 100 == 0:
            print(
                f"  [{i+1:,}/{total:,}] "
                f"best={best_acc*100:.2f}% "
                f"current={acc*100:.2f}% "
                f"weights={list(weights)}"
            )

        if acc > best_acc:

            best_acc = acc
            best_weights = weights.copy()

            print(
                f"\n  *** NEW BEST ***\n"
                f"  combo   : {i+1:,}/{total:,}\n"
                f"  acc     : {acc*100:.2f}%\n"
                f"  weights :"
            )

            for fname, w in zip(feature_names, weights):
                print(f"     {fname:<8s} = {w}")

        if best_acc >= 1.0:
            print("\n  Perfect accuracy reached.")
            break

    return best_weights, best_acc
# ===========================================================================
# 8. C-Header Export
# ===========================================================================
def export_c_header(words, templates_per_word, best_weights, output_path):
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
        f.write(f" * Best weights: {list(best_weights)}\n")
        f.write(" */\n\n")
        f.write("#ifndef DTW_TEMPLATES_H\n#define DTW_TEMPLATES_H\n\n")
        f.write("#include <stdint.h>\n#include <avr/pgmspace.h>\n\n")

        f.write(f"#define N_WORDS             {n_words}\n")
        f.write(f"#define N_FEATURES          {N_FEATURES}\n")
        f.write(f"#define TEMPLATES_PER_WORD  {K_TEMPLATES}\n")
        f.write(f"#define TOTAL_TEMPLATES     {total_templates}\n")
        f.write(f"#define MAX_TEMPLATE_FRAMES {max_frames}\n")
        f.write(f"#define DTW_BAND_W          {SAKOE_CHIBA_W}\n")
        f.write(f"#define VAD_THRESHOLD       {VAD_THRESHOLD}\n\n")

        f.write("// Feature weights found by exhaustive search\n")
        f.write(f"const uint8_t feature_weights[N_FEATURES] PROGMEM = {{\n    ")
        f.write(", ".join(str(int(w)) for w in best_weights))
        f.write("\n};\n\n")

        f.write("// Word label strings (in Flash)\n")
        for i, w in enumerate(words):
            f.write(f'static const char word_str_{i}[] PROGMEM = "{w}";\n')
        f.write(f"const char* const word_labels[N_WORDS] PROGMEM = {{\n")
        for i in range(n_words):
            comma = "," if i < n_words - 1 else ""
            f.write(f"    word_str_{i}{comma}\n")
        f.write("};\n\n")

        f.write("const float feat_min[N_FEATURES] PROGMEM = {{\n    ")
        f.write(", ".join(["0.00000000f"] * N_FEATURES))
        f.write("\n};\n\n")
        f.write("const float feat_max[N_FEATURES] PROGMEM = {{\n    ")
        f.write(", ".join(["255.00000000f"] * N_FEATURES))
        f.write("\n};\n\n")

        labels = []
        for word_idx, tpls in enumerate(templates_per_word):
            labels.extend([str(word_idx)] * len(tpls))
        f.write(f"const uint8_t template_labels[TOTAL_TEMPLATES] PROGMEM = {{\n    ")
        f.write(", ".join(labels))
        f.write("\n};\n\n")

        lengths = [str(len(seq)) for tpls in templates_per_word for seq in tpls]
        f.write(f"const uint8_t template_lengths[TOTAL_TEMPLATES] PROGMEM = {{\n    ")
        f.write(", ".join(lengths))
        f.write("\n};\n\n")

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

        f.write(f"const uint8_t* const template_ptrs[TOTAL_TEMPLATES] PROGMEM = {{\n")
        for i in range(total_templates):
            comma = "," if i < total_templates - 1 else ""
            f.write(f"    (const uint8_t*)template_{i}{comma}\n")
        f.write("};\n\n")
        f.write("#endif // DTW_TEMPLATES_H\n")

    print(f"  [OK] C header written to '{output_path}'")


# ===========================================================================
# Main
# ===========================================================================
def main():
    print("=" * 70)
    print("  Quantized DTW Training Pipeline — Exhaustive Weight Search")
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
    print(f"\n[1/5] Detected {N_WORDS} classes: {', '.join(word_folders)}")

    # ------------------------------------------------------------------
    # Load
    # ------------------------------------------------------------------
    print(f"\n[2/5] Loading feature files...")
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
              f"frames: {min(frame_counts)}-{max(frame_counts)}")

    # ------------------------------------------------------------------
    # VAD trim
    # ------------------------------------------------------------------
    print(f"\n[3/5] VAD trim (threshold={VAD_THRESHOLD})...")
    for word in word_folders:
        trimmed = [vad_trim(s) for s in all_sequences[word]]
        trimmed = [s for s in trimmed if len(s) >= 2]
        all_sequences[word] = trimmed
        frame_counts = [len(s) for s in trimmed]
        print(f"  {word:>12s}: frames after trim: "
              f"{min(frame_counts)}-{max(frame_counts)}  "
              f"(mean {np.mean(frame_counts):.1f})")

    # ------------------------------------------------------------------
    # Train/test split
    # ------------------------------------------------------------------
    print(f"\n[4/5] Train/test split ({int((1-TEST_SPLIT)*100)}/{int(TEST_SPLIT*100)})...")
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
        print(f"  {word:>12s}: {len(train_seqs[word])} train / {len(test_seqs[word])} test")

    # ------------------------------------------------------------------
    # Exhaustive weight search on training set only
    # ------------------------------------------------------------------
    print(f"\n[5/5] Exhaustive weight search on TRAIN set...")
    best_weights, best_train_acc = find_best_weights(train_seqs, word_folders)

    print(f"\n  ══════════════════════════════════════")
    print(f"  Best train accuracy : {best_train_acc * 100:.2f}%")
    print(f"  Best weights        : {list(best_weights)}")
    print(f"  ══════════════════════════════════════")

    # ------------------------------------------------------------------
    # Final medoid selection with best weights
    # ------------------------------------------------------------------
    print(f"\n  Building final templates with best weights...")
    templates_per_word  = []
    all_templates       = []
    all_template_labels = []

    for word_idx, word in enumerate(word_folders):
        seqs = train_seqs[word]
        if len(seqs) <= K_TEMPLATES:
            medoid_indices = list(range(len(seqs)))
        else:
            print(f"    '{word}': selecting medoids...", end=" ", flush=True)
            medoid_indices = kmedoids_select(seqs, best_weights, k=K_TEMPLATES)
            print("done.")

        selected = [seqs[i] for i in medoid_indices]
        templates_per_word.append(selected)
        for rank, idx in enumerate(medoid_indices):
            print(f"      Medoid {rank+1}: sample #{idx}  ({len(seqs[idx])} frames)")
            all_templates.append(seqs[idx])
            all_template_labels.append(word_idx)

    # ------------------------------------------------------------------
    # Train + test accuracy report
    # ------------------------------------------------------------------
    def report(split_name, split_seqs):
        print(f"\n  ── {split_name} ──")
        conf  = np.zeros((N_WORDS, N_WORDS), dtype=int)
        ok    = 0
        total = 0
        for true_idx, word in enumerate(word_folders):
            wok = 0
            for seq in split_seqs[word]:
                pred = classify_one(seq, all_templates, all_template_labels, best_weights)
                conf[true_idx, pred] += 1
                if pred == true_idx:
                    wok += 1
                    ok  += 1
                total += 1
            n = len(split_seqs[word])
            print(f"    {word:>14s}: {wok/n*100:6.2f}%  ({wok}/{n})")
        overall = ok / total * 100 if total else 0
        print(f"\n    Overall {split_name}: {overall:.2f}%  ({ok}/{total})")

        print(f"\n  Confusion Matrix ({split_name})")
        header = f"  {'':16s}" + "".join(f"{w:<10s}" for w in word_folders)
        print(header)
        print("  " + "-" * (16 + 10 * N_WORDS))
        for i, w in enumerate(word_folders):
            row = f"  {w:<16s}" + "".join(f"{conf[i,j]:<10d}" for j in range(N_WORDS))
            print(row)
        return overall

    train_acc = report("TRAIN", train_seqs)
    test_acc  = report("TEST",  test_seqs)

    gap = train_acc - test_acc
    print(f"\n  Train/Test gap: {gap:.2f}%  ", end="")
    print("⚠ possible overfitting" if gap > 20 else "✓ looks good")

    # ------------------------------------------------------------------
    # Export
    # ------------------------------------------------------------------
    export_c_header(word_folders, templates_per_word, best_weights, OUTPUT_HEADER)

    model_path     = "dtw_model.npz"
    flat_templates = [seq for tpls in templates_per_word for seq in tpls]
    flat_labels    = [wi  for wi, tpls in enumerate(templates_per_word) for _ in tpls]
    flat_lengths   = [len(seq) for seq in flat_templates]
    save_dict = {
        "word_labels":      np.array(word_folders),
        "template_labels":  np.array(flat_labels,  dtype=np.uint8),
        "template_lengths": np.array(flat_lengths, dtype=np.uint8),
        "best_weights":     best_weights,
        "n_templates":      np.array(len(flat_templates)),
        "vad_threshold":    np.array(VAD_THRESHOLD, dtype=np.uint8),
        "train_accuracy":   np.array(train_acc),
        "test_accuracy":    np.array(test_acc),
    }
    for i, t in enumerate(flat_templates):
        save_dict[f"template_{i}"] = t
    np.savez(model_path, **save_dict)
    print(f"  [OK] Model saved to '{model_path}'")

    print(f"\n{'':=<70}")
    print(f"  Done.  Train: {train_acc:.2f}%   Test: {test_acc:.2f}%")
    print(f"  Weights: {list(best_weights)}")
    print(f"{'':=<70}")


if __name__ == "__main__":
    import warnings
    warnings.filterwarnings("ignore")
    main()