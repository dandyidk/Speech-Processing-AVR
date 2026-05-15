import numpy as np
import re

# ================= CONFIG =================
MODEL_PATH = "/home/dandy/dataset/dtw_model.npz"

# same as AVR firmware
WEIGHTS = np.array([1, 1, 1, 1, 2, 2, 2, 2], dtype=np.int16)

SAKOE_CHIBA_W = 8
def vad_trim(seq, threshold=100):
    energies = seq[:, 0]  # feat[0] is STE
    active = np.where(energies >= threshold)[0]
    if len(active) == 0:
        return seq  # or return None and handle silence
    return seq[active[0]:active[-1] + 1]

# ================= LOAD FEATURE FILE =================
def load_feature_file(path):
    frames = []

    with open(path, "r") as f:
        for line in f:
            m = re.match(r"F\d+:\[([^\]]+)\]", line.strip())
            if not m:
                continue
            vals = [int(x) for x in m.group(1).split(",")]
            frames.append(vals)

    return np.array(frames, dtype=np.uint8)


# ================= DTW =================
def dtw_distance(a, b):
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


# ================= LOAD MODEL =================
data = np.load(MODEL_PATH, allow_pickle=True)

word_labels = data["word_labels"]
template_labels = data["template_labels"]
n_templates = int(data["n_templates"])

templates = []
for i in range(n_templates):
    templates.append(data[f"template_{i}"])


# ================= INFERENCE =================
def predict(feature_file):
    seq = load_feature_file(feature_file)
    seq = vad_trim(seq)
    print(f"After trim: {len(seq)} frames")  # add this
    print(templates[0])

    best_score = 1e18
    best_label = -1

    for i in range(n_templates):
        d = dtw_distance(seq, templates[i])
        print(f"Template {i} (label={template_labels[i]}): distance={d:.2f}")

        if d < best_score:
            best_score = d
            best_label = template_labels[i]

    return word_labels[best_label], best_score


# ================= RUN =================
if __name__ == "__main__":
    test_file = "/home/dandy/dataset/feature_test.txt"

    label, score = predict(test_file)

    print("PREDICTION:", label)
    print("SCORE:", score)